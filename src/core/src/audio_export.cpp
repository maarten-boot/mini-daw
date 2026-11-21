#include "minidaw/audio_export.hpp"

#include "file_util.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/presets.hpp"
#include "minidaw/smf.hpp"
#include "minidaw/wav.hpp"
#include "minidaw/audio_reader.hpp"
#include "minidaw/wav_reader.hpp"
#include "zip_writer.hpp"

#include <FLAC/stream_encoder.h>
#include <opusenc.h>
#include <vorbis/vorbisenc.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>

namespace fs = std::filesystem;

namespace minidaw {

std::string_view exportExtension(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Ogg: return ".ogg";
    case ExportFormat::Opus: return ".opus";
    case ExportFormat::Flac: return ".flac";
    case ExportFormat::Wav: return ".wav";
    }
    return ".wav";
}

std::string_view exportFormatName(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Ogg: return "Ogg Vorbis";
    case ExportFormat::Opus: return "Opus";
    case ExportFormat::Flac: return "FLAC";
    case ExportFormat::Wav: return "WAV";
    }
    return "WAV";
}

std::optional<ExportFormat> parseExportFormat(std::string_view text)
{
    std::string t(text);
    for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (t == "ogg" || t == "vorbis") return ExportFormat::Ogg;
    if (t == "opus") return ExportFormat::Opus;
    if (t == "flac") return ExportFormat::Flac;
    if (t == "wav") return ExportFormat::Wav;
    return std::nullopt;
}

namespace {

inline float clampSample(float s, bool& clipped)
{
    if (s > 1.0f) {
        clipped = true;
        return 1.0f;
    }
    if (s < -1.0f) {
        clipped = true;
        return -1.0f;
    }
    return s;
}

std::string writeError(const fs::path& path)
{
    return "cannot write " + pathToUtf8(path) + " (disk full or no permission?)";
}

// --- WAV (24-bit, the existing writer) ----------------------------------------
class WavEncoder : public AudioEncoder {
public:
    bool open(const fs::path& path, unsigned int sampleRate, std::string& error) override
    {
        return writer_.open(path, sampleRate, 2, error);
    }
    bool write(const float* stereo, std::size_t frames, std::string& error) override
    {
        return writer_.write(stereo, frames, error);
    }
    bool finish(std::string& error) override { return writer_.close(error); }

private:
    WavWriter writer_;
};

// --- FLAC (24-bit, compression level 5) -----------------------------------------
class FlacEncoder : public AudioEncoder {
public:
    ~FlacEncoder() override
    {
        if (enc_ != nullptr) FLAC__stream_encoder_delete(enc_);  // finishes and closes a file it was given
        if (file_ != nullptr) std::fclose(file_);                // not handed over (no encoder)
    }
    bool open(const fs::path& path, unsigned int sampleRate, std::string& error) override
    {
        path_ = path;
        file_ = detail::openForWriting(path, false);
        if (file_ == nullptr) {
            error = writeError(path);
            return false;
        }
        enc_ = FLAC__stream_encoder_new();
        if (enc_ == nullptr) {
            error = "FLAC: out of memory";
            return false;
        }
        FLAC__stream_encoder_set_channels(enc_, 2);
        FLAC__stream_encoder_set_bits_per_sample(enc_, 24);
        FLAC__stream_encoder_set_sample_rate(enc_, sampleRate);
        FLAC__stream_encoder_set_compression_level(enc_, 5);
        const auto st = FLAC__stream_encoder_init_FILE(enc_, file_, nullptr, nullptr);
        // The encoder owns the FILE from here, also when init failed: libFLAC stores
        // it before anything that can fail and closes it in finish / delete
        // (crosscheck #26 CC1: it was closed a second time here).
        file_ = nullptr;
        if (st != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
            error = std::string("FLAC: ") + FLAC__StreamEncoderInitStatusString[st];
            return false;
        }
        return true;
    }
    bool write(const float* stereo, std::size_t frames, std::string& error) override
    {
        buf_.resize(frames * 2);
        for (std::size_t i = 0; i < frames * 2; ++i) buf_[i] = floatToPcm24(stereo[i]);
        if (!FLAC__stream_encoder_process_interleaved(enc_, buf_.data(), static_cast<unsigned>(frames))) {
            error = std::string("FLAC: ") + FLAC__stream_encoder_get_resolved_state_string(enc_);
            if (FLAC__stream_encoder_get_state(enc_) == FLAC__STREAM_ENCODER_IO_ERROR) error = writeError(path_);
            return false;
        }
        return true;
    }
    bool finish(std::string& error) override
    {
        const bool ok = FLAC__stream_encoder_finish(enc_) != 0;  // closes the file
        if (!ok) error = writeError(path_);
        return ok;
    }

private:
    fs::path path_;
    std::FILE* file_ = nullptr;
    FLAC__StreamEncoder* enc_ = nullptr;
    std::vector<FLAC__int32> buf_;
};

// --- Ogg Vorbis (VBR quality) ------------------------------------------------------
class VorbisEncoder : public AudioEncoder {
public:
    explicit VorbisEncoder(int quality) : quality_(std::clamp(quality, kMinVorbisQuality, kMaxVorbisQuality)) {}
    ~VorbisEncoder() override
    {
        if (started_) {
            ogg_stream_clear(&os_);
            vorbis_block_clear(&vb_);
            vorbis_dsp_clear(&vd_);
        }
        if (infoInit_) vorbis_info_clear(&vi_);
        if (file_ != nullptr) std::fclose(file_);
    }
    bool open(const fs::path& path, unsigned int sampleRate, std::string& error) override
    {
        path_ = path;
        file_ = detail::openForWriting(path, false);
        if (file_ == nullptr) {
            error = writeError(path);
            return false;
        }
        vorbis_info_init(&vi_);
        infoInit_ = true;
        if (vorbis_encode_init_vbr(&vi_, 2, static_cast<long>(sampleRate), static_cast<float>(quality_) / 10.0f) != 0) {
            error = "Ogg Vorbis: this sample rate (" + std::to_string(sampleRate) + " Hz) is not supported";
            return false;
        }
        vorbis_comment vc;
        vorbis_comment_init(&vc);
        vorbis_comment_add_tag(&vc, "ENCODER", "mini-daw");
        vorbis_analysis_init(&vd_, &vi_);
        vorbis_block_init(&vd_, &vb_);
        std::random_device rd;
        ogg_stream_init(&os_, static_cast<int>(rd() & 0x7fffffff));
        started_ = true;
        ogg_packet header, comm, code;
        vorbis_analysis_headerout(&vd_, &vc, &header, &comm, &code);
        vorbis_comment_clear(&vc);
        ogg_stream_packetin(&os_, &header);
        ogg_stream_packetin(&os_, &comm);
        ogg_stream_packetin(&os_, &code);
        ogg_page og;
        while (ogg_stream_flush(&os_, &og) != 0) {
            if (!writePage(og, error)) return false;
        }
        return true;
    }
    bool write(const float* stereo, std::size_t frames, std::string& error) override
    {
        if (frames == 0) return true;
        float** in = vorbis_analysis_buffer(&vd_, static_cast<int>(frames));
        for (std::size_t i = 0; i < frames; ++i) {
            in[0][i] = stereo[2 * i];
            in[1][i] = stereo[2 * i + 1];
        }
        vorbis_analysis_wrote(&vd_, static_cast<int>(frames));
        return drain(error);
    }
    bool finish(std::string& error) override
    {
        vorbis_analysis_wrote(&vd_, 0);  // end of stream
        if (!drain(error)) return false;
        const bool ok = std::fflush(file_) == 0;
        const bool closed = std::fclose(file_) == 0;
        file_ = nullptr;
        if (!ok || !closed) {
            error = writeError(path_);
            return false;
        }
        return true;
    }

private:
    bool writePage(const ogg_page& og, std::string& error)
    {
        if (std::fwrite(og.header, 1, static_cast<std::size_t>(og.header_len), file_) !=
                static_cast<std::size_t>(og.header_len) ||
            std::fwrite(og.body, 1, static_cast<std::size_t>(og.body_len), file_) != static_cast<std::size_t>(og.body_len)) {
            error = writeError(path_);
            return false;
        }
        return true;
    }
    bool drain(std::string& error)
    {
        while (vorbis_analysis_blockout(&vd_, &vb_) == 1) {
            vorbis_analysis(&vb_, nullptr);
            vorbis_bitrate_addblock(&vb_);
            ogg_packet op;
            while (vorbis_bitrate_flushpacket(&vd_, &op) != 0) {
                ogg_stream_packetin(&os_, &op);
                ogg_page og;
                while (ogg_stream_pageout(&os_, &og) != 0) {
                    if (!writePage(og, error)) return false;
                    if (ogg_page_eos(&og) != 0) break;
                }
            }
        }
        return true;
    }

    int quality_;
    fs::path path_;
    std::FILE* file_ = nullptr;
    vorbis_info vi_{};
    vorbis_dsp_state vd_{};
    vorbis_block vb_{};
    ogg_stream_state os_{};
    bool infoInit_ = false;
    bool started_ = false;
};

// --- Opus (Ogg Opus through libopusenc; it resamples to 48 kHz) ------------------
class OpusEncoder : public AudioEncoder {
public:
    explicit OpusEncoder(int kbps) : kbps_(std::clamp(kbps, kMinOpusKbps, kMaxOpusKbps)) {}
    ~OpusEncoder() override
    {
        if (enc_ != nullptr) ope_encoder_destroy(enc_);
        if (comments_ != nullptr) ope_comments_destroy(comments_);
        if (file_ != nullptr) std::fclose(file_);
    }
    bool open(const fs::path& path, unsigned int sampleRate, std::string& error) override
    {
        path_ = path;
        file_ = detail::openForWriting(path, false);
        if (file_ == nullptr) {
            error = writeError(path);
            return false;
        }
        comments_ = ope_comments_create();
        if (comments_ == nullptr) {
            error = "Opus: out of memory";
            return false;
        }
        ope_comments_add(comments_, "ENCODER", "mini-daw");
        static const OpusEncCallbacks callbacks = {
            [](void* user, const unsigned char* ptr, opus_int32 len) -> int {
                auto* self = static_cast<OpusEncoder*>(user);
                return std::fwrite(ptr, 1, static_cast<std::size_t>(len), self->file_) == static_cast<std::size_t>(len)
                           ? 0
                           : 1;
            },
            [](void*) -> int { return 0; },  // closed in finish()
        };
        int err = 0;
        enc_ = ope_encoder_create_callbacks(&callbacks, this, comments_, static_cast<opus_int32>(sampleRate), 2, 0, &err);
        if (enc_ == nullptr) {
            error = std::string("Opus: ") + ope_strerror(err);
            return false;
        }
        ope_encoder_ctl(enc_, OPUS_SET_BITRATE(kbps_ * 1000));
        return true;
    }
    bool write(const float* stereo, std::size_t frames, std::string& error) override
    {
        const int r = ope_encoder_write_float(enc_, stereo, static_cast<int>(frames));
        if (r != OPE_OK) {
            error = r == OPE_WRITE_FAIL ? writeError(path_) : std::string("Opus: ") + ope_strerror(r);
            return false;
        }
        return true;
    }
    bool finish(std::string& error) override
    {
        const int r = ope_encoder_drain(enc_);
        const bool flushed = std::fflush(file_) == 0;
        const bool closed = std::fclose(file_) == 0;
        file_ = nullptr;
        if (r != OPE_OK || !flushed || !closed) {
            error = r != OPE_OK && r != OPE_WRITE_FAIL ? std::string("Opus: ") + ope_strerror(r) : writeError(path_);
            return false;
        }
        return true;
    }

private:
    int kbps_;
    fs::path path_;
    std::FILE* file_ = nullptr;
    OggOpusComments* comments_ = nullptr;
    OggOpusEnc* enc_ = nullptr;
};

std::string twoDigits(int n)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d", n);
    return buf;
}

void removeQuietly(const fs::path& p)
{
    std::error_code ec;
    fs::remove(p, ec);
}

} // namespace

std::unique_ptr<AudioEncoder> makeEncoder(ExportFormat format, const ExportQuality& quality)
{
    switch (format) {
    case ExportFormat::Ogg: return std::make_unique<VorbisEncoder>(quality.vorbisQuality);
    case ExportFormat::Opus: return std::make_unique<OpusEncoder>(quality.opusKbps);
    case ExportFormat::Flac: return std::make_unique<FlacEncoder>();
    case ExportFormat::Wav: return std::make_unique<WavEncoder>();
    }
    return std::make_unique<WavEncoder>();
}

std::string exportSafeName(std::string_view name)
{
    std::string s = safePresetName(name);
    if (s.empty() || s == "Preset") s = name.empty() ? "export" : s;
    return s;
}

std::string exportFileBase(const ExportItem& item, const ExportPart* stem)
{
    std::string base = item.name;
    if (stem != nullptr) base += " - " + twoDigits(stem->track) + "-" + stem->trackName;
    return exportSafeName(base);
}

namespace {

std::string foldCase(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// `base` + ext, or "base (2)" + ext ... when taken (compared in any case).
std::string uniqueName(const std::string& base, const std::string& ext, std::set<std::string>& used)
{
    std::string name = base + ext;
    for (int k = 2; used.count(foldCase(name)) != 0; ++k) name = base + " (" + std::to_string(k) + ")" + ext;
    used.insert(foldCase(name));
    return name;
}

} // namespace

std::vector<ExportOutput> exportOutputs(const std::vector<ExportItem>& items, const AudioExportSettings& settings)
{
    std::vector<ExportOutput> out;
    const std::string ext(exportExtension(settings.format));
    std::set<std::string> usedNames, usedFolders;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const ExportItem& item = items[i];
        if (item.parts.empty()) continue;
        const std::string folder = uniqueName(exportSafeName(item.name), "", usedFolders);
        if (!settings.stems) {
            out.push_back({i, -1, uniqueName(exportFileBase(item, nullptr), ext, usedNames), folder});
            continue;
        }
        for (std::size_t p = 0; p < item.parts.size(); ++p) {
            out.push_back({i, static_cast<int>(p), uniqueName(exportFileBase(item, &item.parts[p]), ext, usedNames), folder});
        }
    }
    return out;
}

std::vector<fs::path> exportTargets(const std::vector<ExportItem>& items, const AudioExportSettings& settings)
{
    std::vector<fs::path> out;
    const auto outputs = exportOutputs(items, settings);
    if (settings.zip) {
        if (!outputs.empty()) out.push_back(settings.folder / pathFromUtf8(exportSafeName(settings.zipName) + ".zip"));
        return out;
    }
    for (const auto& o : outputs) out.push_back(settings.folder / pathFromUtf8(o.fileName));
    return out;
}

// --- Mixing -------------------------------------------------------------------------

namespace {

struct OpenPart {
    const ExportPart* part = nullptr;
    AudioReader reader;
    std::uint64_t frames = 0;
    unsigned int channels = 0;
};

fs::path partFile(const ExportPart& p) { return p.render ? p.render->out : p.wav; }

} // namespace

std::uint64_t mixLength(const std::vector<ExportPart>& parts)
{
    std::int64_t end = 0;
    for (const auto& p : parts) {
        AudioReader r;
        std::string err;
        if (!r.open(partFile(p), err)) continue;
        end = std::max(end, p.start + static_cast<std::int64_t>(r.frames()));
    }
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, end));
}

bool mixParts(const std::vector<ExportPart>& parts, const ExportPart* only, unsigned int sampleRate,
              AudioEncoder& encoder, const std::atomic<bool>& cancel, std::atomic<std::uint64_t>& done, float& peak,
              std::string& error, std::uint64_t* notNumbers)
{
    // All files are opened (also for a stem: every stem as long as the whole item, so they line up).
    std::vector<std::unique_ptr<OpenPart>> open;
    std::int64_t end = 0;
    for (const auto& p : parts) {
        auto o = std::make_unique<OpenPart>();
        o->part = &p;
        if (!o->reader.open(partFile(p), error)) return false;
        if (o->reader.sampleRate() != sampleRate) {
            error = pathToUtf8(partFile(p)) + " has " + std::to_string(o->reader.sampleRate()) + " Hz, not " +
                    std::to_string(sampleRate) + " Hz";
            return false;
        }
        o->frames = o->reader.frames();
        o->channels = o->reader.channels();
        end = std::max(end, p.start + static_cast<std::int64_t>(o->frames));
        open.push_back(std::move(o));
    }
    const auto length = static_cast<std::uint64_t>(std::max<std::int64_t>(0, end));
    constexpr std::size_t kBlock = 4096;
    std::vector<float> mix(kBlock * 2), in;
    for (std::uint64_t t = 0; t < length; t += kBlock) {
        if (cancel.load()) return false;
        const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(kBlock, length - t));
        std::fill(mix.begin(), mix.begin() + static_cast<std::ptrdiff_t>(n * 2), 0.0f);
        for (auto& o : open) {
            if (only != nullptr && o->part != only) continue;
            const ExportPart& p = *o->part;
            // This block [t, t+n) against the part's frames [start, start+frames).
            const std::int64_t from = std::max<std::int64_t>(static_cast<std::int64_t>(t), p.start);
            const std::int64_t to = std::min<std::int64_t>(static_cast<std::int64_t>(t + n),
                                                           p.start + static_cast<std::int64_t>(o->frames));
            if (from >= to) continue;
            const auto count = static_cast<std::size_t>(to - from);
            const auto at = static_cast<std::uint64_t>(from - p.start);
            if (!o->reader.seek(at)) {
                error = "cannot read " + pathToUtf8(partFile(p));
                return false;
            }
            in.resize(count * o->channels);
            if (o->reader.read(in.data(), count) != count) {
                error = "cannot read " + pathToUtf8(partFile(p)) + (o->reader.error().empty() ? "" : ": " + o->reader.error());
                return false;
            }
            const std::size_t out0 = static_cast<std::size_t>(from - static_cast<std::int64_t>(t));
            for (std::size_t i = 0; i < count; ++i) {
                const float l = in[i * o->channels];
                const float r = o->channels > 1 ? in[i * o->channels + 1] : l;
                mix[(out0 + i) * 2] += l * p.gain;
                mix[(out0 + i) * 2 + 1] += r * p.gain;
            }
        }
        bool clipped = false;
        for (std::size_t i = 0; i < n * 2; ++i) {
            // NaN / Inf (a faulty plugin, a broken float WAV) become silence (crosscheck #26 CC5).
            if (!std::isfinite(mix[i])) {
                mix[i] = 0.0f;
                if (notNumbers != nullptr) ++*notNumbers;
            }
            peak = std::max(peak, std::fabs(mix[i]));
            mix[i] = clampSample(mix[i], clipped);
        }
        if (!encoder.write(mix.data(), n, error)) return false;
        done.fetch_add(n);
    }
    return true;
}

// --- Items from the tracks -----------------------------------------------------------

namespace {

// The gain of a track in a stem: its volume only.
float stemGain(const Track& t)
{
    Track v = t;
    v.mute = false;
    v.solo = false;
    return trackGain(v, false);
}

// The gain in a mix: as Play, except that an instrument's recorded sound that
// is used because its MIDI cannot be rendered follows its MIDI track's mute and
// solo (not its "Sound from").
float mixGain(const Track& t, const std::vector<Track>& tracks, bool forceLinked)
{
    if (!forceLinked) return playbackGain(t, tracks);
    const Track* owner = soundOwner(tracks, t);
    if (owner == nullptr) return playbackGain(t, tracks);
    if (owner->mute) return 0.0f;
    Track c = t;
    c.solo = t.solo || owner->solo;
    return trackGain(c, anySolo(tracks));
}

// A WAV part, or a line in leftOut when it cannot be used.
bool addWav(ExportItem& item, const fs::path& file, int track, std::string trackName, std::int64_t start, float gain,
            unsigned int rate, bool readFiles)
{
    AudioReader r;
    std::string err;
    const std::string shown = pathToUtf8(file.filename());
    if (!readFiles) {
        // Names only (the window's Replace check, CC3): no file is opened.
    } else if (!r.open(file, err)) {
        item.leftOut.push_back(item.name + ": " + shown + " cannot be read: " + err);
        return false;
    }
    if (readFiles && r.sampleRate() != rate) {
        item.leftOut.push_back(item.name + ": " + shown + " has " + std::to_string(r.sampleRate()) + " Hz, not " +
                               std::to_string(rate) + " Hz (as in Play)");
        return false;
    }
    ExportPart p;
    p.track = track;
    p.trackName = std::move(trackName);
    p.wav = file;
    p.start = start;
    p.gain = gain;
    item.parts.push_back(std::move(p));
    return true;
}

// A MIDI part rendered through its instrument, or a line in leftOut.
bool addRender(ExportItem& item, const fs::path& file, const Track& t, const ExportInstrument& inst, std::int64_t start,
               float gain, unsigned int rate, bool readFiles)
{
    SmfData d;
    std::string err;
    if (readFiles && !readSmf(file, d, err)) {
        item.leftOut.push_back(item.name + ": " + pathToUtf8(file.filename()) + " cannot be read: " + err);
        return false;
    }
    ExportPart p;
    p.track = t.number;
    p.trackName = t.name;
    RenderJob job;
    job.module = inst.module;
    job.classId = inst.classId;
    job.state = t.instrumentState;
    job.controllerState = t.instrumentControllerState;
    job.sampleRate = rate;
    if (readFiles) {
        std::uint64_t end = 0;
        job.events = smfToFrames(d, rate, &end);
        job.takeFrames = end;
    }
    p.render = std::move(job);
    p.start = start;
    p.gain = gain;
    item.parts.push_back(std::move(p));
    return true;
}

// Why a MIDI file is not in the export: no MIDI track of its number, a hardware
// MIDI output (not recordable here), or no instrument.
std::string noInstrumentLine(const ExportItem& item, const fs::path& file, const Track* t)
{
    const std::string why = t == nullptr          ? "there is no MIDI track with its number"
                            : !t->midiOut.empty() ? "it plays to the MIDI output " + t->midiOut + ", which cannot be recorded here"
                                                  : "track " + std::to_string(t->number) + " has no instrument";
    return item.name + ": " + pathToUtf8(file.filename()) + " not in the export: " + why;
}

const Track* trackByNumber(const std::vector<Track>& tracks, int number)
{
    for (const auto& t : tracks) {
        if (t.number == number) return &t;
    }
    return nullptr;
}

} // namespace

ExportItem songExportItem(const std::vector<Track>& tracks, const ExportInstruments& instruments,
                          unsigned int sampleRate, bool stems, std::string name, bool readFiles)
{
    ExportItem item;
    item.name = std::move(name);
    // The song starts at the earliest take of any track, whether it sounds or not, so a
    // mix and the stems of one song line up (crosscheck #26 CC4).
    std::optional<std::int64_t> first;
    for (const auto& t : tracks) {
        if (!t.take.empty()) first = std::min(first.value_or(t.takeStart), t.takeStart);
    }
    for (const auto& t : tracks) {
        if (t.take.empty()) continue;
        if (t.kind == TrackKind::Midi) item.midiFiles.push_back(t.take);
        const float gain = stems ? stemGain(t) : playbackGain(t, tracks);
        const std::int64_t start = t.takeStart - first.value_or(0);
        if (t.kind == TrackKind::Audio) {
            const Track* owner = soundOwner(tracks, t);
            if (owner != nullptr && !owner->soundFromRecording) continue;  // the instrument plays instead
            if (gain <= 0.0f && !stems) continue;                           // Play does not hear it
            addWav(item, t.take, t.number, t.name, start, gain, sampleRate, readFiles);
            continue;
        }
        if (!instrumentPlaysTake(t)) continue;  // its recorded sound (the linked track) plays
        const auto inst = instruments.find(t.number);
        if (inst == instruments.end()) {
            item.leftOut.push_back(noInstrumentLine(item, t.take, &t));
            continue;
        }
        if (gain <= 0.0f && !stems) continue;
        addRender(item, t.take, t, inst->second, start, gain, sampleRate, readFiles);
    }
    return item;
}

ExportItem recordingExportItem(const RecordingInfo& recording, const std::vector<Track>& tracks,
                               const ExportInstruments& instruments, unsigned int sampleRate, bool stems,
                               std::string name, bool readFiles)
{
    ExportItem item;
    item.name = std::move(name);
    // Which MIDI tracks of this recording are rendered (their linked track's WAV is then not used).
    std::set<int> rendered;
    for (const auto& f : recording.files) {
        int n = 0;
        std::string fileName;
        bool wav = false;
        const bool parsed = parseTakeFileName(f, n, fileName, wav);  // "NN-name", extension in any case (CC8)
        if (parsed && wav) continue;
        if (!parsed) {
            std::string ext = pathToUtf8(f.extension());
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".mid") continue;
            n = 0;  // a MIDI file without a track number: no track
        }
        item.midiFiles.push_back(f);
        const Track* t = n > 0 ? trackByNumber(tracks, n) : nullptr;
        if (t == nullptr || t->kind != TrackKind::Midi) {
            item.leftOut.push_back(noInstrumentLine(item, f, nullptr));
            continue;
        }
        if (!instrumentPlaysTake(*t)) continue;  // "Sound from" = recording: the WAV below plays
        const auto inst = instruments.find(n);
        if (inst == instruments.end()) {
            item.leftOut.push_back(noInstrumentLine(item, f, t));
            continue;
        }
        const float gain = stems ? stemGain(*t) : playbackGain(*t, tracks);
        if (gain <= 0.0f && !stems) {
            rendered.insert(n);
            continue;
        }
        if (addRender(item, f, *t, inst->second, 0, gain, sampleRate, readFiles)) rendered.insert(n);
    }
    for (const auto& f : recording.files) {
        int n = 0;
        std::string trackName;
        bool wav = false;
        if (!parseTakeFileName(f, n, trackName, wav)) {
            // A WAV not named "NN-name": no track, 0 dB, its name from the file.
            std::string ext = pathToUtf8(f.extension());
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".wav" && ext != ".flac") continue;
            n = 0;
            trackName = pathToUtf8(f.stem());
            wav = true;
        }
        if (!wav) continue;
        const Track* t = n > 0 ? trackByNumber(tracks, n) : nullptr;
        float gain = 1.0f;
        if (t != nullptr) {
            trackName = t->name;
            const Track* owner = soundOwner(tracks, *t);
            if (owner != nullptr && rendered.count(owner->number) != 0) continue;  // its instrument was rendered
            const bool forced = owner != nullptr && !owner->soundFromRecording;   // used because the MIDI is not
            gain = stems ? stemGain(*t) : mixGain(*t, tracks, forced);
            if (gain <= 0.0f && !stems) continue;
        }
        addWav(item, f, n, trackName, 0, gain, sampleRate, readFiles);
    }
    return item;
}

// --- AudioExport ------------------------------------------------------------------

AudioExport::~AudioExport()
{
    cancel_.store(true);
    if (rendering_) {
        renderer_.cancel();
        while (!renderer_.done()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        renderer_.finish();
    }
    if (worker_.joinable()) worker_.join();
    if (!work_.empty()) {
        std::error_code ec;
        fs::remove_all(work_, ec);
    }
}

void AudioExport::setStage(std::string text)
{
    std::lock_guard<std::mutex> lock(mutex_);
    stage_ = std::move(text);
}

std::string AudioExport::stage() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stage_;
}

bool AudioExport::start(std::vector<ExportItem> items, AudioExportSettings settings, std::string& error)
{
    if (started_) {
        error = "an export is already running";
        return false;
    }
    if (settings.folder.empty()) {
        error = "no folder chosen";
        return false;
    }
    std::error_code ec;
    if (!fs::is_directory(settings.folder, ec)) {
        error = pathToUtf8(settings.folder) + " is not a folder";
        return false;
    }
    std::size_t parts = 0;
    for (const auto& i : items) parts += i.parts.size();
    if (parts == 0) {
        error = "nothing to export";
        return false;
    }
    // A file the export would write over one of its own sources (a take, when the
    // folder chosen is a take folder): refused (crosscheck #26 CC8).
    for (const auto& target : exportTargets(items, settings)) {
        if (!fs::exists(target, ec)) continue;
        for (const auto& item : items) {
            std::vector<fs::path> inputs = item.midiFiles;
            for (const auto& p : item.parts) {
                if (!p.wav.empty()) inputs.push_back(p.wav);
            }
            for (const auto& in : inputs) {
                if (fs::equivalent(target, in, ec)) {
                    error = pathToUtf8(target) + " is one of the files to export: choose another folder";
                    return false;
                }
            }
        }
    }
    // A work folder beside the output: rendered MIDI, and the audio files when zipping.
    std::mt19937 rng(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    for (int tries = 0; tries < 20 && work_.empty(); ++tries) {
        const fs::path w = settings.folder / (".mini-daw-export-" + std::to_string(rng() % 1000000));
        if (fs::create_directory(w, ec)) work_ = w;
    }
    if (work_.empty()) {
        error = "cannot make a work folder in " + pathToUtf8(settings.folder) + (ec ? ": " + ec.message() : "");
        return false;
    }
    items_ = std::move(items);
    settings_ = std::move(settings);
    for (const auto& i : items_) result_.leftOut.insert(result_.leftOut.end(), i.leftOut.begin(), i.leftOut.end());
    int n = 0;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        for (std::size_t p = 0; p < items_[i].parts.size(); ++p) {
            auto& part = items_[i].parts[p];
            if (!part.render) continue;
            part.render->sampleRate = settings_.sampleRate;
            part.render->out = work_ / ("render-" + std::to_string(++n) + ".wav");
            renders_.emplace_back(i, p);
        }
    }
    started_ = true;
    setStage(renders_.empty() ? "Starting" : "Rendering");
    return true;
}

void AudioExport::step()
{
    if (!started_ || finished_ || done() || worker_.joinable()) return;
    if (rendering_) {
        if (cancel_.load()) renderer_.cancel();
        if (!renderer_.done()) return;
        const RenderResult r = renderer_.finish();
        rendering_ = false;
        const auto [i, p] = renders_[nextRender_ - 1];
        if (!r.ok && !r.cancelled) {
            // That part is left out; the rest goes on.
            result_.leftOut.push_back(items_[i].name + ": " + items_[i].parts[p].trackName + " not rendered: " + r.error);
            failed_.emplace(i, p);
        }
    }
    if (cancel_.load()) {
        std::lock_guard<std::mutex> lock(mutex_);
        result_.cancelled = true;
        stage_ = "Cancelled";
        done_.store(true, std::memory_order_release);
        return;
    }
    if (nextRender_ < renders_.size()) {
        const auto [i, p] = renders_[nextRender_++];
        auto& part = items_[i].parts[p];
        setStage("Rendering " + part.trackName + " (" + std::to_string(nextRender_) + " of " +
                 std::to_string(renders_.size()) + ")");
        std::string err;
        if (renderer_.start(*part.render, err)) {
            rendering_ = true;
        } else {
            result_.leftOut.push_back(items_[i].name + ": " + part.trackName + " not rendered: " + err);
            failed_.emplace(i, p);
        }
        return;
    }
    // All rendered: drop the parts that failed, then mix in the background.
    for (std::size_t i = 0; i < items_.size(); ++i) {
        auto& parts = items_[i].parts;
        for (std::size_t p = parts.size(); p-- > 0;) {
            if (failed_.count({i, p}) != 0) parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(p));
        }
    }
    mixTotal_ = 0;
    for (const auto& item : items_) {
        const std::uint64_t len = mixLength(item.parts);
        mixTotal_ += settings_.stems ? len * item.parts.size() : len;
    }
    mixTotal_ = std::max<std::uint64_t>(mixTotal_, 1);
    worker_ = std::thread([this] { runMix(); });
}

double AudioExport::progress() const
{
    if (!started_) return 0.0;
    if (done()) return 1.0;
    // Renders count for half when there are any.
    const double renders = renders_.empty() ? 1.0
                                            : (static_cast<double>(nextRender_ - (rendering_ ? 1 : 0)) +
                                               (rendering_ ? renderer_.progress() : 0.0)) /
                                                  static_cast<double>(renders_.size());
    const double mixing = worker_.joinable() ? static_cast<double>(mixed_.load()) / static_cast<double>(mixTotal_) : 0.0;
    return renders_.empty() ? std::min(1.0, mixing) : std::min(1.0, 0.5 * renders + 0.5 * mixing);
}

namespace {

// Moves a finished file into place, replacing one of that name. Across drives
// (rename fails) it is copied beside the target first and then renamed, so a
// failed copy never leaves a half file under the target's name (CC6).
bool moveIntoPlace(const fs::path& from, const fs::path& to, std::string& error)
{
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) return true;
    const fs::path part = to.parent_path() / ("." + pathToUtf8(to.filename()) + ".part");
    std::error_code ec2;
    fs::copy_file(from, part, fs::copy_options::overwrite_existing, ec2);
    if (!ec2) fs::rename(part, to, ec2);
    if (ec2) {
        removeQuietly(part);
        error = "cannot write " + pathToUtf8(to) + ": " + ec2.message();
        return false;
    }
    removeQuietly(from);
    return true;
}

} // namespace

void AudioExport::runMix()
{
    AudioExportResult r;
    try {
        mixAll(r);
    } catch (const std::exception& e) {  // never the whole program (crosscheck #26 CC5)
        r = AudioExportResult{};
        r.error = std::string("unexpected error: ") + e.what();
    } catch (...) {
        r = AudioExportResult{};
        r.error = "unexpected error";
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        r.leftOut.insert(r.leftOut.begin(), result_.leftOut.begin(), result_.leftOut.end());
        result_ = std::move(r);
        stage_ = "Done";
    }
    done_.store(true, std::memory_order_release);
}

// Everything is written into the work folder first and moved into the chosen
// folder only when all succeeded, so a failure or Cancel never touches files
// that were there (crosscheck #26 CC2).
void AudioExport::mixAll(AudioExportResult& r)
{
    const fs::path outFolder = work_ / "out";
    std::error_code ec;
    fs::create_directories(outFolder, ec);
    const auto outputs = exportOutputs(items_, settings_);
    std::vector<std::pair<std::string, fs::path>> zipEntries;  // name in the zip, file
    std::vector<std::pair<fs::path, fs::path>> moves;          // file in work, target
    std::set<std::string> zipNames;
    std::size_t lastItem = SIZE_MAX;
    std::string error;
    for (const auto& o : outputs) {
        if (cancel_.load()) break;
        const ExportItem& item = items_[o.item];
        const ExportPart* stem = o.part < 0 ? nullptr : &item.parts[static_cast<std::size_t>(o.part)];
        // WAV files stop at 4 GB: said before the work, not after it (CC10).
        if (settings_.format == ExportFormat::Wav) {
            const std::uint64_t bytes = mixLength(item.parts) * 2 * 3;
            if (bytes > WavWriter::kMaxDataBytes) {
                r.error = o.fileName + " would be over 4 GB, too long for a WAV file: choose FLAC";
                return;
            }
        }
        const fs::path file = outFolder / pathFromUtf8(o.fileName);
        setStage("Writing " + o.fileName);
        auto enc = makeEncoder(settings_.format, settings_.quality);
        float peak = 0.0f;
        std::uint64_t notNumbers = 0;
        if (!enc->open(file, settings_.sampleRate, error) ||
            !mixParts(item.parts, stem, settings_.sampleRate, *enc, cancel_, mixed_, peak, error, &notNumbers) ||
            !enc->finish(error)) {
            if (!cancel_.load()) r.error = error;
            return;
        }
        enc.reset();
        r.peak = std::max(r.peak, peak);
        if (peak > 1.0f) {
            char buf[96];
            std::snprintf(buf, sizeof buf, ": peak %+.1f dBFS, clipped", 20.0 * std::log10(peak));
            r.clipped.push_back(o.fileName + buf);
        }
        if (notNumbers > 0) {
            r.clipped.push_back(o.fileName + ": " + std::to_string(notNumbers) +
                                " sample(s) were not numbers (NaN / Inf), written as silence");
        }
        if (settings_.zip) {
            zipEntries.emplace_back(uniqueName(o.zipFolder + "/" + o.fileName, "", zipNames), file);
            if (o.item != lastItem) {
                // The item's MIDI files once, in its folder; two of one name numbered (CC8).
                for (const auto& mid : item.midiFiles) {
                    const std::string stemName = pathToUtf8(mid.stem());
                    const std::string ext = pathToUtf8(mid.extension());
                    zipEntries.emplace_back(uniqueName(o.zipFolder + "/" + stemName, ext, zipNames), mid);
                }
            }
        } else {
            moves.emplace_back(file, settings_.folder / pathFromUtf8(o.fileName));
        }
        lastItem = o.item;
    }
    if (cancel_.load()) {
        r.cancelled = true;
        return;
    }
    if (settings_.zip) {
        setStage("Zipping");
        const fs::path tmp = work_ / "export.zip";
        detail::ZipWriter zip;
        bool ok = zip.begin(tmp, error);
        for (const auto& [name, src] : zipEntries) {
            if (!ok || cancel_.load()) break;
            ok = zip.addFile(name, src, error);
        }
        if (ok && !cancel_.load()) ok = zip.finish(error);
        if (cancel_.load()) {
            r.cancelled = true;
            return;
        }
        if (!ok) {
            r.error = error;
            return;
        }
        moves.emplace_back(tmp, settings_.folder / pathFromUtf8(exportSafeName(settings_.zipName) + ".zip"));
    }
    // From here Cancel no longer counts: the files are complete (CC6).
    setStage("Moving the files into place");
    for (const auto& [from, to] : moves) {
        if (!moveIntoPlace(from, to, error)) {
            r.error = error + (r.files.empty() ? "" : " (" + std::to_string(r.files.size()) + " file(s) before it were written)");
            return;
        }
        r.files.push_back(to);
    }
    r.ok = true;
}

AudioExportResult AudioExport::finish()
{
    if (worker_.joinable()) worker_.join();
    if (rendering_) {
        renderer_.cancel();
        while (!renderer_.done()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        renderer_.finish();
        rendering_ = false;
    }
    std::error_code ec;
    if (!work_.empty()) fs::remove_all(work_, ec);
    work_.clear();
    finished_ = true;
    std::lock_guard<std::mutex> lock(mutex_);
    AudioExportResult r = std::move(result_);
    if (r.cancelled) r.ok = false;
    return r;
}

} // namespace minidaw
