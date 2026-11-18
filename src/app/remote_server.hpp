#pragma once

// Remote control over HTTP + JSON (P25b, SPEC-RUN-03): the server.
//
// The server answers on its own threads (cpp-httplib). mini-daw's state belongs
// to the GUI thread, so a request is handed over through RemoteBridge: one at a
// time, the GUI thread takes it at the start of a frame, runs it as a script
// line (main.cpp) and answers; the request's thread waits for that answer.
//
//   GET  /api/state     mini-daw's state (as the script command `state`)
//   GET  /api/commands  the commands, their use, and whether this client may send them
//   POST /api/command   {"line": "arm 1 on"} -> {"ok": true, "note": ..., "seconds": ..., "log": [...]}
//
// Who may send what: minidaw/remote.hpp.

#include "minidaw/log.hpp"
#include "minidaw/remote.hpp"

#include <chrono>
#include <condition_variable>
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

// One request at a time, handed from a server thread to the GUI thread.
class RemoteChannel {
public:
    enum class Result { Done, Busy, Closed };
    // Server thread: hands the job over and waits for the answer.
    Result submit(RemoteJob job, RemoteReply& reply);
    // GUI thread: the waiting job, if any (then answer() must follow).
    std::optional<RemoteJob> take();
    void answer(RemoteReply reply);
    // At the end: waiting requests get Closed, new ones too.
    void close();

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<RemoteJob> waiting_;
    std::optional<RemoteReply> reply_;
    bool inProgress_ = false;
    bool closed_ = false;
};

// Commands one at a time; the state can be asked for while a command runs
// (e.g. while an instrument loads).
struct RemoteBridge {
    RemoteChannel commands;
    RemoteChannel states;
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
