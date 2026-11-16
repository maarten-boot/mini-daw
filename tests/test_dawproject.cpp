// T-PRJ-10: DAWproject export (P27, SPEC-PRJ-04). The file is read back with
// miniz and pugixml; when the environment variable MINIDAW_DAWPROJECT_OUT names
// a folder, the exported files are also copied there, where the ctest
// "T-PRJ-10: XSD" unpacks them with another ZIP reader (CMake's) and checks
// project.xml and metadata.xml against the format's schemas
// (tests/dawproject/*.xsd) with xmllint.

#include "minidaw/dawproject.hpp"
#include "minidaw/presets.hpp"
#include "minidaw/recording_notes.hpp"
#include "minidaw/smf.hpp"
#include "minidaw/timeline.hpp"
#include "minidaw/wav.hpp"

#include <catch2/catch_test_macros.hpp>
#include <miniz.h>
#include <pugixml.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace minidaw;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir()
    {
        std::random_device rd;
        path = fs::temp_directory_path() / ("minidaw-dawproject-" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void writeWav(const fs::path& path, unsigned int rate, unsigned int channels, std::size_t frames)
{
    WavWriter w;
    std::string err;
    REQUIRE(w.open(path, rate, channels, err));
    std::vector<float> data(frames * channels, 0.25f);
    REQUIRE(w.write(data.data(), frames, err));
    REQUIRE(w.close(err));
}

MidiEvent ev(std::uint64_t frame, std::uint8_t a, std::uint8_t b, std::uint8_t c)
{
    MidiEvent e;
    e.frame = frame;
    e.data = {a, b, c};
    e.size = 3;
    return e;
}

std::map<std::string, std::string> readZip(const fs::path& file)
{
    std::map<std::string, std::string> out;
    std::ifstream in(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    REQUIRE(mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0));
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
        char name[512];
        mz_zip_reader_get_filename(&zip, i, name, sizeof name);
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        REQUIRE(data != nullptr);
        out[name] = std::string(static_cast<const char*>(data), size);
        mz_free(data);
    }
    mz_zip_reader_end(&zip);
    return out;
}

// A copy for the schema check (ctest "T-PRJ-10: XSD").
void keepForXsd(const fs::path& file, const std::string& name)
{
    if (const char* keep = std::getenv("MINIDAW_DAWPROJECT_OUT"); keep && *keep) {
        fs::create_directories(keep);
        fs::copy_file(file, fs::path(keep) / (name + ".dawproject"), fs::copy_options::overwrite_existing);
    }
}

pugi::xml_document projectXml(const fs::path& file)
{
    pugi::xml_document doc;
    REQUIRE(doc.load_string(readZip(file).at("project.xml").c_str()));
    return doc;
}

bool muted(const pugi::xml_node& track) { return track.child("Channel").child("Mute").attribute("value").as_bool(); }
bool soloed(const pugi::xml_node& track) { return track.child("Channel").attribute("solo").as_bool(); }

// A standard MIDI file written byte by byte (for what writeSmf does not make).
void put32(std::string& s, std::uint32_t v)
{
    for (int i = 3; i >= 0; --i) s += static_cast<char>((v >> (8 * i)) & 0xFF);
}
std::string smfChunk(const std::vector<int>& bytes)
{
    std::string out = "MTrk";
    put32(out, static_cast<std::uint32_t>(bytes.size()));
    for (int b : bytes) out += static_cast<char>(b);
    return out;
}

pugi::xml_node trackNamed(const pugi::xml_document& doc, const char* name)
{
    return doc.child("Project").child("Structure").find_child_by_attribute("Track", "name", name);
}

pugi::xml_node clipOf(const pugi::xml_document& doc, const pugi::xml_node& track)
{
    const auto lanes = doc.child("Project").child("Arrangement").child("Lanes").find_child_by_attribute(
        "Lanes", "track", track.attribute("id").value());
    return lanes.child("Clips").child("Clip");
}

} // namespace

TEST_CASE("T-PRJ-10: XML text and numbers", "[project][dawproject][T-PRJ-10]")
{
    CHECK(xmlSafeText("Caf\xC3\xA9 \xE2\x98\x95") == "Caf\xC3\xA9 \xE2\x98\x95");
    CHECK(xmlSafeText("bad \xFF byte") == "bad \xEF\xBF\xBD byte");
    CHECK(xmlSafeText(std::string("a\x01" "b\tc\nd\x1F", 8)) == "ab\tc\nd");
    CHECK(xmlSafeText("x\xEF\xBF\xBEy") == "xy");  // U+FFFE
    CHECK(xmlNumber(0.5) == "0.5");
    CHECK(xmlNumber(-0.0) == "0");
    CHECK(xmlNumber(120) == "120");
    CHECK(std::stod(xmlNumber(1.0 / 3.0)) == 1.0 / 3.0);
    CHECK(xmlNumber(std::nan("")) == "0");
}

TEST_CASE("T-PRJ-10: a project exported as a .dawproject file", "[project][dawproject][T-PRJ-10]")
{
    TempDir dir;
    const unsigned int rate = 48000;
    const fs::path take = dir.path / "takes" / "take-20261114-120000";
    fs::create_directories(take);
    writeWav(take / "01-Vocal.wav", rate, 1, rate * 2);            // 2 s mono
    writeWav(take / "03-Synth sound.wav", rate, 2, rate * 3);      // 3 s stereo
    const fs::path older = dir.path / "takes" / "take-20261114-110000";  // another recording
    fs::create_directories(older);
    writeWav(older / "04-Shifted.wav", 44100, 2, 44100);           // 1 s at another rate
    SmfTrack keys;
    keys.events = {ev(0, 0x90, 60, 100),        ev(rate / 2, 0x80, 60, 40),  // 0.0 .. 0.5 s
                   ev(rate / 2, 0xB0, 1, 64),   ev(rate, 0x91, 64, 127),     // a controller; channel 2
                   ev(rate, 0x90, 67, 80),      ev(rate * 3 / 2, 0x90, 67, 0),  // velocity 0 = off
                   ev(rate * 3 / 2, 0xE0, 0, 64)};  // pitch bend; 64 on channel 2 still held at the end
    std::string err;
    REQUIRE(writeSmf(take / "02-Keys.mid", "take", {keys}, rate, MidiTiming::Ppq, rate * 2, false, err));
    RecordingNotes notes;
    notes.name = "Chorus";
    notes.note = "first try\nsecond line";
    notes.marks = {{0.75, "good part"}, {1.5, "Caf\xC3\xA9"}};
    REQUIRE(saveRecordingNotes(take, notes, err));

    ProjectData p;
    p.sampleRate = rate;
    p.note = "A note for the whole project";
    Track vocal;
    vocal.number = 1;
    vocal.name = "Vocal \xFF";  // a byte that is not UTF-8
    vocal.channels = {0};
    vocal.take = take / "01-Vocal.wav";
    vocal.takeStart = rate;  // at 1 s
    vocal.volumeDb = -6.0f;
    vocal.solo = true;
    Track synth;
    synth.number = 2;
    synth.name = "Keys";
    synth.kind = TrackKind::Midi;
    synth.take = take / "02-Keys.mid";
    synth.takeStart = rate;
    synth.instrumentClass = "0123456789ABCDEF0123456789ABCDEF";
    synth.instrumentName = "Test Synth";
    synth.instrumentState = std::string("COMP\0STATE", 10);
    synth.instrumentControllerState = "CTRL";
    Track sound;
    sound.number = 3;
    sound.name = "Keys sound";
    sound.channels = {0};  // as addLinkedSoundTrack makes it; its WAV is stereo
    sound.instrumentOf = 2;
    sound.take = take / "03-Synth sound.wav";
    sound.takeStart = rate;
    Track shifted;
    shifted.number = 4;
    shifted.name = "Shifted";
    shifted.channels = {0, 1};
    shifted.take = older / "04-Shifted.wav";
    shifted.takeStart = -static_cast<std::int64_t>(rate / 4);  // starts 0.25 s before the timeline
    shifted.mute = true;
    shifted.volumeDb = kMinVolumeDb;
    Track gone;
    gone.number = 5;
    gone.name = "Gone";
    gone.take = take / "05-missing.wav";
    Track port;
    port.number = 6;
    port.name = "To the synth";
    port.kind = TrackKind::Midi;
    port.midiOut = "Roland";
    p.tracks = {vocal, synth, sound, shifted, gone, port};

    const fs::path out = dir.path / "Song.dawproject";
    const DawExportResult r = exportDawProject(p, rate, "Song", "0.1.0", out);
    REQUIRE(r.ok);
    CHECK(r.error.empty());
    CHECK(r.tracks == 6);
    CHECK(r.audioClips == 3);
    CHECK(r.noteClips == 1);
    CHECK(r.notes == 3);
    CHECK(r.markers == 2);
    CHECK(r.instruments == 1);
    REQUIRE(r.leftOut.size() == 3);
    CHECK(r.leftOut[0].find("Track 2 \"Keys\": 2 MIDI message(s) other than notes") == 0);
    CHECK(r.leftOut[1].find("Track 5 \"Gone\": its take") == 0);
    CHECK(r.leftOut[2].find("Track 6 \"To the synth\": its MIDI output \"Roland\"") == 0);
    CHECK_FALSE(fs::exists(dir.path / "Song.dawproject.tmp"));

    const auto files = readZip(out);
    REQUIRE(files.count("project.xml") == 1);
    REQUIRE(files.count("metadata.xml") == 1);
    CHECK(files.size() == 2 + 3 + 1);  // three WAVs, one preset
    // The WAV is stored as it is.
    std::ifstream wavIn(take / "01-Vocal.wav", std::ios::binary);
    const std::string wavBytes((std::istreambuf_iterator<char>(wavIn)), std::istreambuf_iterator<char>());
    CHECK(files.at("audio/take-20261114-120000 01-Vocal.wav") == wavBytes);

    keepForXsd(out, "song");

    pugi::xml_document doc;
    REQUIRE(doc.load_string(files.at("project.xml").c_str()));
    const pugi::xml_node project = doc.child("Project");
    CHECK(std::string(project.attribute("version").value()) == "1.0");
    CHECK(std::string(project.child("Application").attribute("name").value()) == "mini-daw");
    CHECK(project.child("Transport").child("Tempo").attribute("value").as_double() == 120.0);
    CHECK(std::string(project.child("Arrangement").child("Lanes").attribute("timeUnit").value()) == "seconds");

    // Master first; every track goes to it.
    const pugi::xml_node master = project.child("Structure").child("Track");
    CHECK(std::string(master.child("Channel").attribute("role").value()) == "master");
    const std::string masterId = master.child("Channel").attribute("id").value();

    const pugi::xml_node v = trackNamed(doc, "Vocal \xEF\xBF\xBD");
    REQUIRE(v);
    CHECK(std::string(v.attribute("contentType").value()) == "audio");
    const pugi::xml_node vch = v.child("Channel");
    CHECK(vch.attribute("destination").value() == masterId);
    CHECK(vch.attribute("audioChannels").as_int() == 1);
    CHECK(vch.attribute("solo").as_bool());
    CHECK(std::abs(vch.child("Volume").attribute("value").as_double() - 0.501187) < 1e-5);  // -6 dB
    CHECK_FALSE(vch.child("Mute").attribute("value").as_bool());
    const pugi::xml_node vclip = clipOf(doc, v);
    CHECK(vclip.attribute("time").as_double() == 1.0);
    CHECK(vclip.attribute("duration").as_double() == 2.0);
    CHECK(std::string(vclip.attribute("name").value()) == "Chorus");
    CHECK(std::string(vclip.attribute("comment").value()) == "first try\nsecond line");
    const pugi::xml_node audio = vclip.child("Audio");
    CHECK(audio.attribute("channels").as_int() == 1);
    CHECK(audio.attribute("sampleRate").as_int() == 48000);
    CHECK(std::string(audio.child("File").attribute("path").value()) == "audio/take-20261114-120000 01-Vocal.wav");

    // The MIDI track: notes in seconds from the clip's start, the instrument with its state.
    const pugi::xml_node k = trackNamed(doc, "Keys");
    REQUIRE(k);
    CHECK(std::string(k.attribute("contentType").value()) == "notes");
    CHECK_FALSE(k.child("Channel").child("Mute").attribute("value").as_bool());  // the instrument plays the take
    const pugi::xml_node plugin = k.child("Channel").child("Devices").child("Vst3Plugin");
    REQUIRE(plugin);
    CHECK(std::string(plugin.attribute("deviceID").value()) == "01234567-89AB-CDEF-0123-456789ABCDEF");  // CC2
    CHECK(std::string(plugin.attribute("deviceName").value()) == "Test Synth");
    CHECK(std::string(plugin.attribute("deviceRole").value()) == "instrument");
    const std::string statePath = plugin.child("State").attribute("path").value();
    REQUIRE(files.count(statePath) == 1);
    const auto preset = decodeVstPreset(files.at(statePath), err);
    REQUIRE(preset);
    CHECK(preset->classId == "0123456789ABCDEF0123456789ABCDEF");
    CHECK(preset->component == std::string("COMP\0STATE", 10));
    CHECK(preset->controller == "CTRL");
    const pugi::xml_node kclip = clipOf(doc, k);
    CHECK(kclip.attribute("time").as_double() == 1.0);
    CHECK(kclip.attribute("duration").as_double() == 2.0);  // the file's end
    std::vector<pugi::xml_node> ns;
    for (pugi::xml_node n : kclip.child("Notes").children("Note")) ns.push_back(n);
    REQUIRE(ns.size() == 3);
    CHECK(ns[0].attribute("key").as_int() == 60);
    CHECK(ns[0].attribute("time").as_double() == 0.0);
    CHECK(ns[0].attribute("duration").as_double() == 0.5);
    CHECK(std::abs(ns[0].attribute("vel").as_double() - 100.0 / 127) < 1e-9);
    CHECK(std::abs(ns[0].attribute("rel").as_double() - 40.0 / 127) < 1e-9);
    CHECK(ns[1].attribute("time").as_double() == 1.0);  // 64 on channel 2 and 67, both at 1 s
    CHECK(ns[2].attribute("time").as_double() == 1.0);
    for (const auto& n : ns) {
        if (n.attribute("key").as_int() == 64) {
            CHECK(n.attribute("channel").as_int() == 1);
            CHECK(n.attribute("duration").as_double() == 1.0);  // held to the end (2 s)
        } else if (n.attribute("key").as_int() == 67) {
            CHECK(n.attribute("duration").as_double() == 0.5);
        }
    }

    // The linked sound track: muted, since the instrument plays the take.
    const pugi::xml_node s = trackNamed(doc, "Keys sound");
    REQUIRE(s);
    CHECK(s.child("Channel").child("Mute").attribute("value").as_bool());
    CHECK(std::string(s.attribute("comment").value()) == "The sound of track 2's instrument");
    CHECK(s.child("Channel").attribute("audioChannels").as_int() == 2);  // the WAV's, not the input's (CC3)

    // Shifted before the start: the clip at 0, its first 0.25 s skipped; silent volume, muted.
    const pugi::xml_node sh = trackNamed(doc, "Shifted");
    const pugi::xml_node shclip = clipOf(doc, sh);
    CHECK(shclip.attribute("time").as_double() == 0.0);
    CHECK(shclip.attribute("playStart").as_double() == 0.25);
    CHECK(shclip.attribute("duration").as_double() == 0.75);
    CHECK(shclip.child("Audio").attribute("sampleRate").as_int() == 44100);
    CHECK(sh.child("Channel").child("Mute").attribute("value").as_bool());
    CHECK(sh.child("Channel").child("Volume").attribute("value").as_double() == 0.0);

    // No clip for the missing take; the MIDI-output track is there, without a device.
    CHECK_FALSE(clipOf(doc, trackNamed(doc, "Gone")));
    CHECK_FALSE(trackNamed(doc, "To the synth").child("Channel").child("Devices"));

    // The marks of the recording, once, on the timeline (it starts at 1 s).
    std::vector<pugi::xml_node> marks;
    for (pugi::xml_node m : project.child("Arrangement").child("Markers").children("Marker")) marks.push_back(m);
    REQUIRE(marks.size() == 2);
    CHECK(marks[0].attribute("time").as_double() == 1.75);
    CHECK(std::string(marks[0].attribute("name").value()) == "good part");
    CHECK(marks[1].attribute("time").as_double() == 2.5);

    pugi::xml_document meta;
    REQUIRE(meta.load_string(files.at("metadata.xml").c_str()));
    CHECK(std::string(meta.child("MetaData").child_value("Title")) == "Song");
    CHECK(std::string(meta.child("MetaData").child_value("Comment")) == "A note for the whole project");

    // Sound from the recording: the linked track plays, the MIDI track is muted.
    p.tracks[1].soundFromRecording = true;
    REQUIRE(exportDawProject(p, rate, "Song", "0.1.0", out).ok);  // replaces the file
    pugi::xml_document again;
    REQUIRE(again.load_string(readZip(out).at("project.xml").c_str()));
    CHECK(trackNamed(again, "Keys").child("Channel").child("Mute").attribute("value").as_bool());
    CHECK_FALSE(trackNamed(again, "Keys sound").child("Channel").child("Mute").attribute("value").as_bool());
    keepForXsd(out, "song-sound-from-recording");
}

TEST_CASE("T-PRJ-10: a linked sound track muted and soloed as mini-daw plays it", "[project][dawproject][T-PRJ-10]")
{
    // Crosscheck #22 CC1: the MIDI track's mute silences its linked track, its solo lets it sound.
    TempDir dir;
    const unsigned int rate = 48000;
    const fs::path take = dir.path / "take-1";
    fs::create_directories(take);
    writeWav(take / "02-Keys sound.wav", rate, 2, rate);
    ProjectData p;
    p.sampleRate = rate;
    Track keys;
    keys.number = 1;
    keys.name = "Keys";
    keys.kind = TrackKind::Midi;
    keys.instrumentClass = "0123456789ABCDEF0123456789ABCDEF";
    keys.instrumentName = "Synth";
    Track sound;
    sound.number = 2;
    sound.name = "Keys sound";
    sound.channels = {0};
    sound.instrumentOf = 1;
    sound.take = take / "02-Keys sound.wav";
    Track other;
    other.number = 3;
    other.name = "Other";
    const fs::path out = dir.path / "x.dawproject";
    auto exported = [&](bool fromRecording, bool mute, bool solo) {
        p.tracks = {keys, sound, other};
        p.tracks[0].soundFromRecording = fromRecording;
        p.tracks[0].mute = mute;
        p.tracks[0].solo = solo;
        REQUIRE(exportDawProject(p, rate, "x", "0.1.0", out).ok);
        return projectXml(out);
    };
    {
        INFO("sound from the recording, MIDI track muted: nothing sounds");
        const auto doc = exported(true, true, false);
        CHECK(muted(trackNamed(doc, "Keys")));
        CHECK(muted(trackNamed(doc, "Keys sound")));
    }
    {
        INFO("sound from the recording, MIDI track soloed: the recording sounds, soloed");
        const auto doc = exported(true, false, true);
        CHECK(muted(trackNamed(doc, "Keys")));
        CHECK_FALSE(muted(trackNamed(doc, "Keys sound")));
        CHECK(soloed(trackNamed(doc, "Keys sound")));
        CHECK_FALSE(soloed(trackNamed(doc, "Other")));
        keepForXsd(out, "linked-solo");
    }
    {
        INFO("sound from the instrument, MIDI track soloed: the instrument sounds, the recording stays muted");
        const auto doc = exported(false, false, true);
        CHECK_FALSE(muted(trackNamed(doc, "Keys")));
        CHECK(soloed(trackNamed(doc, "Keys")));
        CHECK(muted(trackNamed(doc, "Keys sound")));
    }
    {
        INFO("nothing set: the instrument plays, the recording is muted, nothing soloed");
        const auto doc = exported(false, false, false);
        CHECK_FALSE(muted(trackNamed(doc, "Keys")));
        CHECK(muted(trackNamed(doc, "Keys sound")));
        CHECK_FALSE(soloed(trackNamed(doc, "Keys sound")));
        // A linked track's channel is stereo also without a take (CC3).
        p.tracks[1].take.clear();
        REQUIRE(exportDawProject(p, rate, "x", "0.1.0", out).ok);
        CHECK(trackNamed(projectXml(out), "Keys sound").child("Channel").attribute("audioChannels").as_int() == 2);
    }
}

TEST_CASE("T-PRJ-10: notes from a MIDI file with a tempo change and several tracks", "[project][dawproject][T-PRJ-10]")
{
    // Crosscheck #22 CC5 / CC8: overlapping notes of one key (first in, first out),
    // a note-off without its note, a note of length 0, a tempo change, two tracks.
    TempDir dir;
    const unsigned int rate = 48000;
    const fs::path take = dir.path / "take-1";
    fs::create_directories(take);
    std::string smf = "MThd";
    put32(smf, 6);
    smf += std::string("\x00\x01\x00\x03\x01\xE0", 6);  // format 1, 3 tracks, 480 ticks per quarter
    // 120 bpm (1 tick = 1/960 s), from tick 960 (1 s) 240 bpm (1 tick = 1/1920 s).
    smf += smfChunk({0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, 0x87, 0x40, 0xFF, 0x51, 0x03, 0x03, 0xD0, 0x90,
                     0x00, 0xFF, 0x2F, 0x00});
    smf += smfChunk({0x00, 0x90, 60, 100,        // 0 s: 60 on
                     0x81, 0x70, 0x90, 60, 50,   // 0.25 s: 60 on again (overlapping)
                     0x81, 0x70, 0x80, 60, 10,   // 0.5 s: off, ends the first
                     0x81, 0x70, 0x80, 60, 20,   // 0.75 s: off, ends the second
                     0x00, 0x80, 62, 64,         // a note-off without its note
                     0x00, 0x90, 64, 90,         // on and off in the same frame
                     0x00, 0x80, 64, 64,
                     0x83, 0x60, 0x90, 67, 100,  // tick 1200 = 1.125 s
                     0x81, 0x70, 0x80, 67, 64,   // tick 1440 = 1.25 s
                     0x00, 0xFF, 0x2F, 0x00});
    smf += smfChunk({0x00, 0x91, 72, 100, 0x87, 0x40, 0x81, 72, 64, 0x00, 0xFF, 0x2F, 0x00});  // 0 .. 1 s, channel 2
    std::ofstream(take / "01-Keys.mid", std::ios::binary) << smf;

    ProjectData p;
    p.sampleRate = rate;
    Track keys;
    keys.number = 1;
    keys.name = "Keys";
    keys.kind = TrackKind::Midi;
    keys.take = take / "01-Keys.mid";
    p.tracks = {keys};
    const fs::path out = dir.path / "notes.dawproject";
    const DawExportResult r = exportDawProject(p, rate, "notes", "0.1.0", out);
    REQUIRE(r.ok);
    CHECK(r.notes == 5);
    CHECK(r.leftOut.empty());  // tempo changes are followed, nothing else in the file
    keepForXsd(out, "notes");
    const auto doc = projectXml(out);
    const pugi::xml_node clip = clipOf(doc, trackNamed(doc, "Keys"));
    CHECK(clip.attribute("duration").as_double() == 1.25);
    std::multimap<int, pugi::xml_node> byKey;
    for (pugi::xml_node n : clip.child("Notes").children("Note")) byKey.emplace(n.attribute("key").as_int(), n);
    REQUIRE(byKey.count(60) == 2);
    auto first = byKey.find(60);
    auto second = std::next(first);
    if (first->second.attribute("time").as_double() > second->second.attribute("time").as_double()) std::swap(first, second);
    CHECK(first->second.attribute("time").as_double() == 0.0);
    CHECK(first->second.attribute("duration").as_double() == 0.5);
    CHECK(std::abs(first->second.attribute("vel").as_double() - 100.0 / 127) < 1e-9);
    CHECK(std::abs(first->second.attribute("rel").as_double() - 10.0 / 127) < 1e-9);
    CHECK(second->second.attribute("time").as_double() == 0.25);
    CHECK(second->second.attribute("duration").as_double() == 0.5);
    CHECK(std::abs(second->second.attribute("vel").as_double() - 50.0 / 127) < 1e-9);
    CHECK(byKey.count(62) == 0);
    REQUIRE(byKey.count(64) == 1);
    CHECK(byKey.find(64)->second.attribute("time").as_double() == 0.75);
    CHECK(byKey.find(64)->second.attribute("duration").as_double() == 1.0 / rate);  // one frame, not 0
    REQUIRE(byKey.count(67) == 1);
    CHECK(byKey.find(67)->second.attribute("time").as_double() == 1.125);  // after the tempo change
    CHECK(byKey.find(67)->second.attribute("duration").as_double() == 0.125);
    REQUIRE(byKey.count(72) == 1);
    CHECK(byKey.find(72)->second.attribute("channel").as_int() == 1);
    CHECK(byKey.find(72)->second.attribute("duration").as_double() == 1.0);
}

TEST_CASE("T-PRJ-10: takes with the same name, one take on two tracks, marks", "[project][dawproject][T-PRJ-10]")
{
    // Crosscheck #22 CC8 (file names in the ZIP) and CC4 (marks from the recording's earliest take).
    TempDir dir;
    const unsigned int rate = 48000;
    const fs::path a = dir.path / "takes" / "take-1";
    const fs::path b = dir.path / "auto" / "take-1";  // the same folder name elsewhere
    fs::create_directories(a);
    fs::create_directories(b);
    writeWav(a / "01-Mic.wav", rate, 1, rate);
    writeWav(b / "01-Mic.wav", rate, 1, rate / 2);
    writeWav(a / "02-Mic.wav", rate, 1, rate);
    RecordingNotes notes;
    notes.marks = {{0.5, "here"}};
    std::string err;
    REQUIRE(saveRecordingNotes(a, notes, err));
    ProjectData p;
    p.sampleRate = rate;
    Track t1;
    t1.number = 1;
    t1.name = "One";
    t1.channels = {0};
    t1.take = a / "01-Mic.wav";
    t1.takeStart = rate;  // 1 s
    Track t2 = t1;
    t2.number = 2;
    t2.name = "Two";
    t2.take = b / "01-Mic.wav";
    t2.takeStart = 0;
    Track t3 = t1;
    t3.number = 3;
    t3.name = "Three";  // the same take as track 1
    Track t4 = t1;
    t4.number = 4;
    t4.name = "Four";
    t4.take = a / "02-Mic.wav";
    t4.takeStart = rate - rate / 20;  // 0.95 s: the same recording, moved earlier (latency correction)
    p.tracks = {t1, t2, t3, t4};
    const fs::path out = dir.path / "names.dawproject";
    const DawExportResult r = exportDawProject(p, rate, "names", "0.1.0", out);
    REQUIRE(r.ok);
    CHECK(r.audioClips == 4);
    keepForXsd(out, "names");
    const auto files = readZip(out);
    CHECK(files.size() == 2 + 3);  // three different WAVs
    CHECK(files.count("audio/take-1 01-Mic.wav") == 1);
    CHECK(files.count("audio/take-1 01-Mic-2.wav") == 1);
    const auto doc = projectXml(out);
    auto fileOf = [&](const char* name) {
        return std::string(clipOf(doc, trackNamed(doc, name)).child("Audio").child("File").attribute("path").value());
    };
    CHECK(fileOf("One") == "audio/take-1 01-Mic.wav");
    CHECK(fileOf("Two") == "audio/take-1 01-Mic-2.wav");
    CHECK(fileOf("Three") == fileOf("One"));
    CHECK(files.at(fileOf("Two")).size() < files.at(fileOf("One")).size());  // the shorter one
    // The mark: 0.5 s after the recording's earliest take (0.95 s), not after track 1's.
    const pugi::xml_node m = doc.child("Project").child("Arrangement").child("Markers").child("Marker");
    REQUIRE(m);
    CHECK(std::abs(m.attribute("time").as_double() - 1.45) < 1e-12);
    CHECK_FALSE(m.next_sibling("Marker"));  // once per recording
}

TEST_CASE("T-PRJ-10: names and the VST3 device ID", "[project][dawproject][T-PRJ-10]")
{
    // Crosscheck #22 CC2 and CC6.
    CHECK(vst3DeviceId("0123456789abcdef0123456789ABCDEF") == "01234567-89AB-CDEF-0123-456789ABCDEF");
    CHECK(vst3DeviceId("not a class id") == "not a class id");
    CHECK(vst3DeviceId("0123456789ABCDEF0123456789ABCDEG") == "0123456789ABCDEF0123456789ABCDEG");
    CHECK(dawExportName("  Song  ") == "Song");
    CHECK(dawExportName("Song.dawproject") == "Song");
    CHECK(dawExportName("Song .DAWproject ") == "Song");
    CHECK(dawExportName("   ") == "");
    CHECK(dawExportName(".dawproject") == "");
    CHECK(dawExportFileName("Song.dawproject") == "Song.dawproject");
    CHECK(dawExportFileName(" ") == "");
    CHECK(dawExportFileName("AC/DC") == "AC_DC.dawproject");
    CHECK(dawExportFileName("CON").front() == '_');  // a Windows device name
}

TEST_CASE("T-PRJ-10: an export that cannot be written leaves nothing", "[project][dawproject][T-PRJ-10]")
{
    TempDir dir;
    ProjectData p;
    const DawExportResult r = exportDawProject(p, 48000, "x", "0.1.0", dir.path / "no-such-folder" / "x.dawproject");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("cannot write") == 0);
    CHECK_FALSE(fs::exists(dir.path / "no-such-folder"));
    // An empty project is still a valid file (only the master track).
    const DawExportResult e = exportDawProject(p, 0, "", "0.1.0", dir.path / "empty.dawproject");
    REQUIRE(e.ok);
    CHECK(e.tracks == 0);
    pugi::xml_document doc;
    REQUIRE(doc.load_string(readZip(dir.path / "empty.dawproject").at("project.xml").c_str()));
    CHECK(doc.child("Project").child("Structure").child("Track"));
}

TEST_CASE("T-PRJ-10: auto record parts that no track plays on an \"Auto record\" track", "[project][dawproject][T-PRJ-10]")
{
    // User 2026-11-16: "one Auto record track": muted, a lane per file, every clip at 0 s.
    TempDir dir;
    const unsigned int rate = 48000;
    const fs::path base = dir.path / "takes";
    const fs::path a = base / "auto" / "2026-11-16 10-00-00";
    const fs::path b = base / "auto" / "2026-11-16 10-05-00";
    const fs::path bad = base / "auto" / "2026-11-16 10-09-00";
    fs::create_directories(a);
    fs::create_directories(b);
    fs::create_directories(bad);
    writeWav(a / "01-Mic.wav", rate, 1, rate);
    SmfTrack keys;
    keys.events = {ev(0, 0x90, 60, 100), ev(rate / 2, 0x80, 60, 0)};
    std::string err;
    REQUIRE(writeSmf(a / "02-Keys.mid", "take", {keys}, rate, MidiTiming::Ppq, rate, false, err));
    RecordingNotes notes;
    notes.name = "Idea";
    notes.note = "the good riff";
    notes.marks = {{0.25, "here"}};
    REQUIRE(saveRecordingNotes(a, notes, err));
    writeWav(b / "01-Mic.wav", rate, 2, rate * 2);
    std::ofstream(bad / "01-Mic.wav", std::ios::binary) << "not a wav file";
    const std::vector<RecordingInfo> parts = listRecordings(base);
    REQUIRE(parts.size() == 3);
    for (const auto& r : parts) CHECK(r.autoPart);

    ProjectData p;
    p.sampleRate = rate;
    Track mic;
    mic.number = 1;
    mic.name = "Mic";
    mic.channels = {0};
    p.tracks = {mic};
    const fs::path out = dir.path / "auto.dawproject";
    const DawExportResult r = exportDawProject(p, rate, "auto", "0.1.0", out, parts);
    REQUIRE(r.ok);
    CHECK(r.tracks == 1);  // the project's own; the Auto record track is counted as parts
    CHECK(r.autoRecordParts == 2);
    CHECK(r.audioClips == 2);
    CHECK(r.noteClips == 1);
    CHECK(r.markers == 0);  // all parts start at 0: their marks are not markers
    REQUIRE(r.leftOut.size() == 1);
    CHECK(r.leftOut[0].find("Auto record part 2026-11-16 10-09-00 01-Mic.wav: its take could not be read") == 0);
    keepForXsd(out, "auto");

    const auto doc = projectXml(out);
    const pugi::xml_node track = trackNamed(doc, "Auto record");
    REQUIRE(track);
    CHECK(std::string(track.attribute("contentType").value()) == "audio notes");
    CHECK(muted(track));
    CHECK_FALSE(track.child("Channel").child("Devices"));
    const pugi::xml_node trackLanes = doc.child("Project").child("Arrangement").child("Lanes").find_child_by_attribute(
        "Lanes", "track", track.attribute("id").value());
    std::vector<pugi::xml_node> lanes;
    for (pugi::xml_node l : trackLanes.children("Lanes")) lanes.push_back(l);
    REQUIRE(lanes.size() == 3);  // two files of the first part, one of the second
    CHECK(std::string(lanes[0].attribute("name").value()) == "2026-11-16 10-00-00 01-Mic");
    CHECK(std::string(lanes[1].attribute("name").value()) == "2026-11-16 10-00-00 02-Keys");
    CHECK(std::string(lanes[2].attribute("name").value()) == "2026-11-16 10-05-00 01-Mic");
    for (const auto& l : lanes) {
        const pugi::xml_node clip = l.child("Clips").child("Clip");
        REQUIRE(clip);
        CHECK(clip.attribute("time").as_double() == 0.0);
    }
    const pugi::xml_node first = lanes[0].child("Clips").child("Clip");
    CHECK(std::string(first.attribute("name").value()) == "Idea");
    CHECK(std::string(first.attribute("comment").value()) == "the good riff");
    CHECK(lanes[1].child("Clips").child("Clip").child("Notes").child("Note"));
    CHECK(lanes[2].child("Clips").child("Clip").attribute("duration").as_double() == 2.0);
    CHECK(std::string(lanes[2].child("Clips").child("Clip").attribute("name").value()) == "2026-11-16 10-05-00");

    // Only unreadable parts: no empty track.
    const DawExportResult none = exportDawProject(p, rate, "auto", "0.1.0", out, {parts[2]});
    REQUIRE(none.ok);
    CHECK(none.autoRecordParts == 0);
    CHECK_FALSE(trackNamed(projectXml(out), "Auto record"));
    // Without parts, as before.
    CHECK_FALSE(trackNamed(projectXml((exportDawProject(p, rate, "auto", "0.1.0", out), out)), "Auto record"));
}
