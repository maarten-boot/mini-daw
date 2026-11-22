#include "remote_server.hpp"
#include "minidaw/keys_text.hpp"

#include "minidaw/script.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace minidaw::app {

// --- RemoteChannel -------------------------------------------------------------

RemoteChannel::Result RemoteChannel::submit(RemoteJob job, RemoteReply& reply, const std::function<bool()>& gone)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (closed_) return Result::Closed;
    if (slots_.size() >= capacity_) return Result::Busy;
    auto slot = std::make_shared<Slot>();
    slot->id = nextId_++;
    slot->job = std::move(job);
    slots_.push_back(slot);
    for (;;) {
        if (cv_.wait_for(lock, std::chrono::milliseconds(250), [&] { return slot->reply.has_value() || closed_; })) break;
        if (gone && !slot->cancel) {
            lock.unlock();
            const bool away = gone();  // a socket check, without the lock
            lock.lock();
            if (away) {
                if (!slot->taken) break;  // never started: nothing to stop
                slot->cancel = true;      // the GUI thread ends it; its answer goes nowhere
            }
        }
    }
    slots_.remove(slot);
    if (!slot->reply) return Result::Closed;
    reply = std::move(*slot->reply);
    return Result::Done;
}

std::optional<RemoteChannel::Taken> RemoteChannel::take()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& s : slots_) {
        if (s->taken) continue;
        s->taken = true;
        return Taken{s->id, s->job};
    }
    return std::nullopt;
}

void RemoteChannel::answer(std::uint64_t id, RemoteReply reply)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& s : slots_) {
            if (s->id == id) s->reply = std::move(reply);
        }
    }
    cv_.notify_all();
}

bool RemoteChannel::requestCancel()
{
    std::lock_guard<std::mutex> lock(mutex_);
    bool any = false;
    for (auto& s : slots_) {
        if (s->taken && !s->reply) {
            s->cancel = true;
            any = true;
        }
    }
    return any;
}

bool RemoteChannel::cancelRequested(std::uint64_t id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& s : slots_) {
        if (s->id == id) return s->cancel;
    }
    return true;  // its request is gone (the client left before it was taken)
}

void RemoteChannel::close()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }
    cv_.notify_all();
}

// --- RemoteServer -------------------------------------------------------------

namespace {

void sendJson(httplib::Response& res, int status, const nlohmann::json& body)
{
    res.status = status;
    res.set_content(body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), "application/json");
}

void sendError(httplib::Response& res, int status, const std::string& why)
{
    sendJson(res, status, {{"ok", false}, {"error", why}});
}

RemoteRequest requestInfo(const httplib::Request& req)
{
    RemoteRequest r;
    r.clientAddress = req.remote_addr;
    r.host = req.get_header_value("Host");
    r.hasOrigin = req.has_header("Origin");
    r.authorization = req.get_header_value("Authorization");
    return r;
}

} // namespace

RemoteServer::RemoteServer(Log& log, RemoteBridge& bridge) : log_(log), bridge_(bridge) {}

RemoteServer::~RemoteServer() { stop(); }

void RemoteServer::logRefusal(const std::string& text)
{
    // A scan of the network could fill the log: at most 10 lines a minute.
    std::lock_guard<std::mutex> lock(refusedMutex_);
    const auto now = std::chrono::steady_clock::now();
    if (now - refusedSince_ > std::chrono::minutes(1)) {
        refusedSince_ = now;
        refusedLogged_ = 0;
    }
    if (++refusedLogged_ <= 10) log_.warning("Remote control: refused " + text);
    if (refusedLogged_ == 10) log_.warning("Remote control: more refusals this minute are not logged");
}

bool RemoteServer::start(const std::string& host, int port, RemotePolicy policy, std::string& error)
{
    if (server_) {
        error = "already listening";
        return false;
    }
    policy_ = std::move(policy);
    server_ = std::make_unique<httplib::Server>();
    httplib::Server& s = *server_;
    // The port is mini-daw's alone. cpp-httplib's default (SO_REUSEPORT on Linux,
    // SO_REUSEADDR on Windows) lets another program listen on the same port and
    // get some of the requests.
    s.set_socket_options([](socket_t sock) {
#if defined(_WIN32)
        httplib::set_socket_opt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
        httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);  // a quick restart; never a second listener
#endif
    });
    s.set_payload_max_length(64 * 1024);
    // One request per connection and short timeouts: closing mini-daw waits for
    // open connections, and a slow sender holds a server thread (crosscheck #24 CC3).
    s.set_keep_alive_max_count(1);
    s.set_read_timeout(2, 0);
    s.set_write_timeout(5, 0);

    // Who sent it, before anything else (minidaw/remote.hpp).
    s.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        const RemoteAccess a = checkRemoteAccess(requestInfo(req), policy_);
        if (a.status == 0) return httplib::Server::HandlerResponse::Unhandled;
        logRefusal(remoteLogText(req.remote_addr + " " + req.method + " " + req.path + ": " + a.why));
        sendError(res, a.status, a.why);
        return httplib::Server::HandlerResponse::Handled;
    });

    s.Get("/api/state", [this](const httplib::Request& req, httplib::Response& res) {
        RemoteJob job;
        job.kind = RemoteJob::Kind::State;
        job.client = req.remote_addr;
        RemoteReply reply;
        switch (bridge_.states.submit(std::move(job), reply)) {
        case RemoteChannel::Result::Done:
            res.status = reply.status;
            res.set_content(reply.body, "application/json");
            return;
        case RemoteChannel::Result::Busy:
            sendError(res, 409, "busy with another request; try again");
            return;
        case RemoteChannel::Result::Closed:
            sendError(res, 503, "mini-daw is closing");
            return;
        }
    });

    s.Get("/api/commands", [this](const httplib::Request& req, httplib::Response& res) {
        const bool loopback = checkRemoteAccess(requestInfo(req), policy_).loopback;
        nlohmann::json list = nlohmann::json::array();
        for (const ScriptCommand& c : scriptCommands()) {
            std::string why;
            const bool allowed = remoteCommandAllowed(c.name, loopback, why);
            list.push_back({{"name", c.name}, {"usage", c.usage}, {"text", keysHere(c.text)}, {"allowed", allowed}});  // CC8
        }
        sendJson(res, 200, {{"ok", true}, {"commands", list}});
    });

    s.Post("/api/command", [this](const httplib::Request& req, httplib::Response& res) {
        const RemoteAccess access = checkRemoteAccess(requestInfo(req), policy_);
        const std::string type = req.get_header_value("Content-Type");
        if (!remoteJsonContentType(type)) {
            sendError(res, 415, "send the command as JSON (Content-Type: application/json)");
            return;
        }
        std::string line;
        std::string why;
        if (!parseRemoteCommandBody(req.body, line, why)) {
            sendError(res, 400, why);
            return;
        }
        const ScriptParse parsed = parseScript(line);
        if (!parsed.errors.empty()) {
            std::string text = parsed.errors.front();
            if (text.rfind("line 1: ", 0) == 0) text.erase(0, 8);
            sendError(res, 400, text);
            return;
        }
        if (parsed.lines.empty()) {
            sendError(res, 400, "no command");
            return;
        }
        if (!remoteLineAllowed(parsed.lines.front().words, access.loopback, why)) {
            logRefusal(remoteLogText(req.remote_addr + ": " + line + ": " + why));
            sendError(res, 403, why);
            return;
        }
        RemoteJob job;
        job.kind = RemoteJob::Kind::Command;
        job.line = line;
        job.client = req.remote_addr;
        job.loopback = access.loopback;
        RemoteReply reply;
        switch (bridge_.commands.submit(std::move(job), reply, [&req] { return req.is_connection_closed(); })) {
        case RemoteChannel::Result::Done:
            res.status = reply.status;
            res.set_content(reply.body, "application/json");
            return;
        case RemoteChannel::Result::Busy:
            sendError(res, 409, "busy with another command; try again when it is done");
            return;
        case RemoteChannel::Result::Closed:
            sendError(res, 503, "mini-daw is closing");
            return;
        }
    });

    // Ends a running wait / wait-for at once (other commands end when they are done).
    s.Post("/api/cancel", [this](const httplib::Request&, httplib::Response& res) {
        const bool running = bridge_.commands.requestCancel();
        sendJson(res, 200,
                 {{"ok", true},
                  {"note", running ? "asked: a wait or wait-for ends at once, other commands when they are done"
                                   : "no command is running"}});
    });

    s.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.body.empty()) {
            sendError(res, res.status, res.status == 404 ? "no such address: use /api/state, /api/commands, /api/command or /api/cancel"
                                                          : "request not handled");
        }
    });

    if (!s.bind_to_port(host, port)) {
        error = "cannot listen on " + host + ":" + std::to_string(port) + " (is the port in use by another program?)";
        server_.reset();
        return false;
    }
    thread_ = std::thread([this] { server_->listen_after_bind(); });
    return true;
}

void RemoteServer::stop()
{
    bridge_.close();
    if (server_) server_->stop();
    if (thread_.joinable()) thread_.join();
    server_.reset();
}

} // namespace minidaw::app
