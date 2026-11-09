#pragma once

// One mini-daw window per config folder, so per user with the default config
// (P20, SPEC-RUN-01). The lock is the operating system's: a lock on the file
// `mini-daw.lock` in the config folder (Windows LockFileEx, elsewhere flock),
// released by the system when the program ends, also after a crash.
//
// A second start does not open a window: it asks the running one to come to
// the front (`requestShow`: writes `mini-daw.show` next to the lock) and quits.
// The running one looks for that request now and then (`showRequested`).
// The lock file holds the owner's process ID (Windows: to allow it to take
// the foreground).

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace minidaw {

class InstanceLock {
public:
    InstanceLock();
    ~InstanceLock();
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

    enum class Result { Acquired, HeldByOther, Failed };
    // Takes the lock in `folder` (made if missing). Failed: the lock file could
    // not be made (a read-only folder): the caller goes on without a lock.
    Result acquire(const std::filesystem::path& folder, std::string& error);
    bool held() const;
    void release();

    // The process ID written by the owner (0 if unknown).
    static std::uint64_t ownerProcess(const std::filesystem::path& folder);

    // Second start: ask the owner to show its window.
    static bool requestShow(const std::filesystem::path& folder);
    // Owner: true once for each new request since the last call (or since acquire).
    bool showRequested();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace minidaw
