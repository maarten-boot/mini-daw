#pragma once

// While loading or unloading a plugin library: no Windows system error
// dialogs ("... is not a valid Win32 application", missing DLL, ...). The
// load fails with an error instead; a dialog would stop the scan (and hang an
// unattended test run). Only this thread is affected. Internal header.

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace minidaw::detail {

class NoErrorDialogs {
public:
#if defined(_WIN32)
    NoErrorDialogs() { SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX, &old_); }
    ~NoErrorDialogs() { SetThreadErrorMode(old_, nullptr); }
#else
    NoErrorDialogs() {}   // user-provided: no "unused variable" warning
    ~NoErrorDialogs() {}
#endif
    NoErrorDialogs(const NoErrorDialogs&) = delete;
    NoErrorDialogs& operator=(const NoErrorDialogs&) = delete;

private:
#if defined(_WIN32)
    DWORD old_ = 0;
#endif
};

} // namespace minidaw::detail
