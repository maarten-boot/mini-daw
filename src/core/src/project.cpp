#include "minidaw/project.hpp"

#include "file_util.hpp"
#include "minidaw/path_util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace minidaw {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path normalFolder(const fs::path& folder)
{
    std::error_code ec;
    fs::path abs = fs::absolute(folder, ec);
    if (ec) abs = folder;
    return abs.lexically_normal();
}

// Take path as stored: relative ('/' separators) inside the folder, else absolute.
std::string storedPath(const fs::path& take, const fs::path& folder)
{
    if (take.empty()) return {};
    const fs::path base = normalFolder(folder);
    const fs::path abs = normalFolder(take);
    const fs::path rel = abs.lexically_relative(base);
    if (!rel.empty() && *rel.begin() != ".." && rel != ".") {
        std::string out = pathToUtf8(rel);
        std::replace(out.begin(), out.end(), '\\', '/');
        return out;
    }
    return pathToUtf8(abs);
}

fs::path loadedPath(const std::string& stored, const fs::path& folder)
{
    if (stored.empty()) return {};
    const fs::path p = pathFromUtf8(stored);
    return (p.is_absolute() ? p : normalFolder(folder) / p).lexically_normal();
}

} // namespace

std::string base64Encode(const std::string& bytes)
{
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const unsigned v = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                           static_cast<unsigned char>(bytes[i + 2]);
        out += abc[(v >> 18) & 63];
        out += abc[(v >> 12) & 63];
        out += abc[(v >> 6) & 63];
        out += abc[v & 63];
    }
    if (i + 1 == bytes.size()) {
        const unsigned v = static_cast<unsigned char>(bytes[i]) << 16;
        out += abc[(v >> 18) & 63];
        out += abc[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const unsigned v = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8);
        out += abc[(v >> 18) & 63];
        out += abc[(v >> 12) & 63];
        out += abc[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

bool base64Decode(const std::string& text, std::string& bytes)
{
    bytes.clear();
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    if (text.size() % 4 != 0) return false;
    for (std::size_t i = 0; i < text.size(); i += 4) {
        int v[4];
        for (int k = 0; k < 4; ++k) {
            const char c = text[i + static_cast<std::size_t>(k)];
            v[k] = c == '=' ? (i + 4 == text.size() && k >= 2 ? -2 : -1) : val(c);
            if (v[k] == -1) return false;
        }
        if (v[2] == -2 && v[3] != -2) return false;
        const unsigned n = (static_cast<unsigned>(v[0]) << 18) | (static_cast<unsigned>(v[1]) << 12) |
                           (static_cast<unsigned>(v[2] < 0 ? 0 : v[2]) << 6) | static_cast<unsigned>(v[3] < 0 ? 0 : v[3]);
        bytes += static_cast<char>((n >> 16) & 0xFF);
        if (v[2] >= 0) bytes += static_cast<char>((n >> 8) & 0xFF);
        if (v[3] >= 0) bytes += static_cast<char>(n & 0xFF);
    }
    return true;
}

bool isInsideFolder(const fs::path& file, const fs::path& folder)
{
    const fs::path rel = normalFolder(file).lexically_relative(normalFolder(folder));
    return !rel.empty() && *rel.begin() != ".." && rel != ".";  // the folder itself is not inside (N8)
}

std::string projectToJson(const ProjectData& data, const fs::path& folder)
{
    json tracks = json::array();
    for (const Track& t : data.tracks) {
        json j;
        j["number"] = t.number;
        j["name"] = t.name;
        j["kind"] = t.kind == TrackKind::Audio ? "audio" : "midi";
        if (t.kind == TrackKind::Audio) {
            j["inputChannels"] = t.channels;
            j["monitor"] = t.monitor;
            if (t.instrumentOf > 0) j["instrumentOf"] = t.instrumentOf;  // P11: the sound of that MIDI track
        } else {
            j["midiInput"] = t.midiPort;
            j["midiChannel"] = t.midiChannel;
            j["midiOutput"] = t.midiOut;
            if (!t.instrumentClass.empty()) {
                j["instrument"] = {{"classId", t.instrumentClass},
                                   {"module", t.instrumentModule},
                                   {"name", t.instrumentName},
                                   {"state", base64Encode(t.instrumentState)},
                                   {"controllerState", base64Encode(t.instrumentControllerState)}};
                if (!t.instrumentPreset.empty()) j["instrument"]["preset"] = t.instrumentPreset;  // PL2
            }
            if (t.soundFromRecording) j["soundFrom"] = "recording";  // P11; default "instrument"
        }
        j["armed"] = t.armed;
        j["volumeDb"] = t.volumeDb;
        j["mute"] = t.mute;
        j["solo"] = t.solo;
        j["take"] = storedPath(t.take, folder);
        j["takeStart"] = t.takeStart;
        tracks.push_back(std::move(j));
    }
    json root;
    root["format"] = "mini-daw project";
    root["version"] = kProjectFormatVersion;
    root["sampleRate"] = data.sampleRate;
    root["tracks"] = std::move(tracks);
    // Bytes that are not valid UTF-8 (an odd driver name, a hand-edited file)
    // become U+FFFD instead of throwing (crosscheck #7, N2).
    return root.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

namespace {

bool parseProject(const std::string& text, const fs::path& folder, ProjectData& data,
                  std::vector<std::string>& warnings, std::string& error)
{
    data = {};
    const json root = json::parse(text, nullptr, false);  // no exceptions
    if (root.is_discarded() || !root.is_object()) {
        error = "The project file is not valid JSON.";
        return false;
    }
    // Types checked before use: json::value() throws on a wrong type (N1).
    if (!root.contains("format") || !root["format"].is_string() ||
        root["format"].get<std::string>() != "mini-daw project") {
        error = "This is not a mini-daw project file.";
        return false;
    }
    if (!root.contains("version") || !root["version"].is_number_integer()) {
        error = "The project file has no valid format version.";
        return false;
    }
    const auto version = root["version"].get<std::int64_t>();
    if (version < 1 || version > kProjectFormatVersion) {
        error = "The project file has format version " + std::to_string(version) + "; this mini-daw reads version " +
                std::to_string(kProjectFormatVersion) + " (it may be from a newer mini-daw).";
        return false;
    }
    if (root.contains("sampleRate") && root["sampleRate"].is_number_unsigned()) {
        data.sampleRate = root["sampleRate"].get<unsigned int>();
    }
    if (!root.contains("tracks") || !root["tracks"].is_array()) {
        error = "The project file has no track list.";
        return false;
    }
    std::vector<int> numbers;
    std::size_t index = 0;
    for (const json& j : root["tracks"]) {
        ++index;
        const std::string where = "Track " + std::to_string(index) + ": ";
        if (!j.is_object()) {
            warnings.push_back(where + "not an object; skipped.");
            continue;
        }
        // Each field on its own, so one bad value does not lose the track.
        auto get = [&](const char* key, auto fallback) {
            using T = decltype(fallback);
            if (!j.contains(key)) return fallback;
            try {
                return j.at(key).get<T>();
            } catch (const json::exception&) {
                warnings.push_back(where + "'" + key + "' has a wrong value; using the default.");
                return fallback;
            }
        };
        Track t;
        t.number = get("number", static_cast<int>(index));
        t.name = get("name", std::string("Track ") + std::to_string(index));
        const std::string kind = get("kind", std::string("audio"));
        if (kind != "audio" && kind != "midi") {
            warnings.push_back(where + "unknown kind '" + kind + "'; skipped.");
            continue;
        }
        t.kind = kind == "audio" ? TrackKind::Audio : TrackKind::Midi;
        if (t.kind == TrackKind::Audio) {
            // 1 or 2 whole numbers 0..63 (a negative number would wrap to a huge one: N7).
            t.channels = {0};
            if (j.contains("inputChannels")) {
                const json& c = j["inputChannels"];
                bool ok = c.is_array() && !c.empty() && c.size() <= 2;
                std::vector<unsigned int> list;
                for (const json& v : c.is_array() ? c : json::array()) {
                    if (!v.is_number_unsigned() || v.get<std::uint64_t>() >= 64) ok = false;
                    else list.push_back(v.get<unsigned int>());
                }
                if (ok) t.channels = list;
                else warnings.push_back(where + "'inputChannels' must be 1 or 2 channel numbers from 0 to 63; using input 1.");
            }
            t.monitor = get("monitor", false);
            t.instrumentOf = get("instrumentOf", 0);
            if (t.instrumentOf < 0) {
                warnings.push_back(where + "'instrumentOf' must be a track number; ignored.");
                t.instrumentOf = 0;
            }
        } else {
            t.midiPort = get("midiInput", std::string());
            if (j.contains("midiChannel")) {
                const json& c = j["midiChannel"];
                if (c.is_number_integer() && c.get<std::int64_t>() >= 0 && c.get<std::int64_t>() <= 16) {
                    t.midiChannel = c.get<int>();
                } else {
                    warnings.push_back(where + "'midiChannel' must be 0 (all) to 16; using all channels.");
                }
            }
            t.midiOut = get("midiOutput", std::string());
            const std::string from = get("soundFrom", std::string("instrument"));
            if (from != "instrument" && from != "recording") {
                warnings.push_back(where + "'soundFrom' must be \"instrument\" or \"recording\"; using instrument.");
            }
            t.soundFromRecording = from == "recording";
            if (j.contains("instrument")) {
                const json& ins = j["instrument"];
                bool ok = ins.is_object() && ins.contains("classId") && ins["classId"].is_string();
                // The other fields are optional strings; a wrong type is a damaged field (Q8).
                auto field = [&](const char* key, bool& good) {
                    if (!ins.contains(key)) return std::string();
                    if (!ins[key].is_string()) {
                        good = false;
                        return std::string();
                    }
                    return ins[key].get<std::string>();
                };
                if (ok) {
                    bool names = true;
                    t.instrumentClass = ins["classId"].get<std::string>();
                    t.instrumentModule = field("module", names);
                    t.instrumentName = field("name", names);
                    if (!names) warnings.push_back(where + "the instrument's module or name has a wrong value; ignored.");
                    bool preset = true;  // PL2; optional, only for display (crosscheck #16, X13)
                    t.instrumentPreset = field("preset", preset);
                    if (!preset) warnings.push_back(where + "the instrument's preset name has a wrong value; ignored.");
                    bool states = true;
                    const std::string state = field("state", states);
                    const std::string controllerState = field("controllerState", states);
                    ok = states && base64Decode(state, t.instrumentState) &&
                         base64Decode(controllerState, t.instrumentControllerState);
                    if (!ok) {
                        t.instrumentState.clear();
                        t.instrumentControllerState.clear();
                        warnings.push_back(where + "the saved instrument settings are damaged; the instrument starts "
                                                   "with its defaults.");
                    }
                } else {
                    warnings.push_back(where + "'instrument' is damaged; no instrument.");
                }
            }
        }
        t.armed = get("armed", false);
        t.volumeDb = std::clamp(get("volumeDb", 0.0f), kMinVolumeDb, kMaxVolumeDb);
        t.mute = get("mute", false);
        t.solo = get("solo", false);
        t.take = loadedPath(get("take", std::string()), folder);
        t.takeStart = get("takeStart", static_cast<std::int64_t>(0));
        if (t.number < 1 || std::find(numbers.begin(), numbers.end(), t.number) != numbers.end()) {
            const int fixed = numbers.empty() ? 1 : *std::max_element(numbers.begin(), numbers.end()) + 1;
            warnings.push_back(where + "number " + std::to_string(t.number) + " is not unique; using " +
                               std::to_string(fixed) + ".");
            t.number = fixed;
        }
        numbers.push_back(t.number);
        std::error_code ec;
        if (!t.take.empty() && !fs::exists(t.take, ec)) {
            warnings.push_back(where + "its take " + pathToUtf8(t.take) + " is missing.");
        }
        data.tracks.push_back(std::move(t));
    }
    // Links between a MIDI track and the audio track with its sound (P11):
    // the MIDI track must exist, and have at most one linked track.
    for (Track& t : data.tracks) {
        if (t.kind != TrackKind::Audio || t.instrumentOf == 0) continue;
        const Track* owner = soundOwner(data.tracks, t);
        const Track* first = linkedSoundTrack(data.tracks, t.instrumentOf);
        if (owner != nullptr && first == &t) {
            t.armed = false;  // records with its MIDI track (R9)
            t.monitor = false;
        }
        if (owner == nullptr || first != &t) {
            warnings.push_back("Track " + std::to_string(t.number) + ": it held the sound of track " +
                               std::to_string(t.instrumentOf) + (owner ? ", which already has one" : ", which is missing") +
                               "; it is a normal audio track now.");
            t.instrumentOf = 0;
        }
    }
    for (Track& t : data.tracks) {
        if (t.kind == TrackKind::Midi && t.soundFromRecording && linkedSoundTrack(data.tracks, t.number) == nullptr) {
            t.soundFromRecording = false;  // nothing recorded to play
        }
    }
    return true;
}

} // namespace

bool projectFromJson(const std::string& text, const fs::path& folder, ProjectData& data,
                     std::vector<std::string>& warnings, std::string& error)
{
    // Never throws (the last project is opened at start: a damaged file must
    // give an error, not a crash at every start; crosscheck #7, N1).
    try {
        return parseProject(text, folder, data, warnings, error);
    } catch (const std::exception& e) {
        data = {};
        error = std::string("The project file is damaged (") + e.what() + ").";
        return false;
    }
}

bool isProjectFolder(const fs::path& folder)
{
    std::error_code ec;
    return fs::is_regular_file(folder / kProjectFileName, ec);
}

bool createProjectFolder(const fs::path& folder, std::string& error)
{
    std::error_code ec;
    if (isProjectFolder(folder)) {
        error = pathToUtf8(folder) + " already holds a project; open it instead.";
        return false;
    }
    if (fs::exists(folder, ec) && !fs::is_directory(folder, ec)) {
        error = pathToUtf8(folder) + " is a file, not a folder.";
        return false;
    }
    fs::create_directories(folder / "takes", ec);
    if (ec) {
        error = "Could not create " + pathToUtf8(folder) + ": " + ec.message();
        return false;
    }
    return true;
}

bool saveProject(const fs::path& folder, const ProjectData& data, std::string& error)
{
    const std::string text = projectToJson(data, folder);
    const fs::path file = folder / kProjectFileName;
    const fs::path tmp = folder / (std::string(kProjectFileName) + ".tmp");
    std::FILE* f = detail::openForWriting(tmp, false);
    if (!f) {
        error = "Could not write " + pathToUtf8(tmp) + ".";
        return false;
    }
    const bool written = std::fwrite(text.data(), 1, text.size(), f) == text.size() && detail::syncToDisk(f);
    std::fclose(f);
    std::error_code ec;
    if (!written) {
        fs::remove(tmp, ec);
        error = "Could not write " + pathToUtf8(tmp) + " (disk full?).";
        return false;
    }
    fs::rename(tmp, file, ec);  // replaces the old file in one step (also on Windows)
    if (ec) {
        error = "Could not replace " + pathToUtf8(file) + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool loadProject(const fs::path& folder, ProjectData& data, std::vector<std::string>& warnings, std::string& error)
{
    const fs::path file = folder / kProjectFileName;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "There is no project in " + pathToUtf8(folder) + ".";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (!projectFromJson(ss.str(), folder, data, warnings, error)) {
        error = pathToUtf8(file) + ": " + error;
        return false;
    }
    return true;
}

bool sameFileContent(const fs::path& a, const fs::path& b)
{
    std::error_code ec;
    const auto size = fs::file_size(a, ec);
    if (ec || fs::file_size(b, ec) != size || ec) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    if (!fa || !fb) return false;
    std::vector<char> ba(1 << 16), bb(1 << 16);
    while (fa && fb) {
        fa.read(ba.data(), static_cast<std::streamsize>(ba.size()));
        fb.read(bb.data(), static_cast<std::streamsize>(bb.size()));
        if (fa.gcount() != fb.gcount() || !std::equal(ba.begin(), ba.begin() + fa.gcount(), bb.begin())) return false;
    }
    return fa.eof() && fb.eof();
}

ImportResult importTakes(const fs::path& folder, std::vector<Track>& tracks)
{
    ImportResult result;
    for (Track& t : tracks) {
        if (t.take.empty() || isInsideFolder(t.take, folder)) continue;
        const std::string who = "Track " + std::to_string(t.number) + ": ";
        std::error_code ec;
        if (!fs::is_regular_file(t.take, ec)) {
            result.missing.push_back(who + "its take " + pathToUtf8(t.take) +
                                     " is missing and could not be copied into the project.");
            continue;
        }
        const fs::path dir = folder / "takes" / "imported" / t.take.parent_path().filename();
        fs::create_directories(dir, ec);
        // The first name that is free, or holds exactly this take already.
        fs::path dest;
        bool reuse = false;
        for (int n = 1; n < 1000 && dest.empty(); ++n) {
            const fs::path candidate =
                n == 1 ? dir / t.take.filename()
                       : dir / (t.take.stem().native() + fs::path("-" + std::to_string(n)).native() +
                                t.take.extension().native());
            if (!fs::exists(candidate, ec)) {
                dest = candidate;
            } else if (sameFileContent(candidate, t.take)) {
                dest = candidate;
                reuse = true;
            }
        }
        if (dest.empty()) {
            result.failed.push_back(who + "no free file name for " + pathToUtf8(t.take.filename()) + " in " +
                                    pathToUtf8(dir) + ".");
            continue;
        }
        if (!reuse) {
            fs::copy_file(t.take, dest, fs::copy_options::none, ec);  // never overwrites
            if (ec) {
                result.failed.push_back(who + pathToUtf8(t.take) + " could not be copied into the project: " +
                                        ec.message());
                std::error_code rm;
                fs::remove(dest, rm);  // only the file this call started (the name was free)
                continue;
            }
            result.copied.push_back(pathToUtf8(t.take) + " -> " + pathToUtf8(dest));
        }
        t.take = dest;
    }
    return result;
}

std::vector<fs::path> listProjects(const fs::path& base)
{
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec) && isProjectFolder(it->path())) out.push_back(it->path());
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return pathToUtf8(a.filename()) < pathToUtf8(b.filename());
    });
    return out;
}

} // namespace minidaw
