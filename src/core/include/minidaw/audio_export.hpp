#pragma once
// Export audio (P29, SPEC-PRJ-06): the song or recordings rendered to Ogg
// Vorbis, Opus, FLAC or WAV, as one mix or as stems, optionally zipped with
// their MIDI files.
//
// The GUI describes what to export as ExportItems (one per chosen item: the
// song, or a recording), each a list of parts (an audio file, or a MIDI take
// to render through its instrument) with their place and gain. AudioExport
// then works in steps: step() on the GUI thread renders the MIDI parts one
// after another (plugins are loaded on the GUI thread), then a worker thread
// mixes, encodes and zips. A cancelled or failed export removes what it made.
#include "minidaw/render.hpp"
#include "minidaw/timeline.hpp"
#include "minidaw/track.hpp"

#include <atomic>
#include <map>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace minidaw {

enum class ExportFormat { Ogg, Opus, Flac, Wav };

// ".ogg", ".opus", ".flac", ".wav".
std::string_view exportExtension(ExportFormat format);
// "Ogg Vorbis", "Opus", "FLAC", "WAV".
std::string_view exportFormatName(ExportFormat format);
// "ogg" / "vorbis", "opus", "flac", "wav" (any case); nothing for others.
std::optional<ExportFormat> parseExportFormat(std::string_view text);

struct ExportQuality {
    int vorbisQuality = 6;  // 0..10 (about 64 .. 500 kbit/s; 6 = about 192)
    int opusKbps = 160;     // 64..256
};
inline constexpr int kMinVorbisQuality = 0, kMaxVorbisQuality = 10;
inline constexpr int kMinOpusKbps = 64, kMaxOpusKbps = 256;

// A stereo encoder writing one file. Samples are interleaved stereo floats,
// -1 .. 1 (louder ones are clipped). finish() completes and closes the file;
// a failed or abandoned encoder leaves a file the caller removes.
class AudioEncoder {
public:
    virtual ~AudioEncoder() = default;
    virtual bool open(const std::filesystem::path& path, unsigned int sampleRate, std::string& error) = 0;
    virtual bool write(const float* stereo, std::size_t frames, std::string& error) = 0;
    virtual bool finish(std::string& error) = 0;
};
std::unique_ptr<AudioEncoder> makeEncoder(ExportFormat format, const ExportQuality& quality);

// One input of an item: an audio file, or a MIDI take rendered through its
// instrument (render.out is set by the export).
struct ExportPart {
    int track = 0;                      // its track number (stem file names)
    std::string trackName;
    std::filesystem::path wav;          // an audio file (empty when rendered)
    std::optional<RenderJob> render;    // a MIDI take through an instrument
    std::int64_t start = 0;             // frame in the item where it starts (may be < 0: cut)
    float gain = 1.0f;
};

// One chosen thing to export: the song, or a recording.
struct ExportItem {
    std::string name;                               // file name base (made safe when written)
    std::vector<ExportPart> parts;
    std::vector<std::filesystem::path> midiFiles;   // for the zip
    std::vector<std::string> leftOut;               // what could not be used, and why
};

// --- What to export, from the tracks (GUI-independent, tested) -------------------

// A loaded instrument of a MIDI track, by track number.
struct ExportInstrument {
    std::filesystem::path module;
    std::string classId;
};
using ExportInstruments = std::map<int, ExportInstrument>;

// The song as Play plays it: every track's take at its place (shifted so the
// earliest take of any track starts at 0, sounding or not: a mix and its
// stems line up). Mix (`stems` false): playbackGain (volume, mute,
// solo, "Sound from"); a track Play does not hear is not in it. Stems: each
// track that sounds, at its volume (mute and solo do not count). MIDI takes
// go through their loaded instrument (rendered) unless "Sound from" is the
// recording; others are listed in leftOut, as are WAV files of another rate
// or that cannot be read. midiFiles: every MIDI track's take.
ExportItem songExportItem(const std::vector<Track>& tracks, const ExportInstruments& instruments,
                          unsigned int sampleRate, bool stems, std::string name, bool readFiles = true);
// A recording from the Timeline on its own, from its start: file "NN-name"
// with track NN's volume / mute / solo and instrument (as in the song); a
// file without such a track at 0 dB (a MIDI file without one is left out).
// An instrument's recorded sound (a linked track's WAV) is used when its MIDI
// is not rendered. midiFiles: the recording's .mid files.
ExportItem recordingExportItem(const RecordingInfo& recording, const std::vector<Track>& tracks,
                               const ExportInstruments& instruments, unsigned int sampleRate, bool stems,
                               std::string name, bool readFiles = true);
// readFiles false (both): the same parts without opening any file (no rate
// checks, no MIDI read): for the file names the export will write (CC3).

struct AudioExportSettings {
    ExportFormat format = ExportFormat::Ogg;
    ExportQuality quality;
    bool stems = false;                 // one file per part instead of one mix per item
    bool zip = false;                   // everything in one .zip (no loose files)
    std::filesystem::path folder;       // where the files (or the zip) go
    std::string zipName = "mini-daw export";
    unsigned int sampleRate = 48000;    // of the parts (Opus resamples to 48 kHz itself)
};

// A file name made safe (safePresetName rules), "export" when nothing is left.
std::string exportSafeName(std::string_view name);
// The files the export will write (audio files, or the one zip), so the GUI
// can ask before replacing existing ones.
std::vector<std::filesystem::path> exportTargets(const std::vector<ExportItem>& items,
                                                 const AudioExportSettings& settings);
// "<item>" for a mix, "<item> - NN-name" for a stem.
std::string exportFileBase(const ExportItem& item, const ExportPart* stem);

// One file the export makes: item `item`'s mix (part -1) or the stem of part
// `part`, named `fileName`; in a zip in the folder `zipFolder`. Names that
// would be the same (also in another case: one file on Windows) get " (2)",
// " (3)" ... (crosscheck #26 CC3 / CC8: the export and the Replace check use
// these same names).
struct ExportOutput {
    std::size_t item = 0;
    int part = -1;
    std::string fileName;
    std::string zipFolder;
};
std::vector<ExportOutput> exportOutputs(const std::vector<ExportItem>& items, const AudioExportSettings& settings);

struct AudioExportResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::vector<std::filesystem::path> files;   // what was written
    std::vector<std::string> leftOut;           // from the items, and renders that failed
    std::vector<std::string> clipped;           // "<file>: peak +1.2 dBFS"
    float peak = 0.0f;                          // the highest peak of any file (linear)
};

// Mixes the parts (WAV files at `sampleRate`; parts of other rates must be
// left out by the caller) into `encoder`, from frame 0 to the end of the
// last part: a mix of all parts, or only `only` (a stem). Mono files play on
// both channels; of more channels the first two are used. Stops early when
// `cancel` becomes true (returns false, error empty). `done` counts frames.
bool mixParts(const std::vector<ExportPart>& parts, const ExportPart* only, unsigned int sampleRate,
              AudioEncoder& encoder, const std::atomic<bool>& cancel, std::atomic<std::uint64_t>& done,
              float& peak, std::string& error, std::uint64_t* notNumbers = nullptr);
// (NaN / Inf in the mix become silence and are counted in `notNumbers`.)
// The frames from 0 to the end of the last part (0 when none can be read).
std::uint64_t mixLength(const std::vector<ExportPart>& parts);

class AudioExport {
public:
    AudioExport() = default;
    ~AudioExport();
    AudioExport(const AudioExport&) = delete;
    AudioExport& operator=(const AudioExport&) = delete;

    // GUI thread. Checks the settings and makes a work folder; false with an
    // error when it cannot start.
    bool start(std::vector<ExportItem> items, AudioExportSettings settings, std::string& error);
    // GUI thread, every frame while active(): renders, then starts the mix thread.
    void step();
    bool active() const { return started_ && !finished_; }
    bool done() const { return done_.load(std::memory_order_acquire); }
    void cancel() { cancel_.store(true); }
    double progress() const;    // 0..1
    std::string stage() const;  // "Rendering Keys (2 of 3)", "Writing song.ogg", "Zipping"
    // GUI thread, after done(): ends the thread, removes the work folder.
    AudioExportResult finish();

private:
    void runMix();
    void mixAll(AudioExportResult& r);
    void setStage(std::string text);

    std::vector<ExportItem> items_;
    AudioExportSettings settings_;
    std::filesystem::path work_;    // rendered MIDI parts, files for the zip
    bool started_ = false;
    bool finished_ = false;
    // Rendering (GUI thread).
    std::vector<std::pair<std::size_t, std::size_t>> renders_;  // item, part
    std::set<std::pair<std::size_t, std::size_t>> failed_;      // renders that failed: left out
    std::size_t nextRender_ = 0;
    OfflineRenderer renderer_;
    bool rendering_ = false;
    // Mixing (worker thread).
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> done_{false};
    std::atomic<std::uint64_t> mixed_{0};
    std::uint64_t mixTotal_ = 1;
    mutable std::mutex mutex_;      // stage_ and result_
    std::string stage_;
    AudioExportResult result_;
};

} // namespace minidaw
