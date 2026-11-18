#pragma once

// Remote control over HTTP + JSON (P25b, SPEC-RUN-03): the server.
//
// The server answers on its own threads (cpp-httplib). mini-daw's state belongs
// to the GUI thread, so a request is handed over through RemoteBridge: one at a
// time (the state: several), the GUI thread takes it at the start of a frame, runs it as a script
// line (main.cpp) and answers; the request's thread waits for that answer.
//
//   GET  /api/state     mini-daw's state (as the script command `state`)
//   GET  /api/commands  the commands, their use, and whether this client may send them
//   POST /api/command   {"line": "arm 1 on"} -> {"ok": true, "note": ..., "seconds": ..., "log": [...]}
//   POST /api/cancel    ends a running wait / wait-for at once
//
// Who may send what: minidaw/remote.hpp.

#include "minidaw/log.hpp"
#include "minidaw/remote.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace httplib {
class Server;
}

namespace minidaw::app {

struct RemoteJob {
    enum class Kind { State, Command } kind = Kind::Command;
    std::string line;    // Command: the script line
    std::string client;  // the address, for the log
    bool loopback = false;
};

struct RemoteReply {
    int status = 200;
    std::string body;  // JSON
};

// Requests handed from server threads to the GUI thread: at most `capacity`
// at a time (more get Busy). Each waits for its own answer.
class RemoteChannel {
public:
    enum class Result { Done, Busy, Closed };
    explicit RemoteChannel(std::size_t capacity = 1) : capacity_(capacity) {}
    // Server thread: hands the job over and waits for the answer. `gone` is
    // asked a few times a second: true = the client went away (the job is then
    // marked cancelled for the GUI thread, and the wait goes on until it answers).
    Result submit(RemoteJob job, RemoteReply& reply, const std::function<bool()>& gone = {});
    // GUI thread: the oldest job not taken yet (answer(id, ...) must follow).
    struct Taken {
        std::uint64_t id = 0;
        RemoteJob job;
    };
    std::optional<Taken> take();
    void answer(std::uint64_t id, RemoteReply reply);
    // Asks the job that was taken to stop (POST /api/cancel); false when none is.
    bool requestCancel();
    // GUI thread: whether the taken job `id` was cancelled or its client went away.
    bool cancelRequested(std::uint64_t id);
    // At the end: waiting requests get Closed, new ones too.
    void close();

private:
    struct Slot {
        std::uint64_t id = 0;
        RemoteJob job;
        bool taken = false;
        bool cancel = false;
        std::optional<RemoteReply> reply;
    };
    std::mutex mutex_;
    std::condition_variable cv_;
    std::list<std::shared_ptr<Slot>> slots_;
    std::size_t capacity_;
    std::uint64_t nextId_ = 1;
    bool closed_ = false;
};

// Commands one at a time; the state can be asked for while a command runs
// (e.g. while an instrument loads), by several clients at once.
struct RemoteBridge {
    RemoteChannel commands{1};
    RemoteChannel states{16};
    void close()
    {
        commands.close();
        states.close();
    }
};

class RemoteServer {
public:
    RemoteServer(Log& log, RemoteBridge& bridge);
    ~RemoteServer();
    RemoteServer(const RemoteServer&) = delete;
    RemoteServer& operator=(const RemoteServer&) = delete;

    // Listens on `host` ("127.0.0.1", or "0.0.0.0" when networks are allowed).
    // False with `error` (e.g. the port is in use).
    bool start(const std::string& host, int port, RemotePolicy policy, std::string& error);
    void stop();
    bool running() const { return thread_.joinable(); }

private:
    Log& log_;
    RemoteBridge& bridge_;
    RemotePolicy policy_;
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    std::mutex refusedMutex_;
    int refusedLogged_ = 0;  // refusals written to the log, at most a few a minute
    std::chrono::steady_clock::time_point refusedSince_{};
    void logRefusal(const std::string& text);
};

} // namespace minidaw::app
