#include "minidaw/dawproject.hpp"

#include "minidaw/path_util.hpp"
#include "minidaw/presets.hpp"
#include "minidaw/recording_notes.hpp"
#include "minidaw/smf.hpp"
#include "minidaw/track.hpp"
#include "minidaw/wav_reader.hpp"

#include <miniz.h>
#include <nlohmann/json.hpp>
#include <pugixml.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace minidaw {

std::string xmlSafeText(const std::string& text)
{
    // nlohmann's replace handler turns bytes that are not UTF-8 into U+FFFD.
    const std::string valid =
        nlohmann::json::parse(nlohmann::json(text).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))
            .get<std::string>();
    std::string out;
    out.reserve(valid.size());
    for (std::size_t i = 0; i < valid.size(); ++i) {
        const auto c = static_cast<unsigned char>(valid[i]);
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') continue;  // not allowed in XML 1.0
        // U+FFFE and U+FFFF (EF BF BE / EF BF BF) are not allowed either.
        if (c == 0xEF && i + 2 < valid.size() && static_cast<unsigned char>(valid[i + 1]) == 0xBF &&
            (static_cast<unsigned char>(valid[i + 2]) == 0xBE || static_cast<unsigned char>(valid[i + 2]) == 0xBF)) {
            i += 2;
            continue;
        }
        out += valid[i];
    }
    return out;
}

std::string xmlNumber(double value)
{
    if (!std::isfinite(value)) value = 0.0;
    if (value == 0.0) value = 0.0;  // no "-0"
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, value);
    return std::string(buf, r.ptr);
}

namespace {

// Writes the ZIP: entries from memory or from files, stored or compressed.
class ZipWriter {
public:
    ~ZipWriter()
    {
        if (open_) mz_zip_writer_end(&zip_);
        if (file_) std::fclose(file_);
    }
    bool begin(const fs::path& path, std::string& error)
    {
#if defined(_WIN32)
        file_ = _wfopen(path.c_str(), L"wb");
#else
        file_ = std::fopen(path.c_str(), "wb");
#endif
        if (!file_) {
            error = "cannot write " + pathToUtf8(path) + ": " + std::strerror(errno);
            return false;
        }
        std::memset(&zip_, 0, sizeof zip_);
        if (!mz_zip_writer_init_cfile(&zip_, file_, 0)) return fail(error);
        open_ = true;
        return true;
    }
    bool addText(const std::string& name, const std::string& text, std::string& error)
    {
        if (!mz_zip_writer_add_mem(&zip_, name.c_str(), text.data(), text.size(), MZ_DEFAULT_LEVEL)) return fail(error);
        return true;
    }
    bool addBytes(const std::string& name, const std::string& bytes, std::string& error)
    {
        return addText(name, bytes, error);
    }
    // A WAV file, stored without compression (it hardly shrinks, and big takes stay fast).
    bool addFile(const std::string& name, const fs::path& source, std::string& error)
    {
#if defined(_WIN32)
        std::FILE* src = _wfopen(source.c_str(), L"rb");
#else
        std::FILE* src = std::fopen(source.c_str(), "rb");
#endif
        if (!src) {
            error = "cannot read " + pathToUtf8(source) + ": " + std::strerror(errno);
            return false;
        }
        std::error_code ec;
        const auto size = fs::file_size(source, ec);
        const bool ok = !ec && mz_zip_writer_add_cfile(&zip_, name.c_str(), src, size, nullptr, nullptr, 0,
                                                       MZ_NO_COMPRESSION, nullptr, 0, nullptr, 0);
        std::fclose(src);
        if (!ok) {
            if (ec) {
                error = "cannot read " + pathToUtf8(source) + ": " + ec.message();
                return false;
            }
            return fail(error);
        }
        return true;
    }
    bool finish(std::string& error)
    {
        if (!mz_zip_writer_finalize_archive(&zip_)) return fail(error);
        mz_zip_writer_end(&zip_);
        open_ = false;
        const bool flushed = std::fflush(file_) == 0;
        const bool closed = std::fclose(file_) == 0;
        file_ = nullptr;
        if (!flushed || !closed) {
            error = std::string("cannot write the file: ") + std::strerror(errno);
            return false;
        }
        return true;
    }

private:
    bool fail(std::string& error)
    {
        error = std::string("ZIP: ") + mz_zip_get_error_string(mz_zip_get_last_error(&zip_));
        return false;
    }
    std::FILE* file_ = nullptr;
    mz_zip_archive zip_{};
    bool open_ = false;
};

struct XmlText : pugi::xml_writer {
    std::string text;
    void write(const void* data, size_t size) override { text.append(static_cast<const char*>(data), size); }
};

std::string documentText(const pugi::xml_document& doc)
{
    XmlText out;
    doc.save(out, "  ", pugi::format_default, pugi::encoding_utf8);
    return out.text;
}

void attr(pugi::xml_node n, const char* name, const std::string& value)
{
    n.append_attribute(name).set_value(xmlSafeText(value).c_str());
}
void attr(pugi::xml_node n, const char* name, double value) { attr(n, name, xmlNumber(value)); }
void attr(pugi::xml_node n, const char* name, int value) { attr(n, name, std::to_string(value)); }
void attrBool(pugi::xml_node n, const char* name, bool value) { attr(n, name, std::string(value ? "true" : "false")); }

struct Ids {
    int next = 0;
    std::string make() { return "id" + std::to_string(next++); }
};

pugi::xml_node realParameter(pugi::xml_node parent, const char* element, Ids& ids, const char* name, const char* unit,
                             double value, double min, double max)
{
    pugi::xml_node p = parent.append_child(element);
    attr(p, "max", max);
    attr(p, "min", min);
    attr(p, "unit", std::string(unit));
    attr(p, "value", value);
    attr(p, "id", ids.make());
    attr(p, "name", std::string(name));
    return p;
}

pugi::xml_node channel(pugi::xml_node track, Ids& ids, const char* role, int audioChannels, const std::string& destination,
                       bool solo)
{
    pugi::xml_node ch = track.append_child("Channel");
    attr(ch, "audioChannels", audioChannels);
    if (!destination.empty()) attr(ch, "destination", destination);
    attr(ch, "role", std::string(role));
    attrBool(ch, "solo", solo);
    const std::string id = ids.make();
    attr(ch, "id", id);
    return ch;
}

// Mute, Pan, Volume come after the Devices (the schema's order).
void channelControls(pugi::xml_node ch, Ids& ids, bool mute, double gain)
{
    pugi::xml_node m = ch.append_child("Mute");
    attrBool(m, "value", mute);
    attr(m, "id", ids.make());
    attr(m, "name", std::string("Mute"));
    realParameter(ch, "Pan", ids, "Pan", "normalized", 0.5, 0.0, 1.0);
    realParameter(ch, "Volume", ids, "Volume", "linear", gain, 0.0, 2.0);
}

double linearGain(float db)
{
    if (db <= kMinVolumeDb) return 0.0;
    return std::min(2.0, std::pow(10.0, static_cast<double>(db) / 20.0));
}

struct NoteOut {
    double time = 0.0;      // seconds from the clip's content start
    double duration = 0.0;
    int channel = 0;
    int key = 0;
    double vel = 0.0;
    double rel = 0.0;
};

// Notes from a MIDI take: note-ons paired with their note-offs (first in, first
// out per channel and key); a note still held at the end ends there. Other
// messages are counted.
struct TakeNotes {
    std::vector<NoteOut> notes;
    double seconds = 0.0;    // the take's length
    int otherMessages = 0;   // controllers, pitch bend, program changes, aftertouch
};

TakeNotes notesFromSmf(const SmfData& smf, unsigned int rate)
{
    TakeNotes out;
    std::uint64_t endFrame = 0;
    const std::vector<MidiEvent> events = smfToFrames(smf, rate, &endFrame);
    std::map<std::pair<int, int>, std::deque<std::pair<std::uint64_t, int>>> held;
    std::uint64_t last = endFrame;
    for (const MidiEvent& e : events) {
        last = std::max(last, e.frame);
        const int status = e.data[0] & 0xF0;
        const int ch = e.data[0] & 0x0F;
        const bool on = status == 0x90 && e.size >= 3 && e.data[2] > 0;
        const bool off = (status == 0x80 && e.size >= 3) || (status == 0x90 && e.size >= 3 && e.data[2] == 0);
        if (on) {
            held[{ch, e.data[1]}].emplace_back(e.frame, e.data[2]);
        } else if (off) {
            auto it = held.find({ch, e.data[1]});
            if (it == held.end() || it->second.empty()) continue;  // a note-off without its note
            const auto [start, vel] = it->second.front();
            it->second.pop_front();
            NoteOut n;
            n.time = static_cast<double>(start) / rate;
            n.duration = static_cast<double>(e.frame - start) / rate;
            n.channel = ch;
            n.key = e.data[1];
            n.vel = vel / 127.0;
            n.rel = (status == 0x80 ? e.data[2] : 64) / 127.0;
            out.notes.push_back(n);
        } else {
            ++out.otherMessages;
        }
    }
    for (const auto& [chKey, queue] : held) {
        for (const auto& [start, vel] : queue) {
            NoteOut n;
            n.time = static_cast<double>(start) / rate;
            n.duration = static_cast<double>(last - start) / rate;
            n.channel = chKey.first;
            n.key = chKey.second;
            n.vel = vel / 127.0;
            n.rel = 64 / 127.0;
            out.notes.push_back(n);
        }
    }
    std::stable_sort(out.notes.begin(), out.notes.end(),
                     [](const NoteOut& a, const NoteOut& b) { return a.time < b.time; });
    out.seconds = static_cast<double>(last) / rate;
    return out;
}

std::string trackLabel(const Track& t)
{
    return "Track " + std::to_string(t.number) + " \"" + t.name + "\"";
}

} // namespace

DawExportResult exportDawProject(const ProjectData& project, unsigned int sampleRate, const std::string& title,
                                 const std::string& appVersion, const fs::path& outFile)
{
    DawExportResult result;
    const unsigned int rate = sampleRate ? sampleRate : (project.sampleRate ? project.sampleRate : 48000);
    Ids ids;

    pugi::xml_document doc;
    pugi::xml_node decl = doc.append_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";
    decl.append_attribute("standalone") = "yes";
    pugi::xml_node root = doc.append_child("Project");
    attr(root, "version", std::string("1.0"));
    pugi::xml_node app = root.append_child("Application");
    attr(app, "name", std::string("mini-daw"));
    attr(app, "version", appVersion);
    pugi::xml_node transport = root.append_child("Transport");
    realParameter(transport, "Tempo", ids, "Tempo", "bpm", 120.0, 20.0, 666.0);
    pugi::xml_node sig = transport.append_child("TimeSignature");
    attr(sig, "denominator", 4);
    attr(sig, "numerator", 4);
    attr(sig, "id", ids.make());
    pugi::xml_node structure = root.append_child("Structure");
    pugi::xml_node arrangement = root.append_child("Arrangement");
    attr(arrangement, "id", ids.make());
    pugi::xml_node lanes = arrangement.append_child("Lanes");
    attr(lanes, "timeUnit", std::string("seconds"));
    attr(lanes, "id", ids.make());

    // The master channel first in the file; the tracks go to it.
    pugi::xml_node master = structure.append_child("Track");
    attr(master, "contentType", std::string("audio notes"));
    attrBool(master, "loaded", true);
    attr(master, "id", ids.make());
    attr(master, "name", std::string("Master"));
    pugi::xml_node masterCh = channel(master, ids, "master", 2, "", false);
    const std::string masterId = masterCh.attribute("id").value();
    channelControls(masterCh, ids, false, 1.0);

    struct Entry {
        std::string zipName;
    };
    std::map<fs::path, Entry> audioFiles;      // a take file used by two tracks is stored once
    std::vector<std::pair<std::string, fs::path>> filesToAdd;
    std::vector<std::pair<std::string, std::string>> bytesToAdd;
    std::set<std::string> zipNames;
    auto uniqueZipName = [&](const std::string& folder, std::string base, const std::string& ext) {
        base = safeFileName(base);
        std::string name = folder + "/" + base + ext;
        for (int i = 2; zipNames.count(name) != 0; ++i) name = folder + "/" + base + "-" + std::to_string(i) + ext;
        zipNames.insert(name);
        return name;
    };
    std::map<fs::path, bool> marksDone;  // recording folders whose marks are written
    std::vector<std::pair<double, std::string>> markers;

    for (const Track& t : project.tracks) {
        const bool isMidi = t.kind == TrackKind::Midi;
        const Track* owner = isMidi ? nullptr : soundOwner(project.tracks, t);
        // Mute so the DAW plays what mini-daw plays (SPEC-VST-03a): the instrument or its recording.
        bool mute = t.mute;
        if (owner && !owner->soundFromRecording) mute = true;
        if (isMidi && !t.instrumentClass.empty() && t.soundFromRecording && linkedSoundTrack(project.tracks, t.number))
            mute = true;

        pugi::xml_node track = structure.append_child("Track");
        attr(track, "contentType", std::string(isMidi ? "notes" : "audio"));
        attrBool(track, "loaded", true);
        const std::string trackId = ids.make();
        attr(track, "id", trackId);
        attr(track, "name", t.name.empty() ? "Track " + std::to_string(t.number) : t.name);
        if (owner) attr(track, "comment", "The sound of track " + std::to_string(owner->number) + "'s instrument");
        const int audioChannels = isMidi ? 2 : (t.channels.size() == 1 ? 1 : 2);
        pugi::xml_node ch = channel(track, ids, "regular", audioChannels, masterId, t.solo);
        if (isMidi && !t.instrumentClass.empty()) {
            pugi::xml_node devices = ch.append_child("Devices");
            pugi::xml_node plugin = devices.append_child("Vst3Plugin");
            attr(plugin, "deviceID", t.instrumentClass);
            attr(plugin, "deviceName", t.instrumentName.empty() ? std::string("Instrument") : t.instrumentName);
            attr(plugin, "deviceRole", std::string("instrument"));
            attrBool(plugin, "loaded", true);
            attr(plugin, "id", ids.make());
            attr(plugin, "name", t.instrumentName.empty() ? std::string("Instrument") : t.instrumentName);
            if (!t.instrumentState.empty()) {
                VstPresetData preset;
                preset.classId = t.instrumentClass;
                preset.component = t.instrumentState;
                preset.controller = t.instrumentControllerState;
                const std::string name =
                    uniqueZipName("plugins", "track-" + std::to_string(t.number) + " " + t.instrumentName, ".vstpreset");
                bytesToAdd.emplace_back(name, encodeVstPreset(preset));
                pugi::xml_node state = plugin.append_child("State");
                attr(state, "path", name);
            }
            ++result.instruments;
        } else if (isMidi && !t.midiOut.empty()) {
            result.leftOut.push_back(trackLabel(t) + ": its MIDI output \"" + t.midiOut +
                                     "\" (DAWproject has no MIDI outputs; choose an instrument there)");
        }
        channelControls(ch, ids, mute, linearGain(t.volumeDb));
        ++result.tracks;

        if (t.take.empty()) continue;
        std::error_code ec;
        if (!fs::is_regular_file(t.take, ec)) {
            result.leftOut.push_back(trackLabel(t) + ": its take " + pathToUtf8(t.take) + " was not found");
            continue;
        }
        // The recording's name, note and marks (recording.json in the take's folder).
        const fs::path folder = t.take.parent_path();
        RecordingNotes notes;
        std::string notesError;
        if (!loadRecordingNotes(folder, notes, notesError)) {
            result.leftOut.push_back(trackLabel(t) + ": the name and notes of its recording (" + notesError + ")");
        }
        const double start = static_cast<double>(t.takeStart) / rate;  // may be < 0 (overdub shift)
        const double cut = start < 0.0 ? -start : 0.0;

        pugi::xml_node trackLanes = lanes.append_child("Lanes");
        attr(trackLanes, "track", trackId);
        attr(trackLanes, "id", ids.make());
        pugi::xml_node clips = trackLanes.append_child("Clips");
        attr(clips, "id", ids.make());
        pugi::xml_node clip = clips.append_child("Clip");
        auto finishClip = [&](double contentSeconds) {
            attr(clip, "time", std::max(0.0, start));
            attr(clip, "duration", std::max(0.0, contentSeconds - cut));
            attr(clip, "contentTimeUnit", std::string("seconds"));
            attr(clip, "playStart", cut);
            attr(clip, "name", notes.name.empty() ? pathToUtf8(folder.filename()) : notes.name);
            if (!notes.note.empty()) attr(clip, "comment", notes.note);
        };

        const std::string ext = [&] {
            std::string e = pathToUtf8(t.take.extension());
            std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return e;
        }();
        if (ext == ".wav") {
            WavReader wav;
            std::string err;
            if (!wav.open(t.take, err) || wav.sampleRate() == 0) {
                result.leftOut.push_back(trackLabel(t) + ": its take could not be read (" + err + ")");
                lanes.remove_child(trackLanes);
                continue;
            }
            const double seconds = static_cast<double>(wav.frames()) / wav.sampleRate();
            auto it = audioFiles.find(t.take);
            if (it == audioFiles.end()) {
                const std::string name =
                    uniqueZipName("audio", pathToUtf8(folder.filename()) + " " + pathToUtf8(t.take.stem()), ".wav");
                it = audioFiles.emplace(t.take, Entry{name}).first;
                filesToAdd.emplace_back(name, t.take);
            }
            finishClip(seconds);
            pugi::xml_node audio = clip.append_child("Audio");
            attr(audio, "channels", static_cast<int>(wav.channels()));
            attr(audio, "sampleRate", static_cast<int>(wav.sampleRate()));
            attr(audio, "duration", seconds);
            attr(audio, "timeUnit", std::string("seconds"));
            attr(audio, "id", ids.make());
            pugi::xml_node file = audio.append_child("File");
            attr(file, "path", it->second.zipName);
            ++result.audioClips;
        } else {
            SmfData smf;
            std::string err;
            if (!readSmf(t.take, smf, err)) {
                result.leftOut.push_back(trackLabel(t) + ": its MIDI take could not be read (" + err + ")");
                lanes.remove_child(trackLanes);
                continue;
            }
            const TakeNotes taken = notesFromSmf(smf, rate);
            double seconds = taken.seconds;
            for (const NoteOut& n : taken.notes) seconds = std::max(seconds, n.time + n.duration);
            finishClip(seconds);
            pugi::xml_node notesNode = clip.append_child("Notes");
            attr(notesNode, "id", ids.make());
            for (const NoteOut& n : taken.notes) {
                pugi::xml_node note = notesNode.append_child("Note");
                attr(note, "time", n.time);
                attr(note, "duration", n.duration);
                attr(note, "channel", n.channel);
                attr(note, "key", n.key);
                attr(note, "vel", n.vel);
                attr(note, "rel", n.rel);
            }
            result.notes += static_cast<int>(taken.notes.size());
            ++result.noteClips;
            if (taken.otherMessages > 0) {
                result.leftOut.push_back(trackLabel(t) + ": " + std::to_string(taken.otherMessages) +
                                         " MIDI message(s) other than notes (controllers, pitch bend, program "
                                         "changes)");
            }
        }
        // Marks made while recording, once per recording, at their place on the timeline.
        if (!marksDone[folder]) {
            marksDone[folder] = true;
            for (const RecordingMark& m : notes.marks) {
                const double at = start + m.seconds;
                if (at >= 0.0) markers.emplace_back(at, m.text);
            }
        }
    }
    if (!markers.empty()) {
        std::stable_sort(markers.begin(), markers.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        pugi::xml_node node = arrangement.append_child("Markers");
        attr(node, "id", ids.make());
        for (const auto& [at, text] : markers) {
            pugi::xml_node m = node.append_child("Marker");
            attr(m, "time", at);
            attr(m, "name", text);
        }
        result.markers = static_cast<int>(markers.size());
    }
    root.append_child("Scenes");

    pugi::xml_document meta;
    pugi::xml_node metaDecl = meta.append_child(pugi::node_declaration);
    metaDecl.append_attribute("version") = "1.0";
    metaDecl.append_attribute("encoding") = "UTF-8";
    metaDecl.append_attribute("standalone") = "yes";
    pugi::xml_node metaRoot = meta.append_child("MetaData");
    if (!title.empty()) metaRoot.append_child("Title").text().set(xmlSafeText(title).c_str());
    if (!project.note.empty()) metaRoot.append_child("Comment").text().set(xmlSafeText(project.note).c_str());

    // The file: written beside the target and renamed, so a failure leaves nothing half.
    fs::path tmp = outFile;
    tmp += ".tmp";
    std::string error;
    bool ok = false;
    {
        ZipWriter zip;
        ok = zip.begin(tmp, error) && zip.addText("project.xml", documentText(doc), error) &&
             zip.addText("metadata.xml", documentText(meta), error);
        for (const auto& [name, bytes] : bytesToAdd) ok = ok && zip.addBytes(name, bytes, error);
        for (const auto& [name, path] : filesToAdd) ok = ok && zip.addFile(name, path, error);
        ok = ok && zip.finish(error);
    }
    std::error_code ec;
    if (ok) {
        fs::rename(tmp, outFile, ec);
        if (ec) {
            ok = false;
            error = "cannot replace " + pathToUtf8(outFile) + ": " + ec.message();
        }
    }
    if (!ok) {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        result = DawExportResult{};
        result.error = error;
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace minidaw
