#pragma once

// A ZIP file written with miniz (internal header): entries from memory or
// from files. Used by the DAWproject export (P27) and the audio export (P29).

#include "minidaw/path_util.hpp"

#include <miniz.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace minidaw::detail {

// Writes the ZIP: entries from memory or from files, stored or compressed.
class ZipWriter {
public:
    ~ZipWriter()
    {
        if (open_) mz_zip_writer_end(&zip_);
        if (file_) std::fclose(file_);
    }
    bool begin(const std::filesystem::path& path, std::string& error)
    {
#if defined(_WIN32)
        file_ = _wfopen(path.c_str(), L"wb");
#else
        file_ = std::fopen(path.c_str(), "wb");
#endif
        if (!file_) {
            error = "cannot write " + pathToUtf8(path) + ": " + std::strerror(errno);
            return false;
        }
        std::memset(&zip_, 0, sizeof zip_);
        if (!mz_zip_writer_init_cfile(&zip_, file_, 0)) return fail(error);
        open_ = true;
        return true;
    }
    bool addText(const std::string& name, const std::string& text, std::string& error)
    {
        if (!mz_zip_writer_add_mem(&zip_, name.c_str(), text.data(), text.size(), MZ_DEFAULT_LEVEL)) return fail(error);
        return true;
    }
    bool addBytes(const std::string& name, const std::string& bytes, std::string& error)
    {
        return addText(name, bytes, error);
    }
    // A WAV file, stored without compression (it hardly shrinks, and big takes stay fast).
    bool addFile(const std::string& name, const std::filesystem::path& source, std::string& error)
    {
#if defined(_WIN32)
        std::FILE* src = _wfopen(source.c_str(), L"rb");
#else
        std::FILE* src = std::fopen(source.c_str(), "rb");
#endif
        if (!src) {
            error = "cannot read " + pathToUtf8(source) + ": " + std::strerror(errno);
            return false;
        }
        std::error_code ec;
        const auto size = std::filesystem::file_size(source, ec);
        const bool ok = !ec && mz_zip_writer_add_cfile(&zip_, name.c_str(), src, size, nullptr, nullptr, 0,
                                                       MZ_NO_COMPRESSION, nullptr, 0, nullptr, 0);
        std::fclose(src);
        if (!ok) {
            if (ec) {
                error = "cannot read " + pathToUtf8(source) + ": " + ec.message();
                return false;
            }
            return fail(error);
        }
        return true;
    }
    bool finish(std::string& error)
    {
        if (!mz_zip_writer_finalize_archive(&zip_)) return fail(error);
        mz_zip_writer_end(&zip_);
        open_ = false;
        const bool flushed = std::fflush(file_) == 0;
        const bool closed = std::fclose(file_) == 0;
        file_ = nullptr;
        if (!flushed || !closed) {
            error = std::string("cannot write the file: ") + std::strerror(errno);
            return false;
        }
        return true;
    }

private:
    bool fail(std::string& error)
    {
        error = std::string("ZIP: ") + mz_zip_get_error_string(mz_zip_get_last_error(&zip_));
        return false;
    }
    std::FILE* file_ = nullptr;
    mz_zip_archive zip_{};
    bool open_ = false;
};

} // namespace minidaw::detail
