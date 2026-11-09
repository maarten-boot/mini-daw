#include "minidaw/instance_lock.hpp"

#include "minidaw/path_util.hpp"

#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace minidaw {
namespace fs = std::filesystem;

namespace {

fs::path lockPath(const fs::path& folder)
{
    return folder / "mini-daw.lock";
}

fs::path showPath(const fs::path& folder)
{
    return folder / "mini-daw.show";
}

std::uint64_t currentProcess()
{
#if defined(_WIN32)
    return GetCurrentProcessId();
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

std::string readSmallFile(const fs::path& p)
{
#if defined(_WIN32)
    std::FILE* f = _wfopen(p.c_str(), L"rb");
#else
    std::FILE* f = std::fopen(p.c_str(), "rb");
#endif
    if (!f) return {};
    char buf[128];
    const std::size_t n = std::fread(buf, 1, sizeof buf, f);
    std::fclose(f);
    return std::string(buf, n);
}

} // namespace

struct InstanceLock::Impl {
    fs::path folder;
    bool held = false;
    std::string lastShow;  // the request seen last
#if defined(_WIN32)
    HANDLE file = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
#endif
};

InstanceLock::InstanceLock() : impl_(std::make_unique<Impl>()) {}

InstanceLock::~InstanceLock()
{
    release();
}

bool InstanceLock::held() const
{
    return impl_->held;
}

InstanceLock::Result InstanceLock::acquire(const fs::path& folder, std::string& error)
{
    release();
    Impl& m = *impl_;
    std::error_code ec;
    fs::create_directories(folder, ec);
    const fs::path p = lockPath(folder);
    const std::string pid = std::to_string(currentProcess()) + "\n";
#if defined(_WIN32)
    // Others may read it (the process ID); the lock is on a byte far beyond the text.
    m.file = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m.file == INVALID_HANDLE_VALUE) {
        error = "cannot open " + pathToUtf8(p) + " (error " + std::to_string(GetLastError()) + ")";
        return Result::Failed;
    }
    OVERLAPPED at{};
    at.Offset = 0;
    at.OffsetHigh = 1;  // byte 4 GiB: never part of the text
    if (!LockFileEx(m.file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &at)) {
        const DWORD e = GetLastError();
        CloseHandle(m.file);
        m.file = INVALID_HANDLE_VALUE;
        if (e == ERROR_LOCK_VIOLATION || e == ERROR_IO_PENDING) return Result::HeldByOther;
        error = "cannot lock " + pathToUtf8(p) + " (error " + std::to_string(e) + ")";
        return Result::Failed;
    }
    SetFilePointer(m.file, 0, nullptr, FILE_BEGIN);
    SetEndOfFile(m.file);
    DWORD written = 0;
    WriteFile(m.file, pid.data(), static_cast<DWORD>(pid.size()), &written, nullptr);
    FlushFileBuffers(m.file);
#else
    m.fd = ::open(p.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (m.fd < 0) {
        error = "cannot open " + pathToUtf8(p) + ": " + std::strerror(errno);
        return Result::Failed;
    }
    if (::flock(m.fd, LOCK_EX | LOCK_NB) != 0) {
        const int e = errno;
        ::close(m.fd);
        m.fd = -1;
        if (e == EWOULDBLOCK) return Result::HeldByOther;
        error = "cannot lock " + pathToUtf8(p) + ": " + std::strerror(e);
        return Result::Failed;
    }
    if (::ftruncate(m.fd, 0) == 0) {
        const ssize_t n = ::pwrite(m.fd, pid.data(), pid.size(), 0);
        (void)n;
    }
#endif
    m.folder = folder;
    m.held = true;
    m.lastShow = readSmallFile(showPath(folder));  // only requests made from now on count
    return Result::Acquired;
}

void InstanceLock::release()
{
    Impl& m = *impl_;
#if defined(_WIN32)
    if (m.file != INVALID_HANDLE_VALUE) {
        OVERLAPPED at{};
        at.OffsetHigh = 1;
        UnlockFileEx(m.file, 0, 1, 0, &at);
        CloseHandle(m.file);
        m.file = INVALID_HANDLE_VALUE;
    }
#else
    if (m.fd >= 0) {
        ::flock(m.fd, LOCK_UN);
        ::close(m.fd);
        m.fd = -1;
    }
#endif
    m.held = false;
}

std::uint64_t InstanceLock::ownerProcess(const fs::path& folder)
{
    const std::string text = readSmallFile(lockPath(folder));
    try {
        return text.empty() ? 0 : std::stoull(text);
    } catch (...) {
        return 0;
    }
}

bool InstanceLock::requestShow(const fs::path& folder)
{
    // A new text each time (time and a random number), so every request is seen.
    std::random_device rd;
    const std::string token = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                              std::to_string(rd()) + "\n";
    const fs::path p = showPath(folder);
#if defined(_WIN32)
    std::FILE* f = _wfopen(p.c_str(), L"wb");
#else
    std::FILE* f = std::fopen(p.c_str(), "wb");
#endif
    if (!f) return false;
    const bool ok = std::fwrite(token.data(), 1, token.size(), f) == token.size();
    return std::fclose(f) == 0 && ok;
}

bool InstanceLock::showRequested()
{
    Impl& m = *impl_;
    if (!m.held) return false;
    const std::string now = readSmallFile(showPath(m.folder));
    if (now.empty() || now == m.lastShow) return false;
    m.lastShow = now;
    return true;
}

} // namespace minidaw
