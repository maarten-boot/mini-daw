#include "minidaw/folder_browse.hpp"

#include "minidaw/path_util.hpp"

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <iterator>
#include <utility>

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
    // A separator at the end is dropped (V6), not that of a root ("/", "C:\\").
#ifdef _WIN32
    const auto isSep = [](char c) { return c == '/' || c == '\\'; };
#else
    const auto isSep = [](char c) { return c == '/'; };
#endif
    while (text.size() > 1 && isSep(text.back())) {
        if (text.size() == 3 && text[1] == ':') break;  // "C:\\": keep the root
        text.remove_suffix(1);
    }
    return std::string(text);
}

namespace {

// A name to sort by, case ignored. Linux: a file name is bytes and need not be
// UTF-8 (e.g. Latin-1 from an old disk); converting it to wide text throws
// (crosscheck #14, V1), so the bytes are compared, A-Z folded. Windows: names
// are UTF-16, folded with towlower.
#ifdef _WIN32
using SortKey = std::wstring;
SortKey sortKey(const fs::path& p)
{
    std::wstring n = p.filename().native();
    for (auto& c : n) c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    return n;
}
#else
using SortKey = std::string;
SortKey sortKey(const fs::path& p)
{
    std::string n = p.filename().native();
    for (auto& c : n) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return n;
}
#endif

bool startsWithDot(const fs::path& p)
{
    const fs::path name = p.filename();
    const auto& n = name.native();  // no conversion: works for any name
    return !n.empty() && n.front() == '.';
}

} // namespace

FolderListing listSubfolders(const fs::path& folder, bool showHidden)
{
    FolderListing out;
    try {
        std::error_code ec;
        // No skip_permission_denied: a folder that may not be read must say so,
        // not look empty (crosscheck #14 test).
        fs::directory_iterator it(folder, ec), end;
        if (ec) {
            out.error = "Cannot read " + pathToUtf8(folder) + ": " + ec.message();
            return out;
        }
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            std::error_code e2;
            if (!it->is_directory(e2) || e2) continue;
            if (!showHidden && startsWithDot(it->path())) continue;
#ifdef _WIN32
            const DWORD attr = GetFileAttributesW(it->path().c_str());
            if (!showHidden && attr != INVALID_FILE_ATTRIBUTES && (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                continue;
#endif
            out.folders.push_back(it->path());
        }
        if (ec) out.error = "Not all of " + pathToUtf8(folder) + " could be read: " + ec.message();
        std::vector<std::pair<SortKey, fs::path>> keyed;
        keyed.reserve(out.folders.size());
        for (auto& f : out.folders) keyed.emplace_back(sortKey(f), std::move(f));
        std::sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first < b.first : a.second.filename().native() < b.second.filename().native();
        });
        out.folders.clear();
        for (auto& k : keyed) out.folders.push_back(std::move(k.second));
    } catch (const std::exception& e) {  // any other file system surprise: reported, not fatal
        out.folders.clear();
        out.error = "Cannot read " + pathToUtf8(folder) + ": " + e.what();
    }
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
            if (!p.has_filename() && p.has_relative_path()) p = p.parent_path();  // "/a/b/" -> "/a/b" (V6)
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
