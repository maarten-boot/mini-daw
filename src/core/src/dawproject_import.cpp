// DAWproject import (P28, SPEC-PRJ-05): see dawproject.hpp.

#include "minidaw/dawproject.hpp"

#include "minidaw/path_util.hpp"
#include "minidaw/presets.hpp"
#include "minidaw/project.hpp"
#include "minidaw/recording_notes.hpp"
#include "minidaw/sinc_kernel.hpp"
#include "minidaw/smf.hpp"
#include "minidaw/track.hpp"
#include "minidaw/wav.hpp"
#include "minidaw/wav_reader.hpp"

#include <miniz.h>
#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace fs = std::filesystem;

namespace minidaw {

std::string vst3ClassFromDeviceId(const std::string& deviceId)
{
    std::string out;
    for (char c : deviceId) {
        if (c == '-' || c == '{' || c == '}' || c == ' ') continue;
        if (!std::isxdigit(static_cast<unsigned char>(c))) return {};
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out.size() == 32 ? out : std::string();
}

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

std::FILE* openRead(const fs::path& p)
{
#if defined(_WIN32)
    return _wfopen(p.c_str(), L"rb");
#else
    return std::fopen(p.c_str(), "rb");
#endif
}

std::FILE* openWrite(const fs::path& p)
{
#if defined(_WIN32)
    return _wfopen(p.c_str(), L"wb");
#else
    return std::fopen(p.c_str(), "wb");
#endif
}

// A ZIP entry name as written in the XML ("./audio/x.wav", "audio\\x.wav" → "audio/x.wav").
std::string zipEntryName(std::string name)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    while (name.rfind("./", 0) == 0) name.erase(0, 2);
    while (!name.empty() && name.front() == '/') name.erase(0, 1);
    return name;
}

class ZipReader {
public:
    ~ZipReader()
    {
        if (open_) mz_zip_reader_end(&zip_);
        if (file_) std::fclose(file_);
    }
    bool open(const fs::path& path, std::string& error)
    {
        file_ = openRead(path);
        if (!file_) {
            error = "cannot read " + pathToUtf8(path) + ": " + std::strerror(errno);
            return false;
        }
        std::memset(&zip_, 0, sizeof zip_);
        if (!mz_zip_reader_init_cfile(&zip_, file_, 0, 0)) {
            error = "not a DAWproject file: it is not a ZIP archive (" +
                    std::string(mz_zip_get_error_string(mz_zip_get_last_error(&zip_))) + ")";
            return false;
        }
        open_ = true;
        return true;
    }
    int find(const std::string& name) { return mz_zip_reader_locate_file(&zip_, zipEntryName(name).c_str(), nullptr, 0); }
    bool read(const std::string& name, std::string& out, std::string& error)
    {
        const int i = find(name);
        if (i < 0) {
            error = name + " is not in the file";
            return false;
        }
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip_, static_cast<mz_uint>(i), &size, 0);
        if (!data) {
            error = name + " could not be unpacked (" + mz_zip_get_error_string(mz_zip_get_last_error(&zip_)) + ")";
            return false;
        }
        out.assign(static_cast<const char*>(data), size);
        mz_free(data);
        return true;
    }
    bool extract(const std::string& name, const fs::path& to, std::string& error)
    {
        const int i = find(name);
        if (i < 0) {
            error = name + " is not in the file";
            return false;
        }
        std::FILE* f = openWrite(to);
        if (!f) {
            error = "cannot write " + pathToUtf8(to) + ": " + std::strerror(errno);
            return false;
        }
        const bool ok = mz_zip_reader_extract_to_cfile(&zip_, static_cast<mz_uint>(i), f, 0);
        const bool closed = std::fclose(f) == 0;
        if (!ok || !closed) {
            error = name + " could not be unpacked" +
                    (ok ? std::string(" (disk full?)") : " (" + std::string(mz_zip_get_error_string(mz_zip_get_last_error(&zip_))) + ")");
            return false;
        }
        return true;
    }

private:
    std::FILE* file_ = nullptr;
    mz_zip_archive zip_{};
    bool open_ = false;
};

enum class TimeUnit { Beats, Seconds };

struct Times {
    double secondsPerBeat = 0.5;  // 120 bpm
    double sec(double v, TimeUnit u) const { return u == TimeUnit::Beats ? v * secondsPerBeat : v; }
};

TimeUnit unitOf(pugi::xml_node n, TimeUnit inherited, const char* attrName = "timeUnit")
{
    const std::string u = n.attribute(attrName).value();
    if (u == "seconds") return TimeUnit::Seconds;
    if (u == "beats") return TimeUnit::Beats;
    return inherited;
}

std::optional<double> number(pugi::xml_node n, const char* name)
{
    const pugi::xml_attribute a = n.attribute(name);
    if (!a) return std::nullopt;
    const double v = a.as_double(std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(v)) return std::nullopt;
    return v;
}

// Where a clip puts its content on the arrangement (all in seconds).
struct ClipMap {
    double time = 0.0;       // T: start on the arrangement
    double duration = kInf;  // D
    double playStart = 0.0;  // P: content time at T
    std::optional<double> playStop;
    bool loop = false;
    double loopStart = 0.0;
    double loopEnd = 0.0;

    // The content time heard `u` seconds into the clip; nullopt after playStop.
    std::optional<double> contentAt(double u) const
    {
        if (!loop) {
            const double c = playStart + u;
            if (playStop && c >= *playStop) return std::nullopt;
            return c;
        }
        const double first = loopEnd - playStart;
        if (u < first) return playStart + u;
        return loopStart + std::fmod(u - first, loopEnd - loopStart);
    }
    // Calls f(contentFrom, contentTo, arrangementTimeOfContentFrom) for each pass over the content.
    template <class F>
    void forEachPass(F f) const
    {
        if (!loop) {
            f(playStart, playStop.value_or(kInf), time);
            return;
        }
        f(playStart, loopEnd, time);
        const double len = loopEnd - loopStart;
        double u = loopEnd - playStart;
        for (int pass = 0; u < duration && pass < 100000; ++pass, u += len) f(loopStart, loopEnd, time + u);
    }
};

struct WarpPoint {
    double time = 0.0;     // clip content seconds
    double content = 0.0;  // audio seconds
};

double warpAt(const std::vector<WarpPoint>& w, double t)
{
    if (w.empty()) return t;
    if (w.size() == 1) return w[0].content + (t - w[0].time);
    std::size_t i = 1;
    while (i + 1 < w.size() && t > w[i].time) ++i;
    const WarpPoint& a = w[i - 1];
    const WarpPoint& b = w[i];
    const double span = b.time - a.time;
    const double slope = span > 0.0 ? (b.content - a.content) / span : 1.0;
    return a.content + (t - a.time) * slope;
}

struct AudioPiece {
    ClipMap map;
    double fadeIn = 0.0;
    double fadeOut = 0.0;
    std::vector<WarpPoint> warps;  // empty: content seconds = audio seconds
    std::string path;              // ZIP entry, or a file (external)
    bool external = false;
};

struct NoteOut {
    double time = 0.0;  // arrangement seconds
    double duration = 0.0;
    int channel = 0;
    int key = 60;
    double vel = 100.0 / 127.0;
    double rel = 64.0 / 127.0;
};

struct LaneContent {
    std::string name;            // "" = the track's own
    std::vector<AudioPiece> audio;
    std::vector<NoteOut> notes;
    int noteClips = 0;
    double notesFrom = kInf;     // where the first note clip starts (the MIDI take's place)
    double notesTo = 0.0;        // where the last one ends
};

struct TrackDef {
    std::string id;
    std::string name;
    bool notesType = false;
    bool audioType = false;
    bool skip = false;           // master, effect return
    float volumeDb = 0.0f;
    bool mute = false;
    bool solo = false;
    std::string instrumentClass;
    std::string instrumentName;
    std::string statePath;
    bool stateExternal = false;
    std::vector<LaneContent> lanes;  // [0] = the track's own
    int automation = 0;

    LaneContent& lane(const std::string& laneName)
    {
        for (auto& l : lanes) {
            if (l.name == laneName) return l;
        }
        LaneContent l;
        l.name = laneName;
        lanes.push_back(std::move(l));
        return lanes.back();
    }
};

std::string label(const TrackDef& t) { return "Track \"" + t.name + "\""; }

class Reader {
public:
    Reader(DawImportResult& result, const Times& times) : result_(result), times_(times) {}

    std::vector<TrackDef> tracks;
    std::vector<std::pair<double, std::string>> markers;  // seconds, name

    void readStructure(pugi::xml_node structure)
    {
        for (pugi::xml_node n : structure.children()) {
            const std::string kind = n.name();
            if (kind == "Track") {
                readTrack(n);
            } else if (kind == "Channel") {
                const std::string role = n.attribute("role").value();
                if (role != "master") note("A mixer channel \"" + std::string(n.attribute("name").value()) + "\" (" +
                                           (role.empty() ? std::string("regular") : role) + ")");
            }
        }
    }

    void readArrangement(pugi::xml_node arrangement)
    {
        if (!arrangement) return;
        const pugi::xml_node lanes = arrangement.child("Lanes");
        const TimeUnit unit = unitOf(lanes, TimeUnit::Beats);
        walk(lanes, unit, -1, "");
        for (pugi::xml_node m : arrangement.children("Markers")) readMarkers(m, unitOf(m, unit));
        const pugi::xml_node tempo = arrangement.child("TempoAutomation");
        if (tempo && tempo.first_child()) {
            note("Tempo changes (the file's tempo " + std::to_string(static_cast<int>(std::lround(60.0 / times_.secondsPerBeat))) +
                 " bpm is used throughout)");
        }
        for (const auto& t : tracks) {
            if (t.automation > 0) note(label(t) + ": its automation (" + std::to_string(t.automation) + " lane(s))");
        }
        if (unknownClipTracks_ > 0) note(std::to_string(unknownClipTracks_) + " clip(s) or lane(s) on no track");
        if (automationNoTrack_ > 0) note("Automation of the project (" + std::to_string(automationNoTrack_) + " lane(s))");
    }

private:
    void note(const std::string& s) { result_.leftOut.push_back(s); }

    void readTrack(pugi::xml_node n)
    {
        const std::string name = n.attribute("name").value();
        const bool group = static_cast<bool>(n.child("Track"));
        pugi::xml_node ch = n.child("Channel");
        const std::string role = ch.attribute("role").value();
        TrackDef t;
        t.id = n.attribute("id").value();
        t.name = name.empty() ? "Track " + std::to_string(tracks.size() + 1) : name;
        const std::string content = n.attribute("contentType").value();
        t.notesType = content.find("notes") != std::string::npos;
        t.audioType = content.find("audio") != std::string::npos;
        if (role == "master") {
            t.skip = true;  // its lanes (rare) are read but not used
        } else if (role == "effect") {
            t.skip = true;
            note(label(t) + ": an effect return track");
        } else if (group) {
            t.skip = true;
            note(label(t) + ": a group; its tracks are imported, its own volume, mute and effects are not");
        }
        if (!t.skip) readChannel(ch, t);
        tracks.push_back(std::move(t));
        if (group) {
            for (pugi::xml_node child : n.children("Track")) readTrack(child);
        }
    }

    void readChannel(pugi::xml_node ch, TrackDef& t)
    {
        if (!ch) return;
        if (const pugi::xml_node v = ch.child("Volume")) {
            const std::string unit = v.attribute("unit").value();
            const double value = v.attribute("value").as_double(1.0);
            double db = 0.0;
            if (unit == "decibel") {
                db = value;
            } else {  // linear (also what the format's own examples use)
                db = value <= 0.0 ? kMinVolumeDb : 20.0 * std::log10(value);
            }
            if (db > kMaxVolumeDb) note(label(t) + ": its volume " + std::to_string(std::lround(db)) + " dB (mini-daw goes to +6 dB)");
            t.volumeDb = static_cast<float>(std::clamp(db, static_cast<double>(kMinVolumeDb), static_cast<double>(kMaxVolumeDb)));
        }
        t.mute = ch.child("Mute").attribute("value").as_bool(false);
        t.solo = ch.attribute("solo").as_bool(false);
        if (const pugi::xml_node p = ch.child("Pan")) {
            const std::string unit = p.attribute("unit").value();
            const double v = p.attribute("value").as_double(unit == "normalized" ? 0.5 : 0.0);
            const double centre = unit == "normalized" ? 0.5 : 0.0;
            if (std::abs(v - centre) > 0.01) note(label(t) + ": its pan (mini-daw has none)");
        }
        if (const pugi::xml_node sends = ch.child("Sends"); sends && sends.first_child()) note(label(t) + ": its sends");
        for (pugi::xml_node d : ch.child("Devices").children()) {
            const std::string type = d.name();
            const std::string role = d.attribute("deviceRole").value();
            const std::string devName = d.attribute("deviceName").value();
            if (type == "Vst3Plugin" && role == "instrument" && t.instrumentClass.empty()) {
                const std::string cls = vst3ClassFromDeviceId(d.attribute("deviceID").value());
                const pugi::xml_node state = d.child("State");
                if (cls.empty() && !state) {
                    note(label(t) + ": its instrument \"" + devName + "\" (no class ID)");
                    continue;
                }
                t.instrumentClass = cls;
                t.instrumentName = devName;
                t.statePath = state.attribute("path").value();
                t.stateExternal = state.attribute("external").as_bool(false);
                continue;
            }
            std::string what = type == "Vst3Plugin" ? "VST3" : type == "Vst2Plugin" ? "VST2" : type == "ClapPlugin" ? "CLAP"
                             : type == "AuPlugin"   ? "AU"
                                                    : "built-in";
            note(label(t) + ": " + (role == "instrument" ? "a second instrument" : "an effect") + " \"" +
                 (devName.empty() ? type : devName) + "\" (" + what + ")");
        }
    }

    int trackIndex(const std::string& id)
    {
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            if (tracks[i].id == id) return static_cast<int>(i);
        }
        return -1;
    }

    void walk(pugi::xml_node node, TimeUnit unit, int track, const std::string& lane)
    {
        int laneNumber = 0;
        for (pugi::xml_node c : node.children()) {
            const std::string kind = c.name();
            const TimeUnit cu = unitOf(c, unit);
            int ct = track;
            std::string cl = lane;
            if (const pugi::xml_attribute a = c.attribute("track")) {
                ct = trackIndex(a.value());
                if (ct != track) cl.clear();
            }
            if (kind == "Lanes") {
                if (!c.attribute("track") && track >= 0) {  // a lane of the track: a track of its own
                    ++laneNumber;
                    std::string n = c.attribute("name").value();
                    if (n.empty()) n = "lane " + std::to_string(laneNumber);
                    cl = lane.empty() ? n : lane + " " + n;
                }
                walk(c, cu, ct, cl);
            } else if (kind == "Clips") {
                for (pugi::xml_node clip : c.children("Clip")) readClip(clip, cu, ct, cl);
            } else if (kind == "Notes" || kind == "Audio" || kind == "Warps") {
                // Content right on the lane: as a clip from 0 that never ends.
                if (ct < 0) {
                    ++unknownClipTracks_;
                    continue;
                }
                ClipMap m;
                readContent(c, cu, m, 0.0, 0.0, tracks[static_cast<std::size_t>(ct)], cl);
            } else if (kind == "Markers") {
                readMarkers(c, cu);
            } else if (kind == "Points") {
                if (ct >= 0) ++tracks[static_cast<std::size_t>(ct)].automation;
                else ++automationNoTrack_;
            } else if (kind == "Video") {
                note("A video on the timeline");
            }
        }
    }

    void readMarkers(pugi::xml_node list, TimeUnit unit)
    {
        for (pugi::xml_node m : list.children("Marker")) {
            const auto t = number(m, "time");
            if (!t) continue;
            markers.emplace_back(times_.sec(*t, unit), m.attribute("name").value());
        }
    }

    void readClip(pugi::xml_node clip, TimeUnit unit, int track, const std::string& lane)
    {
        if (track < 0) {
            ++unknownClipTracks_;
            return;
        }
        TrackDef& t = tracks[static_cast<std::size_t>(track)];
        const std::string clipName = clip.attribute("name").value();
        const std::string what = label(t) + ": the clip" + (clipName.empty() ? std::string() : " \"" + clipName + "\"");
        if (!clip.attribute("enable").as_bool(true)) {
            note(what + " (switched off)");
            return;
        }
        ClipMap m;
        m.time = times_.sec(number(clip, "time").value_or(0.0), unit);
        if (const auto d = number(clip, "duration")) m.duration = times_.sec(*d, unit);
        const TimeUnit cu = unitOf(clip, unit, "contentTimeUnit");
        m.playStart = times_.sec(number(clip, "playStart").value_or(0.0), cu);
        if (const auto s = number(clip, "playStop")) m.playStop = times_.sec(*s, cu);
        const auto ls = number(clip, "loopStart");
        const auto le = number(clip, "loopEnd");
        if (ls && le && *le > *ls) {
            m.loop = true;
            m.loopStart = times_.sec(*ls, cu);
            m.loopEnd = times_.sec(*le, cu);
            if (m.duration == kInf) m.duration = m.loopEnd - m.playStart;  // no length: one pass
        }
        const TimeUnit fu = unitOf(clip, unit, "fadeTimeUnit");
        const double fadeIn = times_.sec(number(clip, "fadeInTime").value_or(0.0), fu);
        const double fadeOut = times_.sec(number(clip, "fadeOutTime").value_or(0.0), fu);

        pugi::xml_node content = clip.first_child();
        while (content && content.type() != pugi::node_element) content = content.next_sibling();
        if (!content) {
            if (const pugi::xml_attribute ref = clip.attribute("reference")) {
                content = clip.root().find_node([&](pugi::xml_node n) {
                    return std::strcmp(n.attribute("id").value(), ref.value()) == 0;
                });
            }
        }
        if (!content) {
            note(what + " (empty)");
            return;
        }
        readContent(content, cu, m, fadeIn, fadeOut, t, lane, what);
    }

    void readContent(pugi::xml_node content, TimeUnit cu, const ClipMap& m, double fadeIn, double fadeOut, TrackDef& t,
                     const std::string& lane, const std::string& what = {})
    {
        const std::string kind = content.name();
        const std::string context = what.empty() ? label(t) : what;
        const TimeUnit inner = unitOf(content, cu);
        if (kind == "Notes") {
            LaneContent& l = t.lane(lane);
            readNotes(content, inner, m, l);
            ++l.noteClips;
            l.notesFrom = std::min(l.notesFrom, m.time);
            if (m.duration != kInf) l.notesTo = std::max(l.notesTo, m.time + m.duration);
        } else if (kind == "Audio") {
            AudioPiece p;
            p.map = m;
            p.fadeIn = fadeIn;
            p.fadeOut = fadeOut;
            if (!readFile(content, p)) {
                note(context + " (its audio has no file)");
                return;
            }
            t.lane(lane).audio.push_back(std::move(p));
        } else if (kind == "Warps") {
            const TimeUnit wcu = unitOf(content, TimeUnit::Seconds, "contentTimeUnit");
            std::vector<WarpPoint> w;
            for (pugi::xml_node p : content.children("Warp")) {
                const auto a = number(p, "time");
                const auto b = number(p, "contentTime");
                if (a && b) w.push_back({times_.sec(*a, inner), times_.sec(*b, wcu)});
            }
            std::sort(w.begin(), w.end(), [](const WarpPoint& a, const WarpPoint& b) { return a.time < b.time; });
            bool ownSpeed = true;
            for (std::size_t i = 1; i < w.size(); ++i) {
                const double span = w[i].time - w[i - 1].time;
                if (span <= 0.0) continue;
                const double speed = (w[i].content - w[i - 1].content) / span;
                if (std::abs(speed - 1.0) > 0.002) ownSpeed = false;
            }
            pugi::xml_node audio = content.child("Audio");
            if (!audio) {
                note(context + " (warped content other than audio)");
                return;
            }
            if (!ownSpeed) {
                note(context + " (time-stretched audio: mini-daw cannot stretch)");
                return;
            }
            AudioPiece p;
            p.map = m;
            p.fadeIn = fadeIn;
            p.fadeOut = fadeOut;
            p.warps = std::move(w);
            if (!readFile(audio, p)) {
                note(context + " (its audio has no file)");
                return;
            }
            t.lane(lane).audio.push_back(std::move(p));
        } else if (kind == "Clips" || kind == "Lanes") {
            note(context + " (clips inside a clip)");
        } else if (kind == "Points") {
            ++t.automation;
        } else {
            note(context + " (" + kind + ")");
        }
    }

    static bool readFile(pugi::xml_node audio, AudioPiece& p)
    {
        const pugi::xml_node file = audio.child("File");
        p.path = file.attribute("path").value();
        p.external = file.attribute("external").as_bool(false);
        return !p.path.empty();
    }

    void readNotes(pugi::xml_node notes, TimeUnit unit, const ClipMap& m, LaneContent& l)
    {
        const double windowEnd = m.time + m.duration;
        for (pugi::xml_node n : notes.children("Note")) {
            const auto time = number(n, "time");
            const auto dur = number(n, "duration");
            const int key = n.attribute("key").as_int(-1);
            if (!time || !dur || key < 0 || key > 127) continue;
            const double c = times_.sec(*time, unit);
            const double d = std::max(0.0, times_.sec(*dur, unit));
            m.forEachPass([&](double from, double to, double at) {
                if (c < from || c >= to) return;
                const double start = at + (c - from);
                if (start >= windowEnd) return;
                NoteOut o;
                o.time = start;
                o.duration = std::min({start + d, at + (to - from), windowEnd}) - start;
                o.channel = std::clamp(n.attribute("channel").as_int(0), 0, 15);
                o.key = key;
                if (const auto v = number(n, "vel")) o.vel = *v;
                if (const auto r = number(n, "rel")) o.rel = *r;
                l.notes.push_back(o);
            });
        }
    }

    DawImportResult& result_;
    const Times& times_;
    int unknownClipTracks_ = 0;
    int automationNoTrack_ = 0;
};

// Source audio for rendering: frames read in blocks, kept for reuse.
class Source {
public:
    bool open(const fs::path& path, std::string& error)
    {
        if (!reader_.open(path, error) || reader_.sampleRate() == 0 || reader_.channels() == 0) {
            if (error.empty()) error = "no sound in it";
            return false;
        }
        return true;
    }
    unsigned int rate() const { return reader_.sampleRate(); }
    unsigned int channels() const { return reader_.channels(); }
    std::uint64_t frames() const { return reader_.frames(); }
    double seconds() const { return static_cast<double>(reader_.frames()) / reader_.sampleRate(); }
    float at(std::int64_t frame, unsigned int ch)
    {
        if (frame < 0 || static_cast<std::uint64_t>(frame) >= reader_.frames()) return 0.0f;
        if (frame < cacheStart_ || frame >= cacheStart_ + cacheFrames_) load(frame);
        return cache_[static_cast<std::size_t>(frame - cacheStart_) * reader_.channels() + ch];
    }

private:
    void load(std::int64_t frame)
    {
        constexpr std::size_t kBlock = 65536;
        cacheStart_ = std::max<std::int64_t>(0, frame - 64);  // a little before: the kernel looks back
        cache_.assign(kBlock * reader_.channels(), 0.0f);
        reader_.seek(static_cast<std::uint64_t>(cacheStart_));
        cacheFrames_ = static_cast<std::int64_t>(reader_.read(cache_.data(), kBlock));
        if (cacheFrames_ <= frame - cacheStart_) cacheFrames_ = frame - cacheStart_ + 1;  // a read error: silence
    }
    WavReader reader_;
    std::vector<float> cache_;
    std::int64_t cacheStart_ = 0;
    std::int64_t cacheFrames_ = 0;
};

struct Placed {
    const AudioPiece* piece = nullptr;
    Source* source = nullptr;
    double duration = 0.0;  // the clip's length on the arrangement (finite)
    std::unique_ptr<SincKernel> kernel;
};

// All audio pieces of one lane into one WAV at `rate`, from the first piece's
// start; false with `error` when it cannot be written. `startFrame` gets its place.
bool renderAudio(std::vector<Placed>& placed, unsigned int rate, const fs::path& out, std::int64_t& startFrame,
                 unsigned int& channelsOut, std::string& error)
{
    double from = kInf;
    double to = 0.0;
    channelsOut = 1;
    for (const Placed& p : placed) {
        from = std::min(from, p.piece->map.time);
        to = std::max(to, p.piece->map.time + p.duration);
        if (p.source->channels() >= 2) channelsOut = 2;
    }
    startFrame = std::llround(from * rate);
    const std::int64_t endFrame = std::llround(to * rate);
    const std::uint64_t total = endFrame > startFrame ? static_cast<std::uint64_t>(endFrame - startFrame) : 0;
    WavWriter w;
    if (!w.open(out, rate, channelsOut, error)) return false;
    constexpr std::size_t kBlock = 8192;
    std::vector<float> buf(kBlock * channelsOut);
    for (std::uint64_t done = 0; done < total;) {
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(kBlock, total - done));
        std::fill(buf.begin(), buf.end(), 0.0f);
        for (Placed& p : placed) {
            const AudioPiece& a = *p.piece;
            const double clipFrom = a.map.time;
            const double clipTo = a.map.time + p.duration;
            const double blockFrom = static_cast<double>(startFrame + static_cast<std::int64_t>(done)) / rate;
            const double blockTo = blockFrom + static_cast<double>(n) / rate;
            if (clipTo <= blockFrom || clipFrom >= blockTo) continue;
            Source& s = *p.source;
            const unsigned int srcCh = s.channels();
            const double srcRate = s.rate();
            for (std::size_t i = 0; i < n; ++i) {
                const double t = static_cast<double>(startFrame + static_cast<std::int64_t>(done + i)) / rate;
                const double u = t - clipFrom;
                if (u < 0.0 || u >= p.duration) continue;
                const auto c = a.map.contentAt(u);
                if (!c) continue;
                const double pos = warpAt(a.warps, *c) * srcRate;  // in source frames
                float gain = 1.0f;
                if (a.fadeIn > 0.0 && u < a.fadeIn) gain *= static_cast<float>(u / a.fadeIn);
                if (a.fadeOut > 0.0 && p.duration - u < a.fadeOut) gain *= static_cast<float>((p.duration - u) / a.fadeOut);
                const double whole = std::round(pos);
                for (unsigned int ch = 0; ch < channelsOut; ++ch) {
                    const unsigned int sc = std::min(ch, srcCh - 1);  // mono → both sides
                    float v = 0.0f;
                    if (std::abs(pos - whole) < 1e-4 && static_cast<unsigned int>(srcRate) == rate) {
                        v = s.at(static_cast<std::int64_t>(whole), sc);  // the same rate on whole frames: exact
                    } else {
                        if (!p.kernel) p.kernel = std::make_unique<SincKernel>(rate / srcRate);
                        const auto base = static_cast<std::int64_t>(std::floor(pos));
                        const int hw = p.kernel->halfWidth();
                        for (std::int64_t k = base - hw + 1; k <= base + hw; ++k) {
                            v += s.at(k, sc) * p.kernel->weight(pos - static_cast<double>(k));
                        }
                    }
                    buf[i * channelsOut + ch] += v * gain;
                }
            }
        }
        if (!w.write(buf.data(), n, error)) {
            std::string ignored;
            w.close(ignored);
            return false;
        }
        done += n;
    }
    return w.close(error);
}

bool writeNotes(const LaneContent& l, unsigned int rate, const fs::path& out, const std::string& name,
                std::int64_t& startFrame, std::string& error)
{
    double from = l.notesFrom;
    double to = l.notesTo;
    for (const NoteOut& n : l.notes) {
        from = std::min(from, n.time);
        to = std::max(to, n.time + n.duration);
    }
    if (from == kInf) from = 0.0;
    startFrame = std::llround(from * rate);
    struct Ev {
        MidiEvent e;
        bool on = false;
    };
    std::vector<Ev> evs;
    for (const NoteOut& n : l.notes) {
        const std::int64_t on = std::llround(n.time * rate) - startFrame;
        std::int64_t off = std::llround((n.time + n.duration) * rate) - startFrame;
        if (off <= on) off = on + 1;  // at least a frame
        const auto vel = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(n.vel * 127.0)), 1, 127));
        const auto rel = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(n.rel * 127.0)), 0, 127));
        Ev a;
        a.e.frame = static_cast<std::uint64_t>(std::max<std::int64_t>(0, on));
        a.e.data = {static_cast<std::uint8_t>(0x90 | n.channel), static_cast<std::uint8_t>(n.key), vel};
        a.e.size = 3;
        a.on = true;
        Ev b;
        b.e.frame = static_cast<std::uint64_t>(std::max<std::int64_t>(0, off));
        b.e.data = {static_cast<std::uint8_t>(0x80 | n.channel), static_cast<std::uint8_t>(n.key), rel};
        b.e.size = 3;
        evs.push_back(a);
        evs.push_back(b);
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) {
        if (a.e.frame != b.e.frame) return a.e.frame < b.e.frame;
        return !a.on && b.on;  // a note-off before a note-on at the same time
    });
    SmfTrack track;
    track.name = name;
    for (const Ev& e : evs) track.events.push_back(e.e);
    const std::int64_t end = std::max<std::int64_t>(std::llround(to * rate) - startFrame,
                                                    evs.empty() ? 0 : static_cast<std::int64_t>(evs.back().e.frame));
    return writeSmf(out, name, {track}, rate, MidiTiming::Ppq, static_cast<std::uint64_t>(std::max<std::int64_t>(0, end)),
                    false, error);
}

} // namespace

DawImportResult importDawProject(const fs::path& file, const fs::path& projectFolder, unsigned int sampleRate,
                                 const std::string& takeFolderName)
{
    DawImportResult result;
    const unsigned int rate = sampleRate ? sampleRate : 48000;

    // 1. Read and check everything before anything is made.
    ZipReader zip;
    std::string error;
    if (!zip.open(file, error)) {
        result.error = error;
        return result;
    }
    std::string xml;
    if (!zip.read("project.xml", xml, error)) {
        result.error = "not a DAWproject file: " + error;
        return result;
    }
    pugi::xml_document doc;
    if (const pugi::xml_parse_result parsed = doc.load_buffer(xml.data(), xml.size()); !parsed) {
        result.error = "project.xml is damaged: " + std::string(parsed.description()) + " at byte " +
                       std::to_string(parsed.offset);
        return result;
    }
    const pugi::xml_node project = doc.child("Project");
    if (!project) {
        result.error = "project.xml has no <Project>";
        return result;
    }
    const std::string version = project.attribute("version").value();
    if (!version.empty() && std::atoi(version.c_str()) > 1) {
        result.error = "the file is in DAWproject version " + version + "; mini-daw reads version 1";
        return result;
    }
    if (const pugi::xml_node app = project.child("Application")) {
        result.application = std::string(app.attribute("name").value());
        const std::string v = app.attribute("version").value();
        if (!v.empty()) result.application += " " + v;
    }
    std::string metaText;
    std::string projectComment;
    result.title = pathToUtf8(file.stem());
    if (zip.find("metadata.xml") >= 0 && zip.read("metadata.xml", metaText, error)) {
        pugi::xml_document meta;
        if (meta.load_buffer(metaText.data(), metaText.size())) {
            const pugi::xml_node m = meta.child("MetaData");
            if (const std::string t = m.child_value("Title"); !t.empty()) result.title = t;
            projectComment = m.child_value("Comment");
        }
    }

    Times times;
    if (const auto bpm = number(project.child("Transport").child("Tempo"), "value"); bpm && *bpm > 0.0) {
        times.secondsPerBeat = 60.0 / *bpm;
    }
    Reader reader(result, times);
    reader.readStructure(project.child("Structure"));
    reader.readArrangement(project.child("Arrangement"));
    if (const pugi::xml_node scenes = project.child("Scenes"); scenes && scenes.child("Scene")) {
        int n = 0;
        for (pugi::xml_node s : scenes.children("Scene")) (void)s, ++n;
        result.leftOut.push_back(std::to_string(n) + " scene(s) of the clip launcher");
    }

    // 2. The project folder and the recording folder for the files.
    std::error_code ec;
    const bool folderWasThere = fs::exists(projectFolder, ec);
    if (!createProjectFolder(projectFolder, error)) {
        result.error = error;
        return result;
    }
    const fs::path takes = projectFolder / "takes";
    const bool takesWasThere = fs::exists(takes, ec);
    const fs::path takeDir = takes / takeFolderName;
    if (fs::exists(takeDir, ec) || !fs::create_directories(takeDir, ec)) {
        result.error = "cannot make " + pathToUtf8(takeDir) + (ec ? ": " + ec.message() : " (it is there already)");
        if (!folderWasThere) fs::remove(projectFolder, ec);
        return result;
    }
    const fs::path unpacked = takeDir / ".unpacked";
    auto cleanUp = [&](bool all) {
        std::error_code e;
        fs::remove_all(unpacked, e);
        if (!all) return;
        fs::remove_all(takeDir, e);
        fs::remove(projectFolder / kProjectFileName, e);
        if (!takesWasThere) fs::remove(takes, e);
        if (!folderWasThere) fs::remove(projectFolder, e);  // only when empty
    };
    auto fail = [&](const std::string& why) {
        cleanUp(true);
        DawImportResult r;
        r.error = why;
        return r;
    };

    // 3. Tracks and their takes.
    ProjectData data;
    data.sampleRate = rate;
    data.note = projectComment;
    std::map<std::string, std::unique_ptr<Source>> sources;  // by ZIP entry or path
    int unpackedFiles = 0;
    auto sourceFor = [&](const AudioPiece& p, const std::string& who) -> Source* {
        const std::string key = (p.external ? "file:" : "zip:") + p.path;
        if (auto it = sources.find(key); it != sources.end()) return it->second.get();
        std::string ext = pathToUtf8(pathFromUtf8(p.path).extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".wav") {
            result.leftOut.push_back(who + ": " + p.path + " (only WAV files are read)");
            return nullptr;
        }
        fs::path path;
        std::string err;
        if (p.external) {
            path = pathFromUtf8(p.path);
            if (path.is_relative()) path = file.parent_path() / path;
        } else {
            std::error_code e;
            fs::create_directories(unpacked, e);
            path = unpacked / (std::to_string(++unpackedFiles) + ".wav");
            if (!zip.extract(p.path, path, err)) {
                result.leftOut.push_back(who + ": " + err);
                return nullptr;
            }
        }
        auto s = std::make_unique<Source>();
        if (!s->open(path, err)) {
            result.leftOut.push_back(who + ": " + p.path + " could not be read (" + err + ")");
            return nullptr;
        }
        if (s->channels() > 2) result.leftOut.push_back(who + ": " + p.path + " has " + std::to_string(s->channels()) +
                                                        " channels; the first two are used");
        Source* raw = s.get();
        sources.emplace(key, std::move(s));
        return raw;
    };

    double recordingFrom = kInf;  // the earliest take: where the marks count from
    int trackNumber = 0;
    for (TrackDef& def : reader.tracks) {
        if (def.skip) continue;
        if (def.lanes.empty()) def.lane("");  // an empty track is still a track
        std::stable_sort(def.lanes.begin(), def.lanes.end(),
                         [](const LaneContent& a, const LaneContent& b) { return a.name.empty() && !b.name.empty(); });
        for (LaneContent& lane : def.lanes) {
            const std::string baseName = lane.name.empty() ? def.name : def.name + " " + lane.name;
            const bool hasNotes = lane.noteClips > 0;
            const bool hasAudio = !lane.audio.empty();
            const bool midi = hasNotes || (!hasAudio && lane.name.empty() && (def.notesType || !def.instrumentClass.empty()));
            const bool audio = hasAudio || (!midi && lane.name.empty());
            if (midi) {
                Track t;
                t.number = ++trackNumber;
                t.name = baseName;
                t.kind = TrackKind::Midi;
                t.volumeDb = def.volumeDb;
                t.mute = def.mute;
                t.solo = def.solo;
                if (!def.instrumentClass.empty() || !def.statePath.empty()) {
                    t.instrumentClass = def.instrumentClass;
                    t.instrumentName = def.instrumentName;
                    if (!def.statePath.empty()) {
                        std::string bytes;
                        std::string err;
                        bool got = false;
                        if (def.stateExternal) {
                            fs::path p = pathFromUtf8(def.statePath);
                            if (p.is_relative()) p = file.parent_path() / p;
                            std::FILE* f = openRead(p);
                            if (f) {
                                char buf[65536];
                                std::size_t k;
                                while ((k = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.append(buf, k);
                                std::fclose(f);
                                got = true;
                            } else {
                                err = std::strerror(errno);
                            }
                        } else {
                            got = zip.read(def.statePath, bytes, err);
                        }
                        std::optional<VstPresetData> preset;
                        if (got) preset = decodeVstPreset(bytes, err);
                        if (preset) {
                            if (t.instrumentClass.empty()) t.instrumentClass = vst3ClassFromDeviceId(preset->classId);
                            t.instrumentState = preset->component;
                            t.instrumentControllerState = preset->controller;
                        } else {
                            result.leftOut.push_back(label(def) + ": its instrument's sound settings (" + err + ")");
                        }
                    }
                    if (!t.instrumentClass.empty()) ++result.instruments;
                }
                if (hasNotes) {
                    t.take = takeDir / pathFromUtf8(takeFileName(t));
                    std::string err;
                    if (!writeNotes(lane, rate, t.take, baseName, t.takeStart, err)) {
                        return fail("cannot write " + pathToUtf8(t.take) + ": " + err);
                    }
                    recordingFrom = std::min(recordingFrom, static_cast<double>(t.takeStart) / rate);
                    result.noteClips += lane.noteClips;
                    result.notes += static_cast<int>(lane.notes.size());
                }
                data.tracks.push_back(std::move(t));
                ++result.tracks;
            }
            if (audio) {
                Track t;
                t.number = ++trackNumber;
                t.name = midi ? baseName + " audio" : baseName;
                t.kind = TrackKind::Audio;
                t.channels = {0, 1};
                t.volumeDb = def.volumeDb;
                t.mute = def.mute;
                t.solo = def.solo;
                std::vector<Placed> placed;
                for (const AudioPiece& p : lane.audio) {
                    Source* s = sourceFor(p, label(def));
                    if (!s) continue;
                    double d = p.map.duration;
                    if (d == kInf) {  // no length: up to the end of the file (or playStop)
                        double contentEnd = p.map.playStop.value_or(kInf);
                        if (contentEnd == kInf) {
                            // the content time where the audio file ends
                            contentEnd = p.warps.empty() ? s->seconds()
                                                         : p.warps.back().time + (s->seconds() - p.warps.back().content);
                        }
                        d = std::max(0.0, contentEnd - p.map.playStart);
                    }
                    if (d <= 0.0) continue;
                    Placed pl;
                    pl.piece = &p;
                    pl.source = s;
                    pl.duration = d;
                    placed.push_back(std::move(pl));
                }
                if (!placed.empty()) {
                    t.take = takeDir / pathFromUtf8(takeFileName(t));
                    unsigned int channels = 2;
                    std::string err;
                    if (!renderAudio(placed, rate, t.take, t.takeStart, channels, err)) {
                        return fail("cannot write " + pathToUtf8(t.take) + ": " + err);
                    }
                    t.channels = channels == 1 ? std::vector<unsigned int>{0} : std::vector<unsigned int>{0, 1};
                    recordingFrom = std::min(recordingFrom, static_cast<double>(t.takeStart) / rate);
                    result.audioClips += static_cast<int>(placed.size());
                }
                data.tracks.push_back(std::move(t));
                ++result.tracks;
            }
        }
    }
    sources.clear();  // closes the files before the unpacked ones are removed

    // 4. The recording's name, note and marks; the project file.
    RecordingNotes notes;
    notes.name = result.title;
    notes.note = "Imported from " + pathToUtf8(file.filename()) +
                 (result.application.empty() ? std::string() : " (made with " + result.application + ")") + ".";
    if (recordingFrom == kInf) recordingFrom = 0.0;
    std::stable_sort(reader.markers.begin(), reader.markers.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    int early = 0;
    for (const auto& [at, name] : reader.markers) {
        if (at < recordingFrom - 1e-9) {
            ++early;
            continue;
        }
        notes.marks.push_back({at - recordingFrom, name.empty() ? std::string("marker") : name});
    }
    if (early > 0) result.leftOut.push_back(std::to_string(early) + " marker(s) before the first clip");
    result.markers = static_cast<int>(notes.marks.size());
    if (!saveRecordingNotes(takeDir, notes, error)) return fail("cannot write the recording's notes: " + error);
    if (!saveProject(projectFolder, data, error)) return fail(error);
    cleanUp(false);
    result.ok = true;
    return result;
}

} // namespace minidaw
