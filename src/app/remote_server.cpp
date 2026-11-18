#include "remote_server.hpp"

#include "minidaw/script.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace minidaw::app {

// --- RemoteChannel -------------------------------------------------------------

RemoteChannel::Result RemoteChannel::submit(RemoteJob job, RemoteReply& reply)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (closed_) return Result::Closed;
    if (inProgress_) return Result::Busy;
    inProgress_ = true;
    waiting_ = std::move(job);
    reply_.reset();
    cv_.wait(lock, [&] { return reply_.has_value() || closed_; });
    inProgress_ = false;
    if (!reply_) {
        waiting_.reset();
        return Result::Closed;
    }
    reply = std::move(*reply_);
    reply_.reset();
    return Result::Done;
}

std::optional<RemoteJob> RemoteChannel::take()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<RemoteJob> job = std::move(waiting_);
    waiting_.reset();
    return job;
}

void RemoteChannel::answer(RemoteReply reply)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reply_ = std::move(reply);
    }
    cv_.notify_all();
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
    s.set_read_timeout(10, 0);
    s.set_write_timeout(10, 0);

    // Who sent it, before anything else (minidaw/remote.hpp).
    s.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        const RemoteAccess a = checkRemoteAccess(requestInfo(req), policy_);
        if (a.status == 0) return httplib::Server::HandlerResponse::Unhandled;
        logRefusal(req.remote_addr + " " + req.method + " " + req.path + ": " + a.why);
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
            list.push_back({{"name", c.name}, {"usage", c.usage}, {"text", c.text}, {"allowed", allowed}});
        }
        sendJson(res, 200, {{"ok", true}, {"commands", list}});
    });

    s.Post("/api/command", [this](const httplib::Request& req, httplib::Response& res) {
        const RemoteAccess access = checkRemoteAccess(requestInfo(req), policy_);
        const std::string type = req.get_header_value("Content-Type");
        if (type.rfind("application/json", 0) != 0) {
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
        if (!remoteCommandAllowed(parsed.lines.front().words.front(), access.loopback, why)) {
            logRefusal(req.remote_addr + ": " + line + ": " + why);
            sendError(res, 403, why);
            return;
        }
        RemoteJob job;
        job.kind = RemoteJob::Kind::Command;
        job.line = line;
        job.client = req.remote_addr;
        job.loopback = access.loopback;
        RemoteReply reply;
        switch (bridge_.commands.submit(std::move(job), reply)) {
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

    s.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.body.empty()) {
            sendError(res, res.status, res.status == 404 ? "no such address: use /api/state, /api/commands or /api/command"
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
