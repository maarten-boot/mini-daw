#include "minidaw/track.hpp"

#include <algorithm>
#include <cstdio>
#include <system_error>

namespace minidaw {

std::vector<Track> defaultTracks(unsigned int inputChannels)
{
    if (inputChannels == 0) return {};
    Track t;
    t.number = 1;
    t.kind = TrackKind::Audio;
    if (inputChannels >= 2) {
        t.name = "Audio 1+2";
        t.channels = {0, 1};
    } else {
        t.name = "Audio 1";
        t.channels = {0};
    }
    return {t};
}

std::string safeFileName(std::string_view name)
{
    std::string out;
    for (char c : name) {
        const auto u = static_cast<unsigned char>(c);
        const bool bad = u < 0x20 || u == 0x7F || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                         c == '"' || c == '<' || c == '>' || c == '|';
        out += bad ? '_' : c;
    }
    const auto first = out.find_first_not_of(" .");
    if (first == std::string::npos) return "track";
    const auto last = out.find_last_not_of(" .");
    return out.substr(first, last - first + 1);
}

std::string takeFileName(const Track& track)
{
    char prefix[16];
    std::snprintf(prefix, sizeof prefix, "%02d-", track.number);
    return prefix + safeFileName(track.name) + (track.kind == TrackKind::Midi ? ".mid" : ".wav");
}

std::string takeFolderName(const std::tm& t)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "take-%04d%02d%02d-%02d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                  t.tm_hour, t.tm_min, t.tm_sec);
    return buf;
}

std::filesystem::path createTakeFolder(const std::filesystem::path& base, const std::string& name,
                                       std::string& error)
{
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    if (ec) {
        error = "Cannot create the recording folder " + base.string() + ": " + ec.message();
        return {};
    }
    for (int i = 1; i < 1000; ++i) {
        const auto path = base / (i == 1 ? name : name + "-" + std::to_string(i));
        if (std::filesystem::create_directory(path, ec)) return path;  // false if it already existed
        if (ec) {
            error = "Cannot create " + path.string() + ": " + ec.message();
            return {};
        }
    }
    error = "Too many take folders named " + name + ".";
    return {};
}

int nextTrackNumber(const std::vector<Track>& tracks)
{
    int n = 1;
    while (std::any_of(tracks.begin(), tracks.end(), [n](const Track& t) { return t.number == n; })) ++n;
    return n;
}

} // namespace minidaw
