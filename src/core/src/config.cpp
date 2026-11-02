#include "minidaw/config.hpp"
#include "minidaw/path_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace minidaw {
namespace {

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// A value is either "quoted" (with \" and \\ escapes, any other backslash is
// kept as it is, so "D:\Takes" works; a final \" before the end of the line
// or a comment is a backslash plus the closing quote, so "D:\Takes\" works
// too; anything after the closing quote is ignored) or plain (a trailing
// "; comment" or "# comment" is removed; backslashes are kept as typed).
// After a \" : is this the last quote of the line, followed only by spaces or a
// comment? Values written by mini-daw always end in a real closing quote, so
// this only matches hand-written values such as "D:\Takes\".
bool onlyCommentAfter(std::string_view rest)
{
    if (rest.find('"') != std::string_view::npos) return false;
    rest = trim(rest);
    return rest.empty() || rest.front() == ';' || rest.front() == '#';
}

std::string parseValue(std::string_view value, bool& ok)
{
    ok = true;
    value = trim(value);
    if (value.empty() || value.front() != '"') {
        std::string_view plain = value;
        for (std::size_t i = 0; i < plain.size(); ++i) {
            if ((plain[i] == ';' || plain[i] == '#') &&
                (i == 0 || std::isspace(static_cast<unsigned char>(plain[i - 1])))) {
                plain = plain.substr(0, i);
                break;
            }
        }
        return std::string(trim(plain));
    }
    std::string out;
    for (std::size_t i = 1; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '\\' && i + 1 < value.size() && value[i + 1] == '"' && onlyCommentAfter(value.substr(i + 2))) {
            out += '\\';  // "D:\Takes\": the last quote closes the value (J7)
            return out;
        }
        if (c == '\\' && i + 1 < value.size() && (value[i + 1] == '"' || value[i + 1] == '\\')) {
            out += value[++i];
        } else if (c == '"') {
            return out;
        } else {
            out += c;
        }
    }
    ok = false;  // no closing quote
    return out;
}

bool parseUnsigned(const std::string& text, unsigned int& out)
{
    if (text.empty()) return false;
    unsigned long long v = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<unsigned>(c - '0');
        if (v > 10000000ULL) return false;
    }
    out = static_cast<unsigned int>(v);
    return true;
}

// true/false, yes/no, on/off, 1/0 (any case).
bool parseBool(const std::string& text, bool& out)
{
    const std::string v = lower(text);
    if (v == "true" || v == "yes" || v == "on" || v == "1") { out = true; return true; }
    if (v == "false" || v == "no" || v == "off" || v == "0") { out = false; return true; }
    return false;
}

std::vector<std::string_view> splitLines(std::string_view text)
{
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

// "[name]" -> "name" (lower case); otherwise empty.
std::string sectionName(std::string_view line)
{
    line = trim(line);
    if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
        return lower(trim(line.substr(1, line.size() - 2)));
    }
    return {};
}

std::string getEnv(const char* name)
{
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return {};
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

} // namespace

ConfigLoadResult parseConfig(std::string_view text, std::string_view sourceName)
{
    ConfigLoadResult result;
    std::string section;
    int lineNo = 0;
    auto warn = [&](const std::string& msg) {
        result.warnings.push_back(std::string(sourceName) + " line " + std::to_string(lineNo) + ": " + msg);
    };

    for (std::string_view raw : splitLines(text)) {
        ++lineNo;
        std::string_view line = trim(raw);
        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }
        if (line.front() == '[') {
            section = sectionName(line);
            if (section.empty()) warn("malformed section header");
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            warn("expected 'key = value'");
            continue;
        }
        const std::string key = lower(trim(line.substr(0, eq)));
        bool quotedOk = true;
        const std::string value = parseValue(line.substr(eq + 1), quotedOk);
        if (!quotedOk) {
            warn("missing closing quote");
        }
        AppConfig& cfg = result.config;

        if (section == "ui" && key == "min_contrast") {
            ContrastParseResult parsed = parseContrastSetting(value);
            if (parsed.setting) {
                cfg.contrast = *parsed.setting;
            } else {
                warn(parsed.error + " Using AA (4.5:1).");
            }
        } else if (section == "ui" && key == "font_size") {
            unsigned int px = 0;
            if (parseUnsigned(value, px) && static_cast<float>(px) >= kMinFontSize && static_cast<float>(px) <= kMaxFontSize) {
                cfg.fontSize = static_cast<float>(px);
            } else {
                warn("font_size '" + value + "' must be a whole number from 12 to 32. Using 16.");
            }
        } else if (section == "audio" && key == "input_device") {
            cfg.audio.inputDevice = value.empty() ? kDefaultDevice : value;
        } else if (section == "audio" && key == "output_device") {
            cfg.audio.outputDevice = value.empty() ? kDefaultDevice : value;
        } else if (section == "audio" && key == "sample_rate") {
            unsigned int rate = 0;
            if (parseUnsigned(value, rate) && rate >= 8000 && rate <= 384000) {
                cfg.audio.sampleRate = rate;
            } else {
                warn("sample_rate '" + value + "' must be a number from 8000 to 384000. Using 48000.");
            }
        } else if (section == "audio" && key == "buffer_size") {
            unsigned int frames = 0;
            if (parseUnsigned(value, frames) && frames >= 16 && frames <= 8192) {
                cfg.audio.bufferFrames = frames;
            } else {
                warn("buffer_size '" + value + "' must be a number from 16 to 8192. Using 256.");
            }
        } else if (section == "recording" && key == "sync_to_disk") {
            if (!parseBool(value, cfg.recording.syncToDisk)) {
                cfg.recording.syncToDisk = true;
                warn("sync_to_disk '" + value + "' must be true or false. Using true.");
            }
        } else if (section == "recording" && key == "folder") {
            cfg.recording.folder = value;
        } else if (section == "recording" && key == "midi_timing") {
            if (!parseMidiTiming(value, cfg.recording.midiTiming)) {
                cfg.recording.midiTiming = MidiTiming::Ppq;
                warn("midi_timing '" + value + "' must be ppq or smpte. Using ppq.");
            }
        } else if (section == "recording" && key == "latency_correction_ms") {
            char* end = nullptr;
            const long v = std::strtol(value.c_str(), &end, 10);
            if (value.empty() || end == value.c_str() || *end != '\0' || v < -500 || v > 500) {
                warn("latency_correction_ms '" + value + "' must be a whole number from -500 to 500. Using 0.");
            } else {
                cfg.recording.latencyCorrectionMs = static_cast<int>(v);
            }
        } else {
            warn("unknown setting '" + key + "' in section [" + section + "]");
        }
    }
    return result;
}

std::string setConfigValue(std::string_view text, std::string_view section, std::string_view key,
                           std::string_view value)
{
    const std::string wantSection = lower(section);
    const std::string wantKey = lower(key);
    const std::string newLine = std::string(key) + " = " + std::string(value);

    std::vector<std::string> lines;
    for (std::string_view l : splitLines(text)) lines.emplace_back(l);

    std::string current;
    std::ptrdiff_t sectionEnd = -1;  // index after the last line of the wanted section
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = trim(lines[i]);
        if (!line.empty() && line.front() == '[') {
            current = sectionName(line);
            if (current == wantSection) sectionEnd = static_cast<std::ptrdiff_t>(i) + 1;
            continue;
        }
        if (current != wantSection) continue;
        if (!line.empty() && line.front() != ';' && line.front() != '#') {
            sectionEnd = static_cast<std::ptrdiff_t>(i) + 1;
        }
        const std::size_t eq = line.find('=');
        if (eq != std::string_view::npos && lower(trim(line.substr(0, eq))) == wantKey) {
            lines[i] = newLine;
            std::string out;
            for (const auto& l : lines) out += l + "\n";
            return out;
        }
    }

    if (sectionEnd >= 0) {
        lines.insert(lines.begin() + sectionEnd, newLine);
    } else {
        if (!lines.empty() && !trim(lines.back()).empty()) lines.emplace_back();
        lines.push_back("[" + std::string(section) + "]");
        lines.push_back(newLine);
    }
    std::string out;
    for (const auto& l : lines) out += l + "\n";
    return out;
}

std::string quoteConfigString(std::string_view value)
{
    std::string out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

std::string defaultConfigText()
{
    return "; mini-daw configuration\n"
           "\n"
           "[ui]\n"
           "; Minimum contrast ratio for all screen colours:\n"
           ";   AA  = 4.5:1 (default), AAA = 7:1, Max = 21:1 (pure black on white),\n"
           ";   or a number from 4.5 to 21.\n"
           "min_contrast = AA\n"
           "; Text size in pixels, 12 to 32.\n"
           "font_size = 16\n"
           "\n"
           "[audio]\n"
           "; Device names as shown in the program, in quotes,\n"
           "; or (default) for the system default, or (none).\n"
           "input_device = (default)\n"
           "output_device = (default)\n"
           "; Session sample rate in Hz, and buffer size in frames.\n"
           "sample_rate = 48000\n"
           "buffer_size = 256\n"
           "\n"
           "[recording]\n"
           "; Force take files to the disk once a second, so a take survives a\n"
           "; power loss. Set to false if recording stutters on a slow disk.\n"
           "sync_to_disk = true\n"
           "; Folder for takes. Empty: Documents\\mini-daw on Windows, ~/Music/mini-daw\n"
           "; on Linux and macOS. Without quotes the path is used as typed\n"
           "; (folder = \\\\server\\takes). Inside quotes, \\\\ stands for one \\.\n"
           "folder = \"\"\n"
           "; MIDI file timing: ppq (960 ticks per quarter at 120 BPM) or smpte (1 ms ticks).\n"
           "midi_timing = ppq\n"
           "; Overdub: new audio takes are moved earlier by the estimated round-trip\n"
           "; latency plus this many ms (-500 to 500), so they line up with what you\n"
           "; heard. Raise it if overdubs land late, lower it if they land early.\n"
           "latency_correction_ms = 0\n";
}

std::filesystem::path defaultConfigPath()
{
    namespace fs = std::filesystem;
#if defined(_WIN32)
    const std::string base = getEnv("APPDATA");
    if (!base.empty()) return fs::path(base) / "mini-daw" / "config.ini";
#elif defined(__APPLE__)
    const std::string home = getEnv("HOME");
    if (!home.empty()) return fs::path(home) / "Library" / "Application Support" / "mini-daw" / "config.ini";
#else
    const std::string xdg = getEnv("XDG_CONFIG_HOME");
    if (!xdg.empty()) return fs::path(xdg) / "mini-daw" / "config.ini";
    const std::string home = getEnv("HOME");
    if (!home.empty()) return fs::path(home) / ".config" / "mini-daw" / "config.ini";
#endif
    return fs::path("mini-daw-config.ini");
}

std::filesystem::path defaultRecordingFolder()
{
    namespace fs = std::filesystem;
#if defined(_WIN32)
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs != nullptr) {
        fs::path p = fs::path(docs) / "mini-daw";
        CoTaskMemFree(docs);
        return p;
    }
    if (docs != nullptr) CoTaskMemFree(docs);
    const std::string profile = getEnv("USERPROFILE");
    if (!profile.empty()) return fs::path(profile) / "Documents" / "mini-daw";
#else
    const std::string home = getEnv("HOME");
    if (!home.empty()) return fs::path(home) / "Music" / "mini-daw";
#endif
    return fs::path("mini-daw-takes");
}

std::filesystem::path recordingFolder(const RecordingSettings& settings)
{
    if (settings.folder.empty()) return defaultRecordingFolder();
    return pathFromUtf8(settings.folder);
}

ConfigLoadResult loadConfigFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream text;
    text << in.rdbuf();
    return parseConfig(text.str(), pathToUtf8(path.filename()));
}

std::string saveConfigFile(const std::filesystem::path& path, const AppConfig& config)
{
    std::string text;
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            text = ss.str();
        } else {
            text = defaultConfigText();
        }
    }
    auto deviceValue = [](const std::string& name) {
        return (name == kDefaultDevice || name == kNoDevice) ? name : quoteConfigString(name);
    };
    text = setConfigValue(text, "ui", "min_contrast", config.contrast.toString());
    text = setConfigValue(text, "ui", "font_size", std::to_string(static_cast<int>(config.fontSize)));
    text = setConfigValue(text, "audio", "input_device", deviceValue(config.audio.inputDevice));
    text = setConfigValue(text, "audio", "output_device", deviceValue(config.audio.outputDevice));
    text = setConfigValue(text, "audio", "sample_rate", std::to_string(config.audio.sampleRate));
    text = setConfigValue(text, "audio", "buffer_size", std::to_string(config.audio.bufferFrames));
    text = setConfigValue(text, "recording", "sync_to_disk", config.recording.syncToDisk ? "true" : "false");
    text = setConfigValue(text, "recording", "folder", quoteConfigString(config.recording.folder));
    text = setConfigValue(text, "recording", "midi_timing", midiTimingName(config.recording.midiTiming));
    text = setConfigValue(text, "recording", "latency_correction_ms",
                          std::to_string(config.recording.latencyCorrectionMs));

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return "cannot create folder " + pathToUtf8(path.parent_path()) + ": " + ec.message();
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return "cannot write " + pathToUtf8(path);
    out << text;
    if (!out) return "error while writing " + pathToUtf8(path);
    return {};
}

} // namespace minidaw
