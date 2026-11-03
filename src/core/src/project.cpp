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
        } else {
            j["midiInput"] = t.midiPort;
            j["midiChannel"] = t.midiChannel;
            j["midiOutput"] = t.midiOut;
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
