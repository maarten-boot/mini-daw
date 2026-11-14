// T-PRJ-10: DAWproject export (P27, SPEC-PRJ-04). The file is read back with
// miniz and pugixml; when the environment variable MINIDAW_DAWPROJECT_OUT names
// a folder, project.xml and metadata.xml are also written there, where the
// ctest "T-PRJ-10: XSD" checks them against
// the format's schemas (tests/dawproject/*.xsd) with xmllint.

#include "minidaw/dawproject.hpp"
#include "minidaw/presets.hpp"
#include "minidaw/recording_notes.hpp"
#include "minidaw/smf.hpp"
#include "minidaw/wav.hpp"

#include <catch2/catch_test_macros.hpp>
#include <miniz.h>
#include <pugixml.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
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
    writeWav(take / "04-Shifted.wav", 44100, 2, 44100);            // 1 s at another rate
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
    sound.channels = {};
    sound.instrumentOf = 2;
    sound.take = take / "03-Synth sound.wav";
    sound.takeStart = rate;
    Track shifted;
    shifted.number = 4;
    shifted.name = "Shifted";
    shifted.channels = {0, 1};
    shifted.take = take / "04-Shifted.wav";
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

    // For the schema check (ctest "T-PRJ-10: XSD").
    if (const char* keep = std::getenv("MINIDAW_DAWPROJECT_OUT"); keep && *keep) {
        fs::create_directories(keep);
        std::ofstream(fs::path(keep) / "project.xml", std::ios::binary) << files.at("project.xml");
        std::ofstream(fs::path(keep) / "metadata.xml", std::ios::binary) << files.at("metadata.xml");
    }

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
    CHECK(std::string(plugin.attribute("deviceID").value()) == "0123456789ABCDEF0123456789ABCDEF");
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
    CHECK(s.child("Channel").attribute("audioChannels").as_int() == 2);

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
