#include "minidaw/folder_browse.hpp"

#include "minidaw/path_util.hpp"

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <iterator>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace minidaw {

std::string cleanPathText(std::string_view text)
{
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
        while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
        while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
    }
    return std::string(text);
}

FolderListing listSubfolders(const fs::path& folder, bool showHidden)
{
    FolderListing out;
    std::error_code ec;
    fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) {
        out.error = "Cannot read " + pathToUtf8(folder) + ": " + ec.message();
        return out;
    }
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code e2;
        if (!it->is_directory(e2) || e2) continue;
        const std::wstring name = it->path().filename().wstring();
        if (!showHidden && !name.empty() && name.front() == L'.') continue;
#ifdef _WIN32
        const DWORD attr = GetFileAttributesW(it->path().c_str());
        if (!showHidden && attr != INVALID_FILE_ATTRIBUTES && (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
            continue;
#endif
        out.folders.push_back(it->path());
    }
    if (ec && out.error.empty()) out.error = "Not all of " + pathToUtf8(folder) + " could be read: " + ec.message();
    const auto key = [](const fs::path& p) {
        std::wstring n = p.filename().wstring();
        for (auto& c : n) c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
        return n;
    };
    std::sort(out.folders.begin(), out.folders.end(), [&](const fs::path& a, const fs::path& b) {
        const auto ka = key(a), kb = key(b);
        return ka != kb ? ka < kb : a.filename() < b.filename();
    });
    return out;
}

fs::path browseStart(std::string_view text, const fs::path& fallback)
{
    const std::string clean = cleanPathText(text);
    if (!clean.empty()) {
        std::error_code ec;
        fs::path p = fs::absolute(pathFromUtf8(clean), ec);
        if (!ec) {
            p = p.lexically_normal();
            for (int i = 0; i < 256 && !p.empty(); ++i) {
                if (fs::is_directory(p, ec)) return p;
                const fs::path parent = p.parent_path();
                if (parent == p) break;
                p = parent;
            }
        }
    }
    return fallback;
}

std::vector<fs::path> filesystemRoots()
{
#ifdef _WIN32
    std::vector<fs::path> out;
    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (drives & (1u << i)) {
            const wchar_t root[] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', 0};
            out.emplace_back(root);
        }
    }
    return out;
#else
    return {fs::path("/")};
#endif
}

fs::path homeFolder()
{
#ifdef _WIN32
    wchar_t buf[32768];
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, static_cast<DWORD>(std::size(buf)));
    if (n > 0 && n < std::size(buf)) return fs::path(std::wstring(buf, n));
#else
    if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h);
#endif
    return {};
}

} // namespace minidaw
