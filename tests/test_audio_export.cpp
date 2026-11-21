// Export audio (P29, SPEC-PRJ-06): T-PRJ-12.

#include "minidaw/audio_export.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/plugin_scan.hpp"
#include "minidaw/wav.hpp"
#include "test_support.hpp"

#include <FLAC/stream_decoder.h>
#include <miniz.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>

using namespace minidaw;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

// A WAV of `frames` frames: channel c of frame i = value(i, c).
template <class F>
fs::path makeWav(const fs::path& path, unsigned rate, unsigned channels, std::size_t frames, F value)
{
    WavWriter w;
    std::string err;
    REQUIRE(w.open(path, rate, channels, err));
    std::vector<float> buf(frames * channels);
    for (std::size_t i = 0; i < frames; ++i) {
        for (unsigned c = 0; c < channels; ++c) buf[i * channels + c] = value(i, c);
    }
    REQUIRE(w.write(buf.data(), frames, err));
    REQUIRE(w.close(err));
    return path;
}

std::string head(const fs::path& p, std::size_t n)
{
    std::ifstream f(p, std::ios::binary);
    std::string s(n, '\0');
    f.read(s.data(), static_cast<std::streamsize>(n));
    s.resize(static_cast<std::size_t>(f.gcount()));
    return s;
}

// Collects an encoder's output in memory.
struct MemoryEncoder : AudioEncoder {
    std::vector<float> samples;
    bool open(const fs::path&, unsigned, std::string&) override { return true; }
    bool write(const float* s, std::size_t frames, std::string&) override
    {
        samples.insert(samples.end(), s, s + frames * 2);
        return true;
    }
    bool finish(std::string&) override { return true; }
};

// Decodes a FLAC file to interleaved int32 (24-bit values).
struct FlacRead {
    std::vector<std::int32_t> samples;
    unsigned rate = 0, channels = 0, bits = 0;
};
FlacRead decodeFlac(const fs::path& p)
{
    FlacRead out;
    FLAC__StreamDecoder* d = FLAC__stream_decoder_new();
    REQUIRE(d != nullptr);
    const auto write = [](const FLAC__StreamDecoder*, const FLAC__Frame* frame, const FLAC__int32* const buffer[],
                          void* user) -> FLAC__StreamDecoderWriteStatus {
        auto* o = static_cast<FlacRead*>(user);
        for (unsigned i = 0; i < frame->header.blocksize; ++i) {
            for (unsigned c = 0; c < frame->header.channels; ++c) o->samples.push_back(buffer[c][i]);
        }
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    };
    const auto meta = [](const FLAC__StreamDecoder*, const FLAC__StreamMetadata* m, void* user) {
        if (m->type != FLAC__METADATA_TYPE_STREAMINFO) return;
        auto* o = static_cast<FlacRead*>(user);
        o->rate = m->data.stream_info.sample_rate;
        o->channels = m->data.stream_info.channels;
        o->bits = m->data.stream_info.bits_per_sample;
    };
    const auto error = [](const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void*) {};
    std::FILE* f = std::fopen(pathToUtf8(p).c_str(), "rb");
    REQUIRE(f != nullptr);
    REQUIRE(FLAC__stream_decoder_init_FILE(d, f, write, meta, error, &out) == FLAC__STREAM_DECODER_INIT_STATUS_OK);
    REQUIRE(FLAC__stream_decoder_process_until_end_of_stream(d));
    FLAC__stream_decoder_delete(d);  // closes f
    return out;
}

std::vector<std::string> zipNames(const fs::path& p)
{
    mz_zip_archive z{};
    REQUIRE(mz_zip_reader_init_file(&z, pathToUtf8(p).c_str(), 0));
    std::vector<std::string> names;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z); ++i) {
        char name[512];
        mz_zip_reader_get_filename(&z, i, name, sizeof name);
        names.emplace_back(name);
    }
    mz_zip_reader_end(&z);
    std::sort(names.begin(), names.end());
    return names;
}

AudioExportResult runExport(std::vector<ExportItem> items, const AudioExportSettings& s)
{
    AudioExport ex;
    std::string err;
    REQUIRE(ex.start(std::move(items), s, err));
    for (int i = 0; i < 20000 && !ex.done(); ++i) {
        ex.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(ex.done());
    return ex.finish();
}

bool noWorkFolderLeft(const fs::path& folder)
{
    for (const auto& e : fs::directory_iterator(folder)) {
        if (pathToUtf8(e.path().filename()).rfind(".mini-daw-export-", 0) == 0) return false;
    }
    return true;
}

} // namespace

TEST_CASE("T-PRJ-12: export formats, names and file names", "[export][T-PRJ-12]")
{
    CHECK(parseExportFormat("OGG") == ExportFormat::Ogg);
    CHECK(parseExportFormat("vorbis") == ExportFormat::Ogg);
    CHECK(parseExportFormat("opus") == ExportFormat::Opus);
    CHECK(parseExportFormat("Flac") == ExportFormat::Flac);
    CHECK(parseExportFormat("wav") == ExportFormat::Wav);
    CHECK_FALSE(parseExportFormat("mp3"));
    CHECK(exportExtension(ExportFormat::Opus) == ".opus");
    CHECK(exportFormatName(ExportFormat::Ogg) == "Ogg Vorbis");

    ExportItem item;
    item.name = "My song: take 1?";
    ExportPart p;
    p.track = 3;
    p.trackName = "Keys";
    item.parts.push_back(p);
    CHECK(exportFileBase(item, nullptr) == "My song_ take 1_");
    CHECK(exportFileBase(item, &item.parts[0]) == "My song_ take 1_ - 03-Keys");
    CHECK(exportSafeName("CON") != "CON");  // a Windows device name

    AudioExportSettings s;
    s.folder = "out";
    s.format = ExportFormat::Flac;
    CHECK(exportTargets({item}, s) == std::vector<fs::path>{fs::path("out") / "My song_ take 1_.flac"});
    s.stems = true;
    CHECK(exportTargets({item}, s) == std::vector<fs::path>{fs::path("out") / "My song_ take 1_ - 03-Keys.flac"});
    s.zip = true;
    s.zipName = "Final";
    CHECK(exportTargets({item}, s) == std::vector<fs::path>{fs::path("out") / "Final.zip"});
}

TEST_CASE("T-PRJ-12: parts are mixed at their places, with their gains; a stem alone", "[export][T-PRJ-12]")
{
    test::TempDir dir;
    // A: stereo 0.25 / -0.25, 100 frames at 0. B: mono 0.5, 50 frames from 80, gain 0.5.
    // C: mono 0.1, 30 frames starting at -10 (its first 10 frames are cut).
    std::vector<ExportPart> parts(3);
    parts[0].wav = makeWav(dir.path() / "a.wav", 48000, 2, 100, [](std::size_t, unsigned c) { return c ? -0.25f : 0.25f; });
    parts[1].wav = makeWav(dir.path() / "b.wav", 48000, 1, 50, [](std::size_t, unsigned) { return 0.5f; });
    parts[1].start = 80;
    parts[1].gain = 0.5f;
    parts[2].wav = makeWav(dir.path() / "c.wav", 48000, 1, 30, [](std::size_t, unsigned) { return 0.1f; });
    parts[2].start = -10;
    CHECK(mixLength(parts) == 130);

    MemoryEncoder enc;
    std::atomic<bool> cancel{false};
    std::atomic<std::uint64_t> done{0};
    float peak = 0.0f;
    std::string err;
    REQUIRE(mixParts(parts, nullptr, 48000, enc, cancel, done, peak, err));
    REQUIRE(enc.samples.size() == 130 * 2);
    CHECK(done.load() == 130);
    const float q = 1.0f / 8388607.0f;  // one 24-bit step
    CHECK(enc.samples[0] == Approx(0.25f + 0.1f).margin(2 * q));     // A + C
    CHECK(enc.samples[1] == Approx(-0.25f + 0.1f).margin(2 * q));
    CHECK(enc.samples[2 * 25] == Approx(0.25f).margin(2 * q));       // C ended at 20
    CHECK(enc.samples[2 * 90] == Approx(0.25f + 0.25f).margin(2 * q));  // A + B * 0.5
    CHECK(enc.samples[2 * 90 + 1] == Approx(-0.25f + 0.25f).margin(2 * q));
    CHECK(enc.samples[2 * 120] == Approx(0.25f).margin(2 * q));      // B alone (mono on both)
    CHECK(enc.samples[2 * 120 + 1] == Approx(0.25f).margin(2 * q));
    CHECK(peak == Approx(0.5f).margin(2 * q));

    // A stem: only B, but as long as the whole mix (they line up).
    MemoryEncoder stem;
    done = 0;
    peak = 0.0f;
    REQUIRE(mixParts(parts, &parts[1], 48000, stem, cancel, done, peak, err));
    REQUIRE(stem.samples.size() == 130 * 2);
    CHECK(stem.samples[0] == 0.0f);
    CHECK(stem.samples[2 * 79] == 0.0f);
    CHECK(stem.samples[2 * 80] == Approx(0.25f).margin(2 * q));

    // Too loud: clipped in the file, the peak reported.
    std::vector<ExportPart> loud(2);
    loud[0].wav = parts[1].wav;
    loud[1].wav = parts[1].wav;
    loud[1].gain = 2.0f;
    MemoryEncoder l;
    peak = 0.0f;
    REQUIRE(mixParts(loud, nullptr, 48000, l, cancel, done, peak, err));
    CHECK(peak == Approx(1.5f).margin(4 * q));
    CHECK(l.samples[0] == 1.0f);

    // Another sample rate: refused (the GUI leaves such files out first).
    std::vector<ExportPart> other(1);
    other[0].wav = makeWav(dir.path() / "d.wav", 44100, 1, 10, [](std::size_t, unsigned) { return 0.0f; });
    CHECK_FALSE(mixParts(other, nullptr, 48000, l, cancel, done, peak, err));
    CHECK(err.find("44100") != std::string::npos);

    // Cancelled: stops, no error.
    cancel = true;
    err.clear();
    MemoryEncoder c;
    CHECK_FALSE(mixParts(parts, nullptr, 48000, c, cancel, done, peak, err));
    CHECK(err.empty());
}

TEST_CASE("T-PRJ-12: every format writes a file of its kind; FLAC is lossless", "[export][T-PRJ-12]")
{
    test::TempDir dir;
    // One second of a 440 Hz sine at -6 dBFS, left; right half as loud.
    const std::size_t frames = 48000;
    std::vector<ExportPart> parts(1);
    parts[0].wav = makeWav(dir.path() / "sine.wav", 48000, 2, frames, [](std::size_t i, unsigned c) {
        const float v = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * static_cast<double>(i) / 48000.0));
        return c ? v * 0.5f : v;
    });
    WavData original;
    std::string err;
    REQUIRE(readWav(parts[0].wav, original, err));

    for (ExportFormat f : {ExportFormat::Ogg, ExportFormat::Opus, ExportFormat::Flac, ExportFormat::Wav}) {
        INFO(exportFormatName(f));
        const fs::path out = dir.path() / ("out" + std::string(exportExtension(f)));
        auto enc = makeEncoder(f, ExportQuality{});
        std::atomic<bool> cancel{false};
        std::atomic<std::uint64_t> done{0};
        float peak = 0.0f;
        REQUIRE(enc->open(out, 48000, err));
        REQUIRE(mixParts(parts, nullptr, 48000, *enc, cancel, done, peak, err));
        REQUIRE(enc->finish(err));
        enc.reset();
        const std::string h = head(out, 64);
        const auto size = fs::file_size(out);
        switch (f) {
        case ExportFormat::Ogg:
            CHECK(h.rfind("OggS", 0) == 0);
            CHECK(h.find("vorbis") != std::string::npos);
            CHECK(size > 5000);
            CHECK(size < 100000);  // compressed: far below the 288 kB of the WAV
            break;
        case ExportFormat::Opus:
            CHECK(h.rfind("OggS", 0) == 0);
            CHECK(h.find("OpusHead") != std::string::npos);
            CHECK(size > 5000);
            CHECK(size < 100000);
            break;
        case ExportFormat::Flac: {
            CHECK(h.rfind("fLaC", 0) == 0);
            const FlacRead d = decodeFlac(out);
            CHECK(d.rate == 48000);
            CHECK(d.channels == 2);
            CHECK(d.bits == 24);
            REQUIRE(d.samples.size() == frames * 2);
            bool same = true;
            for (std::size_t i = 0; i < d.samples.size() && same; ++i) {
                same = d.samples[i] == floatToPcm24(original.samples[i]);
            }
            CHECK(same);
            CHECK(size < 288000);
            break;
        }
        case ExportFormat::Wav: {
            WavData back;
            REQUIRE(readWav(out, back, err));
            CHECK(back.sampleRate == 48000);
            CHECK(back.channels == 2);
            CHECK(back.frames() == frames);
            CHECK(back.samples[2 * 1000] == Approx(original.samples[2 * 1000]).margin(1e-6));
            break;
        }
        }
    }
    // Opus at another rate (44.1 kHz in): resampled by the encoder, still a file.
    std::vector<ExportPart> p44(1);
    p44[0].wav = makeWav(dir.path() / "s44.wav", 44100, 1, 44100, [](std::size_t i, unsigned) {
        return 0.3f * static_cast<float>(std::sin(static_cast<double>(i) * 0.05));
    });
    auto enc = makeEncoder(ExportFormat::Opus, ExportQuality{6, 96});
    std::atomic<bool> cancel{false};
    std::atomic<std::uint64_t> done{0};
    float peak = 0.0f;
    REQUIRE(enc->open(dir.path() / "44.opus", 44100, err));
    REQUIRE(mixParts(p44, nullptr, 44100, *enc, cancel, done, peak, err));
    REQUIRE(enc->finish(err));
    // A folder that does not exist: an error, no crash.
    auto bad = makeEncoder(ExportFormat::Flac, ExportQuality{});
    CHECK_FALSE(bad->open(dir.path() / "no" / "such" / "x.flac", 48000, err));
    CHECK(err.find("cannot write") != std::string::npos);
}

TEST_CASE("T-PRJ-12: an export writes mixes, stems or one zip, and cleans up", "[export][T-PRJ-12]")
{
    test::TempDir dir;
    const fs::path src = dir.path() / "src";
    fs::create_directories(src);
    const fs::path a = makeWav(src / "01-Guitar.wav", 48000, 1, 4800, [](std::size_t, unsigned) { return 0.2f; });
    const fs::path b = makeWav(src / "02-Bass.wav", 48000, 1, 9600, [](std::size_t, unsigned) { return 0.1f; });
    std::ofstream(src / "03-Keys.mid") << "MThd";  // only copied into the zip

    const auto song = [&] {
        ExportItem item;
        item.name = "Song";
        ExportPart pa;
        pa.track = 1;
        pa.trackName = "Guitar";
        pa.wav = a;
        ExportPart pb;
        pb.track = 2;
        pb.trackName = "Bass";
        pb.wav = b;
        pb.start = 2400;
        item.parts = {pa, pb};
        item.midiFiles = {src / "03-Keys.mid"};
        item.leftOut = {"Song: 04-Synth.mid has no instrument"};
        return item;
    };
    AudioExportSettings s;
    s.format = ExportFormat::Wav;
    s.sampleRate = 48000;

    SECTION("one mix")
    {
        s.folder = dir.path() / "mix";
        fs::create_directories(s.folder);
        const AudioExportResult r = runExport({song()}, s);
        REQUIRE(r.ok);
        REQUIRE(r.files.size() == 1);
        CHECK(r.files[0] == s.folder / "Song.wav");
        WavData w;
        std::string err;
        REQUIRE(readWav(r.files[0], w, err));
        CHECK(w.frames() == 2400 + 9600);
        CHECK(w.samples[2 * 3000] == Approx(0.3f).margin(1e-5));
        CHECK(r.leftOut == std::vector<std::string>{"Song: 04-Synth.mid has no instrument"});
        CHECK(r.clipped.empty());
        CHECK(noWorkFolderLeft(s.folder));
    }
    SECTION("stems, all as long as the song")
    {
        s.folder = dir.path() / "stems";
        fs::create_directories(s.folder);
        s.stems = true;
        s.format = ExportFormat::Flac;
        const AudioExportResult r = runExport({song()}, s);
        REQUIRE(r.ok);
        REQUIRE(r.files.size() == 2);
        CHECK(r.files[0].filename() == "Song - 01-Guitar.flac");
        CHECK(r.files[1].filename() == "Song - 02-Bass.flac");
        CHECK(decodeFlac(r.files[0]).samples.size() == (2400 + 9600) * 2);
        CHECK(decodeFlac(r.files[1]).samples.size() == (2400 + 9600) * 2);
        CHECK(noWorkFolderLeft(s.folder));
    }
    SECTION("a zip with the audio and the MIDI files, no loose files")
    {
        s.folder = dir.path() / "zip";
        fs::create_directories(s.folder);
        s.zip = true;
        s.zipName = "Final";
        s.format = ExportFormat::Ogg;
        ExportItem second = song();
        second.name = "Song";  // the same name twice: numbered
        const AudioExportResult r = runExport({song(), second}, s);
        REQUIRE(r.ok);
        REQUIRE(r.files == std::vector<fs::path>{s.folder / "Final.zip"});
        CHECK(zipNames(s.folder / "Final.zip") ==
              std::vector<std::string>{"Song (2)/03-Keys.mid", "Song (2)/Song (2).ogg", "Song/03-Keys.mid",
                                       "Song/Song.ogg"});
        std::size_t entries = 0;
        for (const auto& e : fs::directory_iterator(s.folder)) {
            (void)e;
            ++entries;
        }
        CHECK(entries == 1);  // only the zip
    }
    SECTION("cancelled: nothing left")
    {
        s.folder = dir.path() / "cancel";
        fs::create_directories(s.folder);
        AudioExport ex;
        std::string err;
        REQUIRE(ex.start({song()}, s, err));
        ex.cancel();
        for (int i = 0; i < 5000 && !ex.done(); ++i) {
            ex.step();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const AudioExportResult r = ex.finish();
        CHECK_FALSE(r.ok);
        CHECK(r.cancelled);
        CHECK(fs::is_empty(s.folder));
    }
    SECTION("cannot write: an error, nothing left")
    {
        s.folder = dir.path() / "nofolder";
        AudioExport ex;
        std::string err;
        CHECK_FALSE(ex.start({song()}, s, err));
        CHECK(err.find("not a folder") != std::string::npos);
        fs::create_directories(s.folder);
        CHECK_FALSE(ex.start({}, s, err));
        CHECK(err == "nothing to export");
    }
}

TEST_CASE("T-PRJ-12: a MIDI part is rendered through its instrument", "[export][vst][T-PRJ-12]")
{
    test::TempDir dir;
    std::string err;
    const ScannedModule m = scanModule(MINIDAW_TEST_VST3);
    REQUIRE(m.classes.size() == 1);
    ExportItem item;
    item.name = "Rendered";
    ExportPart p;
    p.track = 1;
    p.trackName = "Synth";
    RenderJob job;
    job.module = MINIDAW_TEST_VST3;
    job.classId = m.classes[0].classId;
    job.events = {{1000, {0x90, 69, 127}, 3}, {5000, {0x80, 69, 0}, 3}};
    job.takeFrames = 6000;
    p.render = job;
    p.gain = 0.5f;
    item.parts.push_back(p);
    // And one that cannot load: left out with the reason, the export goes on.
    ExportPart broken = p;
    broken.track = 2;
    broken.trackName = "Missing";
    broken.render->module = dir.path() / "missing.vst3";
    item.parts.push_back(broken);

    AudioExportSettings s;
    s.folder = dir.path();
    s.format = ExportFormat::Wav;
    s.sampleRate = 48000;
    const AudioExportResult r = runExport({item}, s);
    REQUIRE(r.ok);
    REQUIRE(r.files.size() == 1);
    REQUIRE(r.leftOut.size() == 1);
    CHECK(r.leftOut[0].find("Missing not rendered") != std::string::npos);
    WavData w;
    REQUIRE(readWav(r.files[0], w, err));
    CHECK(w.frames() >= 6000);
    float before = 0.0f, during = 0.0f;
    for (std::size_t i = 0; i < 1000; ++i) before = std::max(before, std::fabs(w.samples[2 * i]));
    for (std::size_t i = 1000; i < 5000; ++i) during = std::max(during, std::fabs(w.samples[2 * i]));
    CHECK(before < 1e-4f);  // silent before the note
    CHECK(during > 0.01f);  // the synth plays it
    CHECK(noWorkFolderLeft(dir.path()));
}

TEST_CASE("T-PRJ-12: the song as Play plays it: mixer, Sound from, instruments", "[export][T-PRJ-12]")
{
    test::TempDir dir;
    const auto wav = [&](const char* name, unsigned rate) {
        return makeWav(dir.path() / name, rate, 1, 100, [](std::size_t, unsigned) { return 0.1f; });
    };
    std::ofstream(dir.path() / "03-Keys.mid", std::ios::binary)
        << std::string("MThd\0\0\0\x06\0\0\0\x01\x01\xE0MTrk\0\0\0\x04\0\xFF\x2F\0", 26);
    std::vector<Track> tracks(6);
    tracks[0].number = 1;
    tracks[0].name = "Vocal";
    tracks[0].take = wav("01-Vocal.wav", 48000);
    tracks[0].takeStart = 1000;
    tracks[0].volumeDb = -6.0f;
    tracks[1].number = 2;
    tracks[1].name = "Guitar";
    tracks[1].take = wav("02-Guitar.wav", 48000);
    tracks[1].takeStart = -200;  // an overdub moved earlier: the song starts there
    tracks[1].mute = true;
    tracks[2].number = 3;
    tracks[2].name = "Keys";
    tracks[2].kind = TrackKind::Midi;
    tracks[2].take = dir.path() / "03-Keys.mid";
    tracks[3].number = 4;
    tracks[3].name = "Keys sound";
    tracks[3].instrumentOf = 3;
    tracks[3].take = wav("04-Keys sound.wav", 48000);
    tracks[4].number = 5;
    tracks[4].name = "Old";
    tracks[4].take = wav("05-Old.wav", 44100);
    tracks[5].number = 6;
    tracks[5].name = "Hardware";
    tracks[5].kind = TrackKind::Midi;
    tracks[5].midiOut = "MIDI Out 1";
    tracks[5].take = dir.path() / "03-Keys.mid";
    ExportInstruments inst;
    inst[3] = {MINIDAW_TEST_VST3, "0123"};

    SECTION("mix: muted out, instrument rendered, its recorded sound silent")
    {
        const ExportItem s = songExportItem(tracks, inst, 48000, false, "My song");
        CHECK(s.name == "My song");
        REQUIRE(s.parts.size() == 2);
        CHECK(s.parts[0].track == 1);
        CHECK(s.parts[0].gain == Approx(std::pow(10.0f, -6.0f / 20.0f)));
        CHECK(s.parts[0].start == 1000);  // the muted guitar does not count for the start
        CHECK(s.parts[1].track == 3);
        REQUIRE(s.parts[1].render);
        CHECK(s.parts[1].render->classId == "0123");
        CHECK(s.parts[1].start == 0);
        REQUIRE(s.leftOut.size() == 2);
        CHECK(s.leftOut[0].find("05-Old.wav has 44100 Hz") != std::string::npos);
        CHECK(s.leftOut[1].find("MIDI output MIDI Out 1") != std::string::npos);
        CHECK(s.midiFiles.size() == 2);
    }
    SECTION("stems: mute does not count, volume does")
    {
        const ExportItem s = songExportItem(tracks, inst, 48000, true, "My song");
        REQUIRE(s.parts.size() == 3);
        CHECK(s.parts[0].track == 1);
        CHECK(s.parts[1].track == 2);
        CHECK(s.parts[1].gain == Approx(1.0f));
        CHECK(s.parts[1].start == 0);  // the earliest
        CHECK(s.parts[0].start == 1200);
        CHECK(s.parts[2].track == 3);
    }
    SECTION("Sound from = recording: the linked WAV, not the instrument")
    {
        tracks[2].soundFromRecording = true;
        const ExportItem s = songExportItem(tracks, inst, 48000, false, "My song");
        REQUIRE(s.parts.size() == 2);
        CHECK(s.parts[1].track == 4);
        CHECK_FALSE(s.parts[1].render);
    }
    SECTION("solo: only the soloed track")
    {
        tracks[0].solo = true;
        const ExportItem s = songExportItem(tracks, inst, 48000, false, "My song");
        REQUIRE(s.parts.size() == 1);
        CHECK(s.parts[0].track == 1);
        CHECK(s.parts[0].start == 0);
    }
    SECTION("a recording on its own: its files with the tracks' settings")
    {
        RecordingInfo rec;
        rec.folder = dir.path();
        rec.files = {dir.path() / "01-Vocal.wav", dir.path() / "03-Keys.mid", dir.path() / "04-Keys sound.wav",
                     wav("09-Extra.wav", 48000), dir.path() / "05-Old.wav"};
        ExportItem r = recordingExportItem(rec, tracks, inst, 48000, false, "Take 1");
        REQUIRE(r.parts.size() == 3);
        CHECK(r.parts[0].track == 3);  // MIDI first, rendered
        REQUIRE(r.parts[0].render);
        CHECK(r.parts[1].track == 1);
        CHECK(r.parts[1].start == 0);
        CHECK(r.parts[2].track == 9);  // no such track: 0 dB, its name from the file
        CHECK(r.parts[2].trackName == "Extra");
        CHECK(r.parts[2].gain == 1.0f);
        CHECK(r.midiFiles == std::vector<fs::path>{dir.path() / "03-Keys.mid"});
        REQUIRE(r.leftOut.size() == 1);
        CHECK(r.leftOut[0].find("44100") != std::string::npos);
        // Without the instrument loaded: its recorded sound is used instead, the MIDI listed.
        r = recordingExportItem(rec, tracks, {}, 48000, false, "Take 1");
        REQUIRE(r.parts.size() == 3);
        CHECK(r.parts[1].track == 4);
        CHECK(r.parts[1].gain == Approx(1.0f));
        CHECK(r.leftOut[0].find("03-Keys.mid not in the export: track 3 has no instrument") != std::string::npos);
    }
}
