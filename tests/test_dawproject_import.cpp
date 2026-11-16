// T-PRJ-11: DAWproject import (P28, SPEC-PRJ-05).

#include "minidaw/dawproject.hpp"
#include "minidaw/presets.hpp"
#include "minidaw/project.hpp"
#include "minidaw/recording_notes.hpp"
#include "minidaw/sinc_kernel.hpp"
#include "minidaw/smf.hpp"
#include "minidaw/wav.hpp"
#include "minidaw/wav_reader.hpp"

#include <catch2/catch_test_macros.hpp>
#include <miniz.h>

#include <cmath>
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

constexpr double kPi = 3.14159265358979323846;

struct TempDir {
    fs::path path;
    TempDir()
    {
        std::random_device rd;
        path = fs::temp_directory_path() / ("minidaw-dawimport-" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// A sine (or, with freq 0, a ramp of frame numbers) as a WAV.
void writeWav(const fs::path& path, unsigned int rate, unsigned int channels, std::size_t frames, double freq = 0.0)
{
    WavWriter w;
    std::string err;
    REQUIRE(w.open(path, rate, channels, err));
    std::vector<float> data(frames * channels);
    for (std::size_t i = 0; i < frames; ++i) {
        const float v = freq > 0.0 ? static_cast<float>(0.5 * std::sin(2 * kPi * freq * static_cast<double>(i) / rate))
                                   : static_cast<float>(static_cast<double>(i % 1000) / 2000.0);
        for (unsigned int c = 0; c < channels; ++c) data[i * channels + c] = c == 0 ? v : -v;
    }
    REQUIRE(w.write(data.data(), frames, err));
    REQUIRE(w.close(err));
}

std::vector<float> readWav(const fs::path& path, unsigned int& rate, unsigned int& channels)
{
    WavReader r;
    std::string err;
    REQUIRE(r.open(path, err));
    rate = r.sampleRate();
    channels = r.channels();
    std::vector<float> out(static_cast<std::size_t>(r.frames()) * channels);
    REQUIRE(r.read(out.data(), static_cast<std::size_t>(r.frames())) == r.frames());
    return out;
}

void writeZip(const fs::path& file, const std::map<std::string, std::string>& entries)
{
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    REQUIRE(mz_zip_writer_init_file(&zip, file.string().c_str(), 0));
    for (const auto& [name, bytes] : entries) {
        REQUIRE(mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_LEVEL));
    }
    REQUIRE(mz_zip_writer_finalize_archive(&zip));
    REQUIRE(mz_zip_writer_end(&zip));
}

std::string fileBytes(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

ProjectData loadImported(const fs::path& folder)
{
    ProjectData d;
    std::vector<std::string> warnings;
    std::string err;
    REQUIRE(loadProject(folder, d, warnings, err));
    CHECK(warnings.empty());
    return d;
}

const Track& trackNamed(const ProjectData& d, const char* name)
{
    for (const Track& t : d.tracks) {
        if (t.name == name) return t;
    }
    FAIL("no track " << name);
    return d.tracks.front();
}

struct NoteSeen {
    std::uint64_t on = 0;
    std::uint64_t off = 0;
    int channel = 0;
    int key = 0;
    int vel = 0;
};

std::vector<NoteSeen> notesOf(const fs::path& mid, unsigned int rate)
{
    SmfData smf;
    std::string err;
    REQUIRE(readSmf(mid, smf, err));
    std::vector<NoteSeen> out;
    std::map<std::pair<int, int>, std::size_t> open;
    for (const MidiEvent& e : smfToFrames(smf, rate)) {
        const int status = e.data[0] & 0xF0;
        const int ch = e.data[0] & 0x0F;
        if (status == 0x90 && e.data[2] > 0) {
            open[{ch, e.data[1]}] = out.size();
            out.push_back({e.frame, 0, ch, e.data[1], e.data[2]});
        } else if (status == 0x80 || (status == 0x90 && e.data[2] == 0)) {
            auto it = open.find({ch, e.data[1]});
            if (it != open.end()) {
                out[it->second].off = e.frame;
                open.erase(it);
            }
        }
    }
    return out;
}

bool hasLine(const DawImportResult& r, const std::string& text)
{
    for (const auto& l : r.leftOut) {
        if (l.find(text) != std::string::npos) return true;
    }
    return false;
}

} // namespace

TEST_CASE("T-PRJ-11: VST3 device IDs", "[project][dawimport][T-PRJ-11]")
{
    CHECK(vst3ClassFromDeviceId("01234567-89ab-CDEF-0123-456789ABCDEF") == "0123456789ABCDEF0123456789ABCDEF");
    CHECK(vst3ClassFromDeviceId("{01234567-89AB-CDEF-0123-456789ABCDEF}") == "0123456789ABCDEF0123456789ABCDEF");
    CHECK(vst3ClassFromDeviceId("0123456789ABCDEF0123456789ABCDEF") == "0123456789ABCDEF0123456789ABCDEF");
    CHECK(vst3ClassFromDeviceId("1234").empty());
    CHECK(vst3ClassFromDeviceId("0123456789ABCDEF0123456789ABCDEX").empty());
}

TEST_CASE("T-PRJ-11: the sinc kernel converts a sine cleanly", "[project][dawimport][T-PRJ-11]")
{
    // 1 kHz at 44.1 kHz read at 48 kHz positions: within -60 dB of the true sine.
    const double inRate = 44100;
    const double outRate = 48000;
    std::vector<float> in(44100);
    for (std::size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float>(std::sin(2 * kPi * 1000.0 * static_cast<double>(i) / inRate));
    const SincKernel k(outRate / inRate);
    double worst = 0.0;
    for (int j = 4800; j < 43200; j += 7) {  // away from the ends
        const double pos = j * inRate / outRate;
        const auto base = static_cast<long>(std::floor(pos));
        double v = 0.0;
        for (long i = base - k.halfWidth() + 1; i <= base + k.halfWidth(); ++i) v += in[static_cast<std::size_t>(i)] * k.weight(pos - static_cast<double>(i));
        worst = std::max(worst, std::abs(v - std::sin(2 * kPi * 1000.0 * j / outRate)));
    }
    CHECK(worst < 1e-3);
    // Down: 18 kHz at 48 kHz is above the 44.1 kHz band and is removed.
    const SincKernel down(44100.0 / 48000.0);
    std::vector<float> hi(48000);
    for (std::size_t i = 0; i < hi.size(); ++i) hi[i] = static_cast<float>(std::sin(2 * kPi * 23000.0 * static_cast<double>(i) / 48000.0));
    double peak = 0.0;
    for (int j = 4410; j < 39690; j += 5) {
        const double pos = j * 48000.0 / 44100.0;
        const auto base = static_cast<long>(std::floor(pos));
        double v = 0.0;
        for (long i = base - down.halfWidth() + 1; i <= base + down.halfWidth(); ++i) v += hi[static_cast<std::size_t>(i)] * down.weight(pos - static_cast<double>(i));
        peak = std::max(peak, std::abs(v));
    }
    CHECK(peak < 0.001);  // 23 kHz does not fold back into the audible band (below -60 dB)
}

TEST_CASE("T-PRJ-11: mini-daw's own export comes back", "[project][dawimport][T-PRJ-11]")
{
    TempDir dir;
    const unsigned int rate = 48000;
    const fs::path take = dir.path / "src" / "takes" / "take-20261116-120000";
    fs::create_directories(take);
    writeWav(take / "01-Vocal.wav", rate, 1, rate * 2);
    writeWav(take / "03-Keys sound.wav", rate, 2, rate * 3);
    SmfTrack keys;
    MidiEvent a;
    a.frame = 0;
    a.data = {0x90, 60, 100};
    a.size = 3;
    MidiEvent b = a;
    b.frame = rate / 2;
    b.data = {0x80, 60, 40};
    MidiEvent c = a;
    c.frame = rate;
    c.data = {0x91, 64, 127};
    MidiEvent e = c;
    e.frame = rate * 3 / 2;
    e.data = {0x81, 64, 0};
    keys.events = {a, b, c, e};
    std::string err;
    REQUIRE(writeSmf(take / "02-Keys.mid", "take", {keys}, rate, MidiTiming::Ppq, rate * 2, false, err));
    RecordingNotes notes;
    notes.name = "Chorus";
    notes.marks = {{0.5, "good"}};
    REQUIRE(saveRecordingNotes(take, notes, err));

    ProjectData p;
    p.sampleRate = rate;
    p.note = "the whole song";
    Track vocal;
    vocal.number = 1;
    vocal.name = "Vocal";
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
    sound.channels = {0};
    sound.instrumentOf = 2;
    sound.take = take / "03-Keys sound.wav";
    sound.takeStart = rate;
    Track cut;  // an overdub moved before the start: exported as a clip at 0 with playStart
    cut.number = 4;
    cut.name = "Cut";
    cut.channels = {0};
    cut.take = take / "01-Vocal.wav";
    cut.takeStart = -static_cast<std::int64_t>(rate / 4);
    cut.mute = true;
    p.tracks = {vocal, synth, sound, cut};
    const fs::path file = dir.path / "Song.dawproject";
    REQUIRE(exportDawProject(p, rate, "Song", "0.1.0", file).ok);

    const fs::path folder = dir.path / "projects" / "Song again";
    const DawImportResult r = importDawProject(file, folder, rate, "take-20261116-130000");
    REQUIRE(r.ok);
    CHECK(r.error.empty());
    CHECK(r.application == "mini-daw 0.1.0");
    CHECK(r.title == "Song");
    CHECK(r.tracks == 4);
    CHECK(r.audioClips == 3);
    CHECK(r.noteClips == 1);
    CHECK(r.notes == 2);
    CHECK(r.instruments == 1);
    CHECK(r.markers == 1);
    CHECK(r.leftOut.empty());
    CHECK_FALSE(fs::exists(folder / "takes" / "take-20261116-130000" / ".unpacked"));

    const ProjectData d = loadImported(folder);
    CHECK(d.note == "the whole song");
    REQUIRE(d.tracks.size() == 4);
    const Track& v = trackNamed(d, "Vocal");
    CHECK(v.kind == TrackKind::Audio);
    CHECK(v.solo);
    CHECK(std::abs(v.volumeDb + 6.0f) < 0.01f);
    CHECK(v.takeStart == rate);
    CHECK(v.channels == std::vector<unsigned int>{0});
    unsigned int wr = 0;
    unsigned int wc = 0;
    const auto got = readWav(v.take, wr, wc);
    unsigned int orr = 0;
    unsigned int oc = 0;
    const auto orig = readWav(take / "01-Vocal.wav", orr, oc);
    CHECK(wr == rate);
    CHECK(wc == 1);
    CHECK(got == orig);  // the same rate on whole frames: unchanged

    const Track& k = trackNamed(d, "Keys");
    CHECK(k.kind == TrackKind::Midi);
    CHECK(k.instrumentClass == "0123456789ABCDEF0123456789ABCDEF");
    CHECK(k.instrumentName == "Test Synth");
    CHECK(k.instrumentState == std::string("COMP\0STATE", 10));
    CHECK(k.instrumentControllerState == "CTRL");
    CHECK(k.takeStart == rate);
    const auto ns = notesOf(k.take, rate);
    REQUIRE(ns.size() == 2);
    CHECK(ns[0].on == 0);
    CHECK(ns[0].off == rate / 2);
    CHECK(ns[0].key == 60);
    CHECK(ns[0].vel == 100);
    CHECK(ns[1].on == rate);
    CHECK(ns[1].channel == 1);
    CHECK(ns[1].vel == 127);

    // The linked track came out muted (the instrument plays the take): a muted audio track.
    const Track& s = trackNamed(d, "Keys sound");
    CHECK(s.mute);
    CHECK(s.channels == std::vector<unsigned int>{0, 1});

    // The cut take: at 0, its first 0.25 s gone.
    const Track& c2 = trackNamed(d, "Cut");
    CHECK(c2.mute);
    CHECK(c2.takeStart == 0);
    const auto cutGot = readWav(c2.take, wr, wc);
    REQUIRE(cutGot.size() == orig.size() - rate / 4);
    CHECK(std::equal(cutGot.begin(), cutGot.end(), orig.begin() + rate / 4));

    // The recording: named after the title, the mark at its place (0.5 s after the earliest take at 0).
    RecordingNotes back;
    REQUIRE(loadRecordingNotes(folder / "takes" / "take-20261116-130000", back, err));
    CHECK(back.name == "Song");
    CHECK(back.note.find("made with mini-daw 0.1.0") != std::string::npos);
    REQUIRE(back.marks.size() == 1);
    // Exported 0.5 s after the recording's earliest take (the cut one at -0.25 s), so at 0.25 s; that
    // take now starts at 0, the earliest: the mark is 0.25 s into the imported recording.
    CHECK(std::abs(back.marks[0].seconds - 0.25) < 1e-9);
    CHECK(back.marks[0].text == "good");
}

TEST_CASE("T-PRJ-11: a file as other programs write it", "[project][dawimport][T-PRJ-11]")
{
    TempDir dir;
    const unsigned int rate = 48000;
    // Audio: a 1 kHz sine at 44.1 kHz (converted), a ramp at 48 kHz (copied exactly).
    writeWav(dir.path / "sine.wav", 44100, 2, 44100, 1000.0);
    writeWav(dir.path / "ramp.wav", rate, 1, rate);
    writeWav(dir.path / "outside.wav", rate, 1, rate / 2);  // external, beside the file
    VstPresetData preset;
    preset.classId = "ABCDEF0123456789ABCDEF0123456789";
    preset.component = "SYNTHSTATE";
    // 150 bpm: one beat = 0.4 s. The arrangement in beats.
    const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Project version="1.0">
  <Application name="Some DAW" version="5.2"/>
  <Transport><Tempo unit="bpm" value="150" id="t"/></Transport>
  <Structure>
    <Track contentType="audio notes" id="m" name="Master"><Channel role="master" id="mc"><Volume unit="linear" value="0.5"/></Channel></Track>
    <Track contentType="tracks" id="g" name="Band">
      <Channel role="submix" id="gc"/>
      <Track contentType="notes" id="k" name="Keys">
        <Channel role="regular" id="kc" solo="false">
          <Devices>
            <Vst3Plugin deviceID="abcdef01-2345-6789-ABCD-EF0123456789" deviceName="Synth" deviceRole="instrument" id="p1"><State path="plugins/synth.vstpreset"/></Vst3Plugin>
            <ClapPlugin deviceID="com.x.reverb" deviceName="Reverb" deviceRole="audioFX" id="p2"/>
          </Devices>
          <Mute value="true"/>
          <Pan unit="normalized" value="0.2"/>
          <Volume unit="decibel" value="-12"/>
        </Channel>
      </Track>
      <Track contentType="audio" id="a" name="Guitar">
        <Channel role="regular" id="ac" solo="true"><Volume unit="linear" value="1"/><Sends><Send id="s1"/></Sends></Channel>
      </Track>
    </Track>
    <Track contentType="audio" id="fx" name="Reverb return"><Channel role="effect" id="fxc"/></Track>
    <Track contentType="audio" id="e" name="Empty"><Channel role="regular" id="ec"/></Track>
  </Structure>
  <Arrangement id="arr">
    <Lanes timeUnit="beats" id="l0">
      <Lanes track="k" id="l1">
        <Clips id="c1">
          <Clip time="4" duration="8" playStart="1" loopStart="0" loopEnd="2" name="riff">
            <Notes id="n1">
              <Note time="0" duration="0.5" key="60" vel="0.5" rel="0.5"/>
              <Note time="1" duration="2" key="62" vel="1"/>
              <Note time="3" duration="1" key="64" vel="1"/>
            </Notes>
          </Clip>
          <Clip time="20" duration="1" enable="false"><Notes><Note time="0" duration="1" key="70"/></Notes></Clip>
        </Clips>
        <Points id="auto1"><Target parameter="kc"/></Points>
      </Lanes>
      <Lanes track="a" id="l2">
        <Clips id="c2">
          <Clip time="0" duration="2.5" contentTimeUnit="seconds" playStart="0">
            <Audio channels="2" sampleRate="44100" duration="1"><File path="audio/sine.wav"/></Audio>
          </Clip>
          <Clip time="5" duration="2" contentTimeUnit="beats" fadeTimeUnit="seconds" fadeInTime="0.1">
            <Warps contentTimeUnit="seconds" timeUnit="beats" id="w1">
              <Audio channels="1" sampleRate="48000" duration="1"><File path="audio/ramp.wav"/></Audio>
              <Warp time="0" contentTime="0"/>
              <Warp time="2.5" contentTime="1"/>
            </Warps>
          </Clip>
          <Clip time="10" duration="2" contentTimeUnit="beats">
            <Warps contentTimeUnit="seconds" id="w2">
              <Audio channels="1" sampleRate="48000" duration="1"><File path="audio/ramp.wav"/></Audio>
              <Warp time="0" contentTime="0"/>
              <Warp time="1" contentTime="1"/>
            </Warps>
          </Clip>
          <Clip time="14" duration="1" contentTimeUnit="seconds">
            <Audio channels="2" sampleRate="44100" duration="3"><File path="audio/song.mp3"/></Audio>
          </Clip>
        </Clips>
        <Lanes name="take 2" id="l3">
          <Clips><Clip time="0" contentTimeUnit="seconds"><Audio channels="1" sampleRate="48000" duration="0.5"><File external="true" path="outside.wav"/></Audio></Clip></Clips>
        </Lanes>
      </Lanes>
    </Lanes>
    <Markers id="mk"><Marker time="-1" name="before"/><Marker time="6" name="Verse"/></Markers>
    <TempoAutomation id="ta"><RealPoint time="0" value="150"/><RealPoint time="8" value="160"/></TempoAutomation>
  </Arrangement>
  <Scenes><Scene id="sc1"/><Scene id="sc2"/></Scenes>
</Project>)";
    const std::string meta = R"(<?xml version="1.0" encoding="UTF-8"?><MetaData><Title>Demo</Title><Comment>From elsewhere</Comment></MetaData>)";
    const fs::path file = dir.path / "demo.dawproject";
    writeZip(file, {{"project.xml", xml},
                    {"metadata.xml", meta},
                    {"audio/sine.wav", fileBytes(dir.path / "sine.wav")},
                    {"audio/ramp.wav", fileBytes(dir.path / "ramp.wav")},
                    {"audio/song.mp3", "ID3"},
                    {"plugins/synth.vstpreset", encodeVstPreset(preset)}});

    const fs::path folder = dir.path / "projects" / "Demo";
    const DawImportResult r = importDawProject(file, folder, rate, "take-20261116-140000");
    REQUIRE(r.ok);
    CHECK(r.application == "Some DAW 5.2");
    CHECK(r.title == "Demo");
    for (const auto& l : r.leftOut) INFO(l);
    CHECK(hasLine(r, "\"Band\": a group"));
    CHECK(hasLine(r, "\"Reverb return\": an effect return track"));
    CHECK(hasLine(r, "\"Keys\": an effect \"Reverb\" (CLAP)"));
    CHECK(hasLine(r, "\"Keys\": its pan"));
    CHECK(hasLine(r, "\"Guitar\": its sends"));
    CHECK(hasLine(r, "(switched off)"));
    CHECK(hasLine(r, "time-stretched"));
    CHECK(hasLine(r, "song.mp3 (only WAV files are read)"));
    CHECK(hasLine(r, "\"Keys\": its automation"));
    CHECK(hasLine(r, "Tempo changes"));
    CHECK(hasLine(r, "2 scene(s)"));
    CHECK(hasLine(r, "1 marker(s) before the first clip"));

    const ProjectData d = loadImported(folder);
    CHECK(d.note == "From elsewhere");
    std::vector<std::string> names;
    for (const auto& t : d.tracks) names.push_back(t.name);
    CHECK(names == std::vector<std::string>{"Keys", "Guitar", "Guitar take 2", "Empty"});

    // Keys: mute, -12 dB, the instrument with its state; the looped clip in beats.
    const Track& k = trackNamed(d, "Keys");
    CHECK(k.kind == TrackKind::Midi);
    CHECK(k.mute);
    CHECK(std::abs(k.volumeDb + 12.0f) < 0.01f);
    CHECK(k.instrumentClass == "ABCDEF0123456789ABCDEF0123456789");
    CHECK(k.instrumentName == "Synth");
    CHECK(k.instrumentState == "SYNTHSTATE");
    // The clip at beat 4 (1.6 s), 8 beats long, from content beat 1, loop 0..2:
    // passes: content 1..2 at beats 4..5, then 0..2 at 5..7, 7..9, 9..11, 11..12 (cut).
    // Note 62 (content 1, 2 beats) plays in the first pass cut to the pass end (1 beat),
    // then in each later full pass (cut at 2); note 60 (content 0) in each later pass;
    // note 64 (content 3) is outside the loop.
    CHECK(k.takeStart == std::llround(1.6 * rate));
    const auto ns = notesOf(k.take, rate);
    std::map<int, std::vector<NoteSeen>> byKey;
    for (const auto& n : ns) byKey[n.key].push_back(n);
    REQUIRE(byKey[62].size() == 4);  // beats 4, 6, 8, 10
    CHECK(byKey[62][0].on == 0);
    CHECK(byKey[62][0].off == static_cast<std::uint64_t>(std::llround(0.4 * rate)));  // cut at the pass end
    CHECK(byKey[62][1].on == static_cast<std::uint64_t>(std::llround(0.8 * rate)));
    REQUIRE(byKey[60].size() == 4);  // beats 5, 7, 9, 11
    CHECK(byKey[60][0].on == static_cast<std::uint64_t>(std::llround(0.4 * rate)));
    CHECK(byKey[60][0].vel == 64);
    CHECK(byKey[60][3].off - byKey[60][3].on == static_cast<std::uint64_t>(std::llround(0.2 * rate)));
    CHECK(byKey.count(64) == 0);
    CHECK(byKey.count(70) == 0);  // the switched-off clip

    // Guitar: solo; the sine at 0 (44.1 → 48 kHz, 1 s long in a 2.5 beat = 1 s clip), the ramp at
    // beat 5 (2 s) with a fade-in; the stretched clip and the mp3 left out.
    const Track& g = trackNamed(d, "Guitar");
    CHECK(g.kind == TrackKind::Audio);
    CHECK(g.solo);
    CHECK(g.takeStart == 0);
    CHECK(g.channels == std::vector<unsigned int>{0, 1});
    unsigned int wr = 0;
    unsigned int wc = 0;
    const auto audio = readWav(g.take, wr, wc);
    CHECK(wr == rate);
    REQUIRE(wc == 2);
    CHECK(audio.size() / 2 == static_cast<std::size_t>(std::llround(2.8 * rate)));  // to the end of the ramp clip (2 s + 0.8 s)
    // The sine: compare with a true 1 kHz sine at 48 kHz in the middle of the clip (the 24-bit file and the filter).
    double worst = 0.0;
    for (std::size_t i = 4800; i < 43200; i += 11) {
        const double want = 0.5 * std::sin(2 * kPi * 1000.0 * static_cast<double>(i) / rate);
        worst = std::max(worst, std::abs(audio[i * 2] - want));
        worst = std::max(worst, std::abs(audio[i * 2 + 1] + want));  // the right side is inverted
    }
    CHECK(worst < 2e-3);
    // The ramp: exact after its 0.1 s fade-in (same rate, whole frames), mono on both sides.
    unsigned int rr = 0;
    unsigned int rc = 0;
    const auto ramp = readWav(dir.path / "ramp.wav", rr, rc);
    const std::size_t at = static_cast<std::size_t>(2 * rate);
    for (std::size_t i = rate / 10; i < static_cast<std::size_t>(0.8 * rate); i += 997) {
        CHECK(audio[(at + i) * 2] == ramp[i]);
        CHECK(audio[(at + i) * 2 + 1] == ramp[i]);
    }
    CHECK(std::abs(audio[(at + rate / 20) * 2] - ramp[rate / 20] * 0.5f) < 1e-4f);  // half way through the fade
    // Between the clips: silence.
    CHECK(audio[static_cast<std::size_t>(1.5 * rate) * 2] == 0.0f);

    // The nested lane: a track of its own, from the external file.
    const Track& g2 = trackNamed(d, "Guitar take 2");
    CHECK(g2.solo);  // the same mix as its track
    CHECK(g2.channels == std::vector<unsigned int>{0});
    CHECK(readWav(g2.take, wr, wc).size() == rate / 2);

    // An empty track stays (no take).
    CHECK(trackNamed(d, "Empty").take.empty());

    // The marker at beat 6 (2.4 s); the earliest take at 0.
    RecordingNotes back;
    std::string err;
    REQUIRE(loadRecordingNotes(folder / "takes" / "take-20261116-140000", back, err));
    CHECK(back.name == "Demo");
    REQUIRE(back.marks.size() == 1);
    CHECK(std::abs(back.marks[0].seconds - 2.4) < 1e-9);
    CHECK(back.marks[0].text == "Verse");
}

TEST_CASE("T-PRJ-11: files that cannot be imported change nothing", "[project][dawimport][T-PRJ-11]")
{
    TempDir dir;
    const fs::path folder = dir.path / "projects" / "X";
    auto refused = [&](const fs::path& file, const std::string& why) {
        const DawImportResult r = importDawProject(file, folder, 48000, "take-20261116-150000");
        CHECK_FALSE(r.ok);
        INFO(r.error);
        CHECK(r.error.find(why) != std::string::npos);
        CHECK_FALSE(fs::exists(folder));
    };
    refused(dir.path / "missing.dawproject", "cannot read");
    std::ofstream(dir.path / "text.dawproject") << "just text";
    refused(dir.path / "text.dawproject", "not a ZIP archive");
    writeZip(dir.path / "empty.dawproject", {{"readme.txt", "x"}});
    refused(dir.path / "empty.dawproject", "project.xml is not in the file");
    writeZip(dir.path / "broken.dawproject", {{"project.xml", "<Project version=\"1.0\"><Structure>"}});
    refused(dir.path / "broken.dawproject", "project.xml is damaged");
    writeZip(dir.path / "other.dawproject", {{"project.xml", "<Song/>"}});
    refused(dir.path / "other.dawproject", "no <Project>");
    writeZip(dir.path / "newer.dawproject", {{"project.xml", "<Project version=\"2.0\"/>"}});
    refused(dir.path / "newer.dawproject", "version 2.0");

    // An existing project is not overwritten.
    writeZip(dir.path / "ok.dawproject", {{"project.xml", "<Project version=\"1.0\"><Structure/></Project>"}});
    fs::create_directories(folder);
    ProjectData p;
    std::string err;
    REQUIRE(saveProject(folder, p, err));
    const DawImportResult r = importDawProject(dir.path / "ok.dawproject", folder, 48000, "take-20261116-150000");
    CHECK_FALSE(r.ok);
    CHECK_FALSE(fs::exists(folder / "takes"));
    // A new folder: an (empty) project.
    const DawImportResult ok = importDawProject(dir.path / "ok.dawproject", dir.path / "new", 48000, "take-20261116-150000");
    REQUIRE(ok.ok);
    CHECK(ok.tracks == 0);
    CHECK(isProjectFolder(dir.path / "new"));
}
