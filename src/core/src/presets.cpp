#include "minidaw/presets.hpp"

#include "file_util.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/track.hpp"  // safeFileName

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <iterator>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace minidaw {
namespace fs = std::filesystem;

namespace {

constexpr std::size_t kHeaderSize = 4 + 4 + 32 + 8;
constexpr std::int32_t kFormatVersion = 1;

void putInt32(std::string& out, std::int32_t v)
{
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((static_cast<std::uint32_t>(v) >> (8 * i)) & 0xFF));
}

void putInt64(std::string& out, std::int64_t v)
{
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((static_cast<std::uint64_t>(v) >> (8 * i)) & 0xFF));
}

std::uint64_t getLE(std::string_view b, std::size_t at, int bytes)
{
    std::uint64_t v = 0;
    for (int i = 0; i < bytes; ++i) v |= static_cast<std::uint64_t>(static_cast<unsigned char>(b[at + i])) << (8 * i);
    return v;
}

std::string lowerAscii(std::string s)
{
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string envVar(const char* name)
{
#if defined(_WIN32)
    wchar_t buf[32768];
    std::wstring wname;
    for (const char* p = name; *p; ++p) wname.push_back(static_cast<wchar_t>(*p));
    const DWORD n = GetEnvironmentVariableW(wname.c_str(), buf, 32768);
    if (n == 0 || n >= 32768) return {};
    return pathToUtf8(fs::path(std::wstring(buf, n)));
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

#if defined(_WIN32)
fs::path documentsFolder()
{
    PWSTR docs = nullptr;
    fs::path p;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs != nullptr) p = fs::path(docs);
    if (docs != nullptr) CoTaskMemFree(docs);
    if (p.empty()) {
        const std::string profile = envVar("USERPROFILE");
        if (!profile.empty()) p = pathFromUtf8(profile) / "Documents";
    }
    return p;
}
#endif

} // namespace

std::string encodeVstPreset(const VstPresetData& data)
{
    std::string out;
    out.append("VST3");
    putInt32(out, kFormatVersion);
    std::string id = data.classId.substr(0, 32);
    id.resize(32, '0');
    out.append(id);
    putInt64(out, 0);  // list offset, filled in below
    struct Entry {
        const char* id;
        std::int64_t offset, size;
    };
    std::vector<Entry> entries;
    entries.push_back({"Comp", static_cast<std::int64_t>(out.size()), static_cast<std::int64_t>(data.component.size())});
    out.append(data.component);
    if (!data.controller.empty()) {
        entries.push_back({"Cont", static_cast<std::int64_t>(out.size()), static_cast<std::int64_t>(data.controller.size())});
        out.append(data.controller);
    }
    const std::int64_t listOffset = static_cast<std::int64_t>(out.size());
    out.append("List");
    putInt32(out, static_cast<std::int32_t>(entries.size()));
    for (const Entry& e : entries) {
        out.append(e.id, 4);
        putInt64(out, e.offset);
        putInt64(out, e.size);
    }
    std::string offset;
    putInt64(offset, listOffset);
    out.replace(kHeaderSize - 8, 8, offset);
    return out;
}

std::optional<VstPresetData> decodeVstPreset(std::string_view b, std::string& error)
{
    if (b.size() < kHeaderSize || b.substr(0, 4) != "VST3") {
        error = "not a VST3 preset file";
        return std::nullopt;
    }
    VstPresetData d;
    d.classId = std::string(b.substr(8, 32));
    if (!isClassIdText(d.classId)) {  // FUID::fromString would read garbage (crosscheck #16, X7)
        error = "the preset file is damaged (its class ID is not valid)";
        return std::nullopt;
    }
    const std::uint64_t listOffset = getLE(b, 40, 8);
    if (listOffset < kHeaderSize || listOffset > b.size() || b.size() - listOffset < 8 ||
        b.substr(static_cast<std::size_t>(listOffset), 4) != "List") {
        error = "the preset file is damaged (no chunk list)";
        return std::nullopt;
    }
    std::size_t at = static_cast<std::size_t>(listOffset) + 4;
    const std::uint64_t count = getLE(b, at, 4);
    at += 4;
    bool haveComp = false;
    for (std::uint64_t i = 0; i < count && i < 128; ++i) {
        if (b.size() - at < 20) {
            error = "the preset file is damaged (chunk list cut off)";
            return std::nullopt;
        }
        const std::string_view id = b.substr(at, 4);
        const std::uint64_t off = getLE(b, at + 4, 8);
        const std::uint64_t size = getLE(b, at + 12, 8);
        at += 20;
        if (off > b.size() || size > b.size() - off) {
            error = "the preset file is damaged (a chunk lies outside the file)";
            return std::nullopt;
        }
        const std::string chunk(b.substr(static_cast<std::size_t>(off), static_cast<std::size_t>(size)));
        if (id == "Comp") {
            d.component = chunk;
            haveComp = true;
        } else if (id == "Cont") {
            d.controller = chunk;
        }
    }
    if (!haveComp) {
        error = "the preset file holds no instrument state";
        return std::nullopt;
    }
    return d;
}

PresetFolders presetFolders(const std::string& vendor, const std::string& pluginName, const fs::path& modulePath)
{
    const fs::path sub = pathFromUtf8(safePresetName(vendor.empty() ? "Unknown" : vendor)) /
                         pathFromUtf8(safePresetName(pluginName.empty() ? "Plugin" : pluginName));
    PresetFolders f;
#if defined(_WIN32)
    const fs::path docs = documentsFolder();
    if (!docs.empty()) f.user = docs / "VST3 Presets" / sub;
    for (const char* var : {"APPDATA", "PROGRAMDATA"}) {
        const std::string root = envVar(var);
        if (!root.empty()) f.all.push_back(pathFromUtf8(root) / "VST3 Presets" / sub);
    }
#else
    const std::string home = envVar("HOME");
    if (!home.empty()) f.user = pathFromUtf8(home) / ".vst3" / "presets" / sub;
    f.all.push_back(fs::path("/usr/share/vst3/presets") / sub);
    f.all.push_back(fs::path("/usr/local/share/vst3/presets") / sub);
#endif
    if (!f.user.empty()) f.all.insert(f.all.begin(), f.user);
    // Factory presets inside the plugin's bundle (<name>.vst3/Contents/<arch>/<module> or a
    // single-file module): <name>.vst3/Contents/Resources/Presets.
    for (fs::path p = modulePath; !p.empty() && p.has_parent_path() && p != p.parent_path(); p = p.parent_path()) {
        if (lowerAscii(pathToUtf8(p.extension())) == ".vst3") {
            std::error_code ec;
            if (fs::is_directory(p, ec)) f.all.push_back(p / "Contents" / "Resources" / "Presets");
            break;
        }
    }
    return f;
}

PresetListing listPresetFiles(const PresetFolders& folders, std::size_t limit, std::size_t searchLimit)
{
    PresetListing result;
    std::vector<PresetFileEntry>& out = result.files;
    bool stop = false;
    for (std::size_t r = 0; r < folders.all.size(); ++r) {
        const fs::path& root = folders.all[r];
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        std::vector<PresetFileEntry> here;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        std::size_t looked = 0;
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (++looked > searchLimit) {  // never stall on a huge tree
                result.searchCut = true;
                break;
            }
            std::error_code fe;
            if (!it->is_regular_file(fe)) continue;
            const fs::path& p = it->path();
            if (lowerAscii(pathToUtf8(p.extension())) != ".vstpreset") continue;
            if (out.size() + here.size() >= limit) {
                result.moreFiles = true;
                stop = true;
                break;
            }
            PresetFileEntry e;
            e.name = pathToUtf8(p.stem());
            const fs::path rel = p.parent_path().lexically_relative(root);
            if (!rel.empty() && rel != ".") {
                e.group = pathToUtf8(rel);
                std::replace(e.group.begin(), e.group.end(), '\\', '/');
            }
            e.path = p;
            e.user = !folders.user.empty() && root == folders.user;
            here.push_back(std::move(e));
        }
        std::sort(here.begin(), here.end(), [](const PresetFileEntry& a, const PresetFileEntry& b) {
            const std::string ga = lowerAscii(a.group), gb = lowerAscii(b.group);
            if (ga != gb) return ga < gb;
            return lowerAscii(a.name) < lowerAscii(b.name);
        });
        out.insert(out.end(), std::make_move_iterator(here.begin()), std::make_move_iterator(here.end()));
        if (stop) break;
    }
    return result;
}

std::string safePresetName(std::string_view name)
{
    std::string s = safeFileName(name);
    if (s == "track" && name.find("track") == std::string_view::npos) s = "Preset";  // safeFileName's fallback
    // At most 120 bytes, cut before a UTF-8 continuation byte; no space or dot at the end.
    if (s.size() > 120) {
        std::size_t cut = 120;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s.resize(cut);
        while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
        if (s.empty()) s = "Preset";
    }
    // Windows device names, also with an extension ("con.txt").
    std::string stem = lowerAscii(s.substr(0, s.find('.')));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    static const char* const kDevices[] = {"con", "prn", "aux", "nul"};
    bool device = std::find(std::begin(kDevices), std::end(kDevices), stem) != std::end(kDevices);
    if (stem.size() == 4 && (stem.rfind("com", 0) == 0 || stem.rfind("lpt", 0) == 0) && stem[3] >= '1' && stem[3] <= '9') {
        device = true;
    }
    return device ? "_" + s : s;
}

fs::path userPresetPath(const PresetFolders& folders, const std::string& name)
{
    return folders.user / pathFromUtf8(safePresetName(name) + ".vstpreset");
}

std::vector<PresetChoice> presetChoices(const std::vector<std::string>& programs,
                                        const std::vector<PresetFileEntry>& files)
{
    std::vector<PresetChoice> out;
    for (std::size_t i = 0; i < programs.size(); ++i) out.push_back({"p:" + std::to_string(i), programs[i]});
    for (const auto& f : files) {
        out.push_back({"f:" + pathToUtf8(f.path), (f.group.empty() ? std::string() : f.group + "/") + f.name});
    }
    return out;
}

int findPresetChoice(const std::vector<PresetChoice>& choices, const std::string& id, const std::string& label,
                     int currentProgram)
{
    if (!id.empty()) {
        for (std::size_t i = 0; i < choices.size(); ++i) {
            if (choices[i].id == id) return static_cast<int>(i);
        }
    }
    if (!label.empty()) {
        for (std::size_t i = 0; i < choices.size(); ++i) {
            if (choices[i].label == label) return static_cast<int>(i);
        }
        return -1;
    }
    if (id.empty() && currentProgram >= 0) {
        const std::string want = "p:" + std::to_string(currentProgram);
        for (std::size_t i = 0; i < choices.size(); ++i) {
            if (choices[i].id == want) return static_cast<int>(i);
        }
    }
    return -1;
}

int stepPresetChoice(std::size_t count, int current, int step)
{
    if (count == 0) return -1;
    const long long n = static_cast<long long>(count);
    long long i = current < 0 || current >= n ? (step < 0 ? n - 1 : 0) : current + step;
    i = (i % n + n) % n;
    return static_cast<int>(i);
}

int programSteps(int stepCount, std::size_t listSize)
{
    if (stepCount > 0) return stepCount;
    return listSize > 1 ? static_cast<int>(std::min<std::size_t>(listSize - 1, 4095)) : 0;
}

double programNormalized(int index, int steps)
{
    return steps > 0 ? static_cast<double>(std::clamp(index, 0, steps)) / steps : 0.0;
}

int programIndex(double normalized, int steps)
{
    if (steps <= 0) return 0;
    return std::clamp(static_cast<int>(std::lround(normalized * steps)), 0, steps);
}

std::size_t reachablePrograms(int steps, std::size_t listSize)
{
    return std::min<std::size_t>(listSize, static_cast<std::size_t>(std::max(steps, 0)) + 1);
}

bool isClassIdText(std::string_view text)
{
    return text.size() == 32 && std::all_of(text.begin(), text.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
           });
}

bool readWholeFile(const fs::path& path, std::string& bytes, std::string& error)
{
#if defined(_WIN32)
    std::FILE* f = _wfopen(path.c_str(), L"rb");
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) {
        error = "cannot open " + pathToUtf8(path) + ": " + std::strerror(errno);
        return false;
    }
    bytes.clear();
    char buf[65536];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        bytes.append(buf, n);
        if (bytes.size() > 256u * 1024u * 1024u) {  // no preset is that big
            std::fclose(f);
            error = pathToUtf8(path) + " is too large for a preset";
            return false;
        }
    }
    const bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (bad) {
        error = "cannot read " + pathToUtf8(path);
        return false;
    }
    return true;
}

bool writeFileReplacing(const fs::path& path, const std::string& bytes, std::string& error, bool sync)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "cannot make the folder " + pathToUtf8(path.parent_path()) + ": " + ec.message();
        return false;
    }
    fs::path tmp = path;
    tmp += ".tmp";
    std::FILE* f = detail::openForWriting(tmp, false);
    if (!f) {
        error = "cannot write " + pathToUtf8(tmp) + ": " + std::strerror(errno);
        return false;
    }
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size() && (!sync || detail::syncToDisk(f));
    const int writeErrno = errno;
    if (std::fclose(f) != 0 || !ok) {
        fs::remove(tmp, ec);
        error = "cannot write " + pathToUtf8(path) + ": " + std::strerror(writeErrno);
        return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        error = "cannot replace " + pathToUtf8(path) + ": " + ec.message();  // e.g. open in another program
        std::error_code ignored;
        fs::remove(tmp, ignored);
        return false;
    }
    return true;
}

} // namespace minidaw
