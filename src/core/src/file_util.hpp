#pragma once

// Small file helpers shared by the WAV and MIDI writers (internal header).

#include <cstdio>
#include <filesystem>

#if defined(_WIN32)
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

namespace minidaw::detail {

// Opens for binary writing ("wb") or updating ("r+b"). On Windows others may
// read the file while it is open, but not write to it (SPEC-REC-05a).
inline std::FILE* openForWriting(const std::filesystem::path& path, bool update)
{
#if defined(_WIN32)
    return _wfsopen(path.c_str(), update ? L"r+b" : L"wb", _SH_DENYWR);
#else
    return std::fopen(path.c_str(), update ? "r+b" : "wb");
#endif
}

// Flush the C buffer and force the data onto the disk (FlushFileBuffers / fsync).
inline bool syncToDisk(std::FILE* file)
{
    if (std::fflush(file) != 0) return false;
#if defined(_WIN32)
    return _commit(_fileno(file)) == 0;
#else
    return ::fsync(fileno(file)) == 0;
#endif
}

} // namespace minidaw::detail
