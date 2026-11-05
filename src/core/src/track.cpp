#include "minidaw/track.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/smf.hpp"
#include "minidaw/wav_reader.hpp"

#include <algorithm>
#include <cmath>
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

std::filesystem::path newRenderPath(const std::filesystem::path& folder, const Track& linked)
{
    std::string base = takeFileName(linked);
    base = base.substr(0, base.size() - 4) + "-render";  // without ".wav"
    std::error_code ec;
    for (int n = 1;; ++n) {
        const std::string name = base + (n == 1 ? std::string() : "-" + std::to_string(n)) + ".wav";
        const std::filesystem::path p = folder / pathFromUtf8(name);
        if (!std::filesystem::exists(p, ec)) return p;
    }
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
        error = "Cannot create the recording folder " + pathToUtf8(base) + ": " + ec.message();
        return {};
    }
    for (int i = 1; i < 1000; ++i) {
        const auto path = base / (i == 1 ? name : name + "-" + std::to_string(i));
        if (std::filesystem::create_directory(path, ec)) return path;  // false if it already existed
        if (ec) {
            error = "Cannot create " + pathToUtf8(path) + ": " + ec.message();
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

float trackGain(const Track& track, bool soloActive)
{
    if (track.mute || (soloActive && !track.solo) || track.volumeDb <= kMinVolumeDb) return 0.0f;
    return static_cast<float>(std::pow(10.0, std::min(track.volumeDb, kMaxVolumeDb) / 20.0));
}

bool anySolo(const std::vector<Track>& tracks)
{
    return std::any_of(tracks.begin(), tracks.end(), [](const Track& t) { return t.solo; });
}

const Track* linkedSoundTrack(const std::vector<Track>& tracks, int midiNumber)
{
    for (const Track& t : tracks) {
        if (t.kind == TrackKind::Audio && t.instrumentOf == midiNumber && midiNumber > 0) return &t;
    }
    return nullptr;
}

Track* linkedSoundTrack(std::vector<Track>& tracks, int midiNumber)
{
    return const_cast<Track*>(linkedSoundTrack(static_cast<const std::vector<Track>&>(tracks), midiNumber));
}

const Track* soundOwner(const std::vector<Track>& tracks, const Track& linked)
{
    if (linked.kind != TrackKind::Audio || linked.instrumentOf <= 0) return nullptr;
    for (const Track& t : tracks) {
        if (t.kind == TrackKind::Midi && t.number == linked.instrumentOf) return &t;
    }
    return nullptr;
}

void removeTrack(std::vector<Track>& tracks, std::size_t index)
{
    if (index >= tracks.size()) return;
    const Track& gone = tracks[index];
    if (gone.kind == TrackKind::Midi) {
        if (Track* l = linkedSoundTrack(tracks, gone.number)) l->instrumentOf = 0;
    } else if (gone.instrumentOf > 0) {
        for (Track& t : tracks) {
            if (t.kind == TrackKind::Midi && t.number == gone.instrumentOf) t.soundFromRecording = false;
        }
    }
    tracks.erase(tracks.begin() + static_cast<std::ptrdiff_t>(index));
}

std::vector<Track> tracksToRecord(const std::vector<Track>& tracks, const std::function<bool(int)>& instrumentLoaded)
{
    std::vector<Track> out = tracks;
    for (Track& t : out) {
        if (t.kind == TrackKind::Midi && !t.instrumentClass.empty() && !instrumentLoaded(t.number)) {
            t.instrumentClass.clear();  // its sound would be silence: keep the linked track's take
        }
        if (t.kind == TrackKind::Audio && t.instrumentOf > 0) t.armed = false;
    }
    return out;
}

float playbackGain(const Track& track, const std::vector<Track>& tracks)
{
    const bool solo = anySolo(tracks);
    const Track* owner = soundOwner(tracks, track);
    if (owner == nullptr) return trackGain(track, solo);
    if (!owner->soundFromRecording || owner->mute) return 0.0f;
    Track t = track;
    t.solo = track.solo || owner->solo;
    return trackGain(t, solo);
}

std::uint64_t takeLengthFrames(const Track& track, unsigned int sampleRate)
{
    if (track.take.empty()) return 0;
    std::string err;
    if (track.kind == TrackKind::Audio) {
        WavReader r;
        return r.open(track.take, err) ? r.frames() : 0;
    }
    SmfData d;
    if (!readSmf(track.take, d, err)) return 0;
    std::uint64_t end = 0;
    smfToFrames(d, sampleRate, &end);
    return end;
}

std::vector<std::filesystem::path> listTakeFolders(const std::filesystem::path& base)
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        const std::string name = pathToUtf8(it->path().filename());
        if (name.rfind("take-", 0) == 0) out.push_back(it->path());
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return pathToUtf8(a.filename()) > pathToUtf8(b.filename());
    });
    return out;
}

std::vector<Track> tracksFromTakeFolder(const std::filesystem::path& folder, std::string& error)
{
    std::vector<Track> tracks;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string file = pathToUtf8(it->path().filename());
        const std::string ext = pathToUtf8(it->path().extension());
        const bool wav = ext == ".wav" || ext == ".WAV";
        const bool mid = ext == ".mid" || ext == ".MID";
        if (!wav && !mid) continue;
        // "NN-name.ext"
        std::size_t digits = 0;
        while (digits < file.size() && file[digits] >= '0' && file[digits] <= '9') ++digits;
        if (digits == 0 || digits > 4 || digits >= file.size() || file[digits] != '-') continue;
        Track t;
        t.number = std::stoi(file.substr(0, digits));
        t.name = pathToUtf8(it->path().stem()).substr(digits + 1);
        t.kind = wav ? TrackKind::Audio : TrackKind::Midi;
        t.take = it->path();
        if (wav) {
            WavReader reader;  // the channel count, and whether it is a WAV we can play at all
            std::string err;
            if (!reader.open(it->path(), err)) continue;
            t.channels = reader.channels() >= 2 ? std::vector<unsigned int>{0, 1} : std::vector<unsigned int>{0};
        }
        tracks.push_back(std::move(t));
    }
    std::sort(tracks.begin(), tracks.end(), [](const Track& a, const Track& b) { return a.number < b.number; });
    // Numbers must be unique (a folder edited by hand): renumber clashes.
    for (std::size_t i = 1; i < tracks.size(); ++i) {
        if (tracks[i].number <= tracks[i - 1].number) tracks[i].number = tracks[i - 1].number + 1;
    }
    if (tracks.empty()) {
        error = "No takes (NN-name.wav or .mid) found in " + pathToUtf8(folder) + ".";
    }
    return tracks;
}

} // namespace minidaw
