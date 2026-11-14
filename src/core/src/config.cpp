#include "minidaw/config.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/presets.hpp"  // writeFileReplacing

#include <algorithm>
#include <cctype>
#include <cstdio>
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

const std::vector<std::string>& stateWindowIds()
{
    static const std::vector<std::string> ids = {"log",  "audio_settings", "recording_settings", "instruments",
                                                 "tabs", "project_note",   "take_browser",       "custom_contrast"};
    return ids;
}

bool isStateWindowId(std::string_view id)
{
    const auto& ids = stateWindowIds();
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

bool switchAutoRecordState(SessionState& state, const std::string& project, bool on)
{
    if (state.project == project && state.autoRecord == on) return false;
    if (state.project != project) {
        state.position = 0.0;
        state.editors.clear();
        state.project = project;
    }
    state.autoRecord = on;
    return true;
}

SessionState sessionStateAtClose(const SessionState& saved, const SessionNow& now)
{
    const bool same = saved.project == now.project;
    SessionState st;
    st.project = now.project;
    st.autoRecord = now.autoMode || (now.autoRestoreFailed && same);
    if (now.position) {
        st.position = std::clamp(*now.position, 0.0, 1e7);
    } else if (same) {
        st.position = saved.position;  // no stream now: the last real one
    }
    for (const EditorPlace& e : now.editors) {
        if (st.editors.size() >= 16) break;
        if (std::none_of(st.editors.begin(), st.editors.end(), [&](const EditorPlace& o) { return o.track == e.track; }))
            st.editors.push_back(e);
    }
    for (const std::string& id : stateWindowIds()) {  // known ids, each once, in the fixed order
        if (std::find(now.windows.begin(), now.windows.end(), id) != now.windows.end()) st.windows.push_back(id);
    }
    return st;
}

RestorePlan planRestore(const SessionState& state, const std::string& project, bool streamRunning)
{
    RestorePlan plan;
    plan.windows = state.windows;
    if (state.project != project) {
        if (state.autoRecord || state.position > 0.0 || !state.editors.empty()) {
            plan.notRestored = "Not restored: the song position, instrument windows and auto record were of another "
                               "project (" + (state.project.empty() ? std::string("none") : state.project) + ")";
        }
        return plan;
    }
    if (state.position > 0.0) {
        if (streamRunning) {
            plan.position = state.position;
        } else {
            plan.notRestored = "Not restored: the song position (no audio stream)";
        }
    }
    plan.editors = state.editors;
    plan.autoRecord = state.autoRecord;
    return plan;
}

namespace {
// "3:120:80" or "3" -> a place; false if malformed.
bool parseEditorPlace(std::string_view item, EditorPlace& out)
{
    std::vector<long> parts;
    std::size_t start = 0;
    while (start <= item.size()) {
        std::size_t colon = item.find(':', start);
        if (colon == std::string_view::npos) colon = item.size();
        const std::string part(item.substr(start, colon - start));
        char* end = nullptr;
        const long v = std::strtol(part.c_str(), &end, 10);
        if (part.empty() || *end != '\0' || v < -100000 || v > 100000) return false;
        parts.push_back(v);
        if (colon == item.size()) break;
        start = colon + 1;
    }
    if ((parts.size() != 1 && parts.size() != 3) || parts[0] <= 0) return false;
    out.track = static_cast<int>(parts[0]);
    out.hasPlace = parts.size() == 3;
    out.x = out.hasPlace ? static_cast<int>(parts[1]) : 0;
    out.y = out.hasPlace ? static_cast<int>(parts[2]) : 0;
    return true;
}

// "a, b ,c" -> {"a","b","c"} (empty items left out).
std::vector<std::string> splitCommaList(std::string_view text)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t comma = text.find(',', start);
        if (comma == std::string_view::npos) comma = text.size();
        std::string_view item = text.substr(start, comma - start);
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.remove_prefix(1);
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.remove_suffix(1);
        if (!item.empty()) out.emplace_back(item);
        if (comma == text.size()) break;
        start = comma + 1;
    }
    return out;
}
} // namespace

std::vector<std::string> defaultTabOrder() { return {"recording", "timeline", "audio", "terms"}; }

bool isTabId(std::string_view id)
{
    return id == "recording" || id == "timeline" || id == "audio" || id == "terms";
}

std::vector<std::string> normalizeTabOrder(const std::vector<std::string>& order)
{
    std::vector<std::string> out;
    const auto add = [&](const std::string& id) {
        if (isTabId(id) && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    };
    for (const auto& id : order) add(id);
    for (const auto& id : defaultTabOrder()) add(id);
    return out;
}

ConfigLoadResult parseConfig(std::string_view text, std::string_view sourceName)
{
    ConfigLoadResult result;
    std::string section;
    int lineNo = 0;
    int windowSeen = 0;      // bits: x, y, width, height read
    bool windowBad = false;  // one of them was wrong
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
        } else if (section == "tabs" && key == "order") {
            // "recording, timeline, audio, terms"
            std::vector<std::string> ids;
            std::string bad;
            std::size_t at = 0;
            while (at <= value.size()) {
                const std::size_t comma = std::min(value.find(',', at), value.size());
                const std::string id = lower(trim(std::string_view(value).substr(at, comma - at)));
                if (!id.empty()) {
                    if (isTabId(id)) {
                        ids.push_back(id);
                    } else {
                        bad += (bad.empty() ? "" : ", ") + id;
                    }
                }
                at = comma + 1;
            }
            if (!bad.empty()) warn("unknown tab '" + bad + "' in order (known: recording, timeline, audio, terms); left out.");
            cfg.tabs.order = normalizeTabOrder(ids);
        } else if (section == "tabs" && key == "last") {
            const std::string id = lower(trim(value));
            if (isTabId(id)) {
                cfg.tabs.last = id;
            } else {
                warn("unknown tab '" + value + "' for last (known: recording, timeline, audio, terms). Using recording.");
            }
        } else if (section == "window" && key == "remember") {
            if (!parseBool(value, cfg.window.remember)) {
                cfg.window.remember = true;
                warn("remember '" + value + "' must be true or false. Using true.");
            }
        } else if (section == "window" && key == "maximized") {
            if (!parseBool(value, cfg.window.maximized)) {
                cfg.window.maximized = false;
                warn("maximized '" + value + "' must be true or false. Using false.");
            }
        } else if (section == "window" && (key == "x" || key == "y" || key == "width" || key == "height")) {
            char* end = nullptr;
            const long v = std::strtol(value.c_str(), &end, 10);
            const bool size = key == "width" || key == "height";
            const long lo = size ? 100 : -32000;
            const long hi = 32000;
            if (value.empty() || end == value.c_str() || *end != '\0' || v < lo || v > hi) {
                warn(key + " '" + value + "' must be a whole number from " + std::to_string(lo) + " to " +
                     std::to_string(hi) + ". The window gets its default place.");
                windowBad = true;
            } else {
                (key == "x" ? cfg.window.x : key == "y" ? cfg.window.y : key == "width" ? cfg.window.width
                                                                                      : cfg.window.height) =
                    static_cast<int>(v);
                windowSeen |= key == "x" ? 1 : key == "y" ? 2 : key == "width" ? 4 : 8;
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
        } else if (section == "recording" && (key == "auto_pause_seconds" || key == "auto_threshold_db")) {
            const bool pause = key == "auto_pause_seconds";
            const long lo = pause ? 1 : -60;
            const long hi = pause ? 60 : -20;
            char* end = nullptr;
            const long v = std::strtol(value.c_str(), &end, 10);
            if (value.empty() || end == value.c_str() || *end != '\0' || v < lo || v > hi) {
                warn(key + " '" + value + "' must be a whole number from " + std::to_string(lo) + " to " +
                     std::to_string(hi) + ". Using " + (pause ? "5" : "-40") + ".");
            } else {
                (pause ? cfg.recording.autoPauseSeconds : cfg.recording.autoThresholdDb) = static_cast<int>(v);
            }
        } else if (section == "instruments" && key.size() >= 7 && key.compare(0, 6, "folder") == 0 &&
                   key.find_first_not_of("0123456789", 6) == std::string::npos) {
            if (!value.empty() && cfg.instruments.folders.size() < 8) cfg.instruments.folders.push_back(value);
        } else if (section == "state" && key == "project") {
            cfg.state.project = value;
        } else if (section == "state" && key == "auto_record") {
            if (!parseBool(value, cfg.state.autoRecord)) warn("auto_record must be true or false. Using false.");
        } else if (section == "state" && key == "position") {
            char* end = nullptr;
            const double v = std::strtod(value.c_str(), &end);
            if (value.empty() || *end != '\0' || !(v >= 0.0) || v > 1e7) {
                warn("position '" + value + "' must be a number of seconds from 0. Using 0.");
            } else {
                cfg.state.position = v;
            }
        } else if (section == "state" && key == "editors") {
            cfg.state.editors.clear();
            for (const std::string& item : splitCommaList(value)) {
                EditorPlace p;
                if (!parseEditorPlace(item, p)) {
                    warn("editors: '" + item + "' is not track or track:x:y; left out.");
                } else if (cfg.state.editors.size() < 16 &&
                           std::none_of(cfg.state.editors.begin(), cfg.state.editors.end(),
                                        [&](const EditorPlace& e) { return e.track == p.track; })) {
                    cfg.state.editors.push_back(p);
                }
            }
        } else if (section == "state" && key == "windows") {
            cfg.state.windows.clear();
            for (const std::string& id : splitCommaList(value)) {
                const std::string l = lower(id);
                if (!isStateWindowId(l)) {
                    warn("windows: unknown window '" + id + "'; left out.");
                } else if (std::find(cfg.state.windows.begin(), cfg.state.windows.end(), l) == cfg.state.windows.end()) {
                    cfg.state.windows.push_back(l);
                }
            }
        } else if (section == "project" && key == "last") {
            cfg.project.last = value;
        } else if (section == "project" && key.size() >= 7 && key.compare(0, 6, "recent") == 0 &&
                   key.find_first_not_of("0123456789", 6) == std::string::npos) {
            if (!value.empty() && cfg.project.recent.size() < kMaxRecentProjects) cfg.project.recent.push_back(value);
        } else {
            warn("unknown setting '" + key + "' in section [" + section + "]");
        }
    }
    result.config.window.hasGeometry = windowSeen == 15 && !windowBad;
    return result;
}

std::optional<ScreenRect> restoredWindowRect(const WindowSettings& saved, const std::vector<ScreenRect>& workAreas)
{
    if (!saved.remember || !saved.hasGeometry || saved.width <= 0 || saved.height <= 0) return std::nullopt;
    const ScreenRect* best = nullptr;
    long long bestArea = 0;
    for (const ScreenRect& a : workAreas) {
        const long long w = std::min<long long>(saved.x + saved.width, a.x + a.width) - std::max(saved.x, a.x);
        const long long h = std::min<long long>(saved.y + saved.height, a.y + a.height) - std::max(saved.y, a.y);
        if (w >= 100 && h >= 50 && w * h > bestArea) {
            best = &a;
            bestArea = w * h;
        }
    }
    if (best == nullptr) return std::nullopt;
    ScreenRect r{saved.x, saved.y, std::min(saved.width, best->width), std::min(saved.height, best->height)};
    r.x = std::clamp(r.x, best->x, best->x + best->width - r.width);
    r.y = std::clamp(r.y, best->y, best->y + best->height - r.height);
    return r;
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
           "[window]\n"
           "; Open the window at the size and place it had when mini-daw closed\n"
           "; (x, y, width, height and maximized are written by mini-daw).\n"
           "remember = true\n"
           "\n"
           "[tabs]\n"
           "; The order of the main window's tabs (also set by dragging a tab, or in\n"
           "; Settings > Tabs...): recording, timeline, audio (Audio setup), terms.\n"
           "order = recording, timeline, audio, terms\n"
           "; The tab shown at start: the one used last (written by mini-daw).\n"
           "last = recording\n"
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
           "latency_correction_ms = 0\n"
           "; Auto record: a part ends after this many seconds without playing (1 to 60),\n"
           "; and an audio input counts as playing from this level in dBFS (-60 to -20).\n"
           "auto_pause_seconds = 5\n"
           "auto_threshold_db = -40\n"
           "\n"
           "[project]\n"
           "; The project open when mini-daw closed; it is opened again at the next start.\n"
           "; recent1 .. recent8 (written by mini-daw) are the File > Recent projects list.\n"
           "last = \"\"\n"
           "\n"
           "[state]\n"
           "; Written by mini-daw: what was going on when it closed, restored at the next\n"
           "; start. auto_record (saved at once when switched, so also after a crash), the\n"
           "; song position in seconds and the instrument windows (track:x:y) come back\n"
           "; only with the same project; windows lists mini-daw's own open windows.\n"
           "project = \"\"\n"
           "auto_record = false\n"
           "position = 0\n"
           "editors =\n"
           "windows =\n";
}

void rememberProject(ProjectSettings& settings, const std::string& folder)
{
    auto& r = settings.recent;
    r.erase(std::remove(r.begin(), r.end(), folder), r.end());
    r.insert(r.begin(), folder);
    if (r.size() > kMaxRecentProjects) r.resize(kMaxRecentProjects);
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
    text = setConfigValue(text, "window", "remember", config.window.remember ? "true" : "false");
    {
        std::string order;
        for (const auto& id : normalizeTabOrder(config.tabs.order)) order += (order.empty() ? "" : ", ") + id;
        text = setConfigValue(text, "tabs", "order", order);
        text = setConfigValue(text, "tabs", "last", isTabId(config.tabs.last) ? config.tabs.last : "recording");
    }
    if (config.window.hasGeometry) {
        text = setConfigValue(text, "window", "x", std::to_string(config.window.x));
        text = setConfigValue(text, "window", "y", std::to_string(config.window.y));
        text = setConfigValue(text, "window", "width", std::to_string(config.window.width));
        text = setConfigValue(text, "window", "height", std::to_string(config.window.height));
        text = setConfigValue(text, "window", "maximized", config.window.maximized ? "true" : "false");
    }
    text = setConfigValue(text, "audio", "input_device", deviceValue(config.audio.inputDevice));
    text = setConfigValue(text, "audio", "output_device", deviceValue(config.audio.outputDevice));
    text = setConfigValue(text, "audio", "sample_rate", std::to_string(config.audio.sampleRate));
    text = setConfigValue(text, "audio", "buffer_size", std::to_string(config.audio.bufferFrames));
    text = setConfigValue(text, "recording", "sync_to_disk", config.recording.syncToDisk ? "true" : "false");
    text = setConfigValue(text, "recording", "folder", quoteConfigString(config.recording.folder));
    text = setConfigValue(text, "recording", "midi_timing", midiTimingName(config.recording.midiTiming));
    text = setConfigValue(text, "recording", "latency_correction_ms",
                          std::to_string(config.recording.latencyCorrectionMs));
    text = setConfigValue(text, "recording", "auto_pause_seconds", std::to_string(config.recording.autoPauseSeconds));
    text = setConfigValue(text, "recording", "auto_threshold_db", std::to_string(config.recording.autoThresholdDb));
    text = setConfigValue(text, "project", "last", quoteConfigString(config.project.last));
    for (std::size_t i = 0; i < 8; ++i) {
        const std::string v = i < config.instruments.folders.size() ? config.instruments.folders[i] : std::string();
        text = setConfigValue(text, "instruments", "folder" + std::to_string(i + 1), quoteConfigString(v));
    }
    for (std::size_t i = 0; i < kMaxRecentProjects; ++i) {
        const std::string v = i < config.project.recent.size() ? config.project.recent[i] : std::string();
        text = setConfigValue(text, "project", "recent" + std::to_string(i + 1), quoteConfigString(v));
    }

    {
        const SessionState& st = config.state;
        text = setConfigValue(text, "state", "project", quoteConfigString(st.project));
        text = setConfigValue(text, "state", "auto_record", st.autoRecord ? "true" : "false");
        char pos[32];
        std::snprintf(pos, sizeof pos, "%.3f", st.position);
        text = setConfigValue(text, "state", "position", pos);
        std::string editors;
        for (const EditorPlace& e : st.editors) {
            editors += (editors.empty() ? "" : ", ") + std::to_string(e.track);
            if (e.hasPlace) editors += ":" + std::to_string(e.x) + ":" + std::to_string(e.y);
        }
        text = setConfigValue(text, "state", "editors", editors);
        std::string windows;
        for (const std::string& w : st.windows) windows += (windows.empty() ? "" : ", ") + w;
        text = setConfigValue(text, "state", "windows", windows);
    }

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return "cannot create folder " + pathToUtf8(path.parent_path()) + ": " + ec.message();
    }
    // Written beside it and renamed over it: the config is now saved while
    // recording (auto record on / off, P26), so a crash or power loss leaves
    // the old or the new file, never half of one.
    std::string err;
    if (!writeFileReplacing(path, text, err, true)) return err;  // forced to the disk: also after a power loss
    return {};
}

} // namespace minidaw
