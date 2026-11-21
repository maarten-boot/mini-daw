#include "minidaw/audio_reader.hpp"

#include "minidaw/path_util.hpp"
#include "minidaw/wav_reader.hpp"

#include <FLAC/stream_decoder.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

#if defined(_WIN32)
#include <share.h>
#endif

namespace fs = std::filesystem;

namespace minidaw {

namespace {

std::string lowerExtension(const fs::path& file)
{
    std::string ext = pathToUtf8(file.extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

// A FLAC file decoded on demand: whole frames into `buf`, served from `bufPos`.
class FlacSource {
public:
    ~FlacSource() { close(); }

    bool open(const fs::path& path, std::string& error)
    {
        close();
        path_ = path;
#if defined(_WIN32)
        std::FILE* f = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
#else
        std::FILE* f = std::fopen(path.c_str(), "rb");
#endif
        const std::string name = pathToUtf8(path.filename());
        if (f == nullptr) {
            error = "Cannot open " + name + ".";
            return false;
        }
        dec_ = FLAC__stream_decoder_new();
        if (dec_ == nullptr) {
            std::fclose(f);
            error = name + ": out of memory.";
            return false;
        }
        // The decoder owns `f` from here (closed by finish / delete).
        if (FLAC__stream_decoder_init_FILE(dec_, f, &FlacSource::onWrite, &FlacSource::onMeta, &FlacSource::onError,
                                           this) != FLAC__STREAM_DECODER_INIT_STATUS_OK ||
            !FLAC__stream_decoder_process_until_end_of_metadata(dec_) || channels_ == 0) {
            error = name + ": not a FLAC file mini-daw can read.";
            close();
            return false;
        }
        if (bits_ > 32 || channels_ > 8) {
            error = name + ": this FLAC format is not supported.";
            close();
            return false;
        }
        if (frames_ == 0 && !countFrames()) {  // length not in the header (crosscheck #27 CC12)
            error = name + ": not a FLAC file mini-daw can read.";
            close();
            return false;
        }
        return true;
    }

    void close()
    {
        if (dec_ != nullptr) {
            FLAC__stream_decoder_delete(dec_);  // finishes, closes the file
            dec_ = nullptr;
        }
        buf_.clear();
        bufPos_ = 0;
        position_ = 0;
        rate_ = channels_ = bits_ = 0;  // nothing of the last file stays (crosscheck #27 CC9)
        frames_ = 0;
        counting_ = false;
        error_.clear();
    }

    bool isOpen() const { return dec_ != nullptr; }
    unsigned int sampleRate() const { return rate_; }
    unsigned int channels() const { return channels_; }
    unsigned int bits() const { return bits_; }
    std::uint64_t frames() const { return frames_; }

    bool seek(std::uint64_t frame)
    {
        if (dec_ == nullptr) return false;
        // The Player seeks before every block, mostly to where it is already:
        // no decoder seek for that, nor for a frame still in the buffer
        // (crosscheck #27 CC3).
        if (frame == position_ && frame < frames_) return true;
        if (frame > position_ && frame < frames_ && channels_ > 0 &&
            frame - position_ < (buf_.size() - bufPos_) / channels_) {
            bufPos_ += static_cast<std::size_t>(frame - position_) * channels_;
            position_ = frame;
            return true;
        }
        buf_.clear();
        bufPos_ = 0;
        if (frame >= frames_) {
            position_ = frames_;
            return true;
        }
        if (!FLAC__stream_decoder_seek_absolute(dec_, frame)) {
            // Where the decoder is now is unknown: back to the start, so the
            // position is right again (crosscheck #27 CC8).
            FLAC__stream_decoder_flush(dec_);
            const std::uint64_t frames = frames_;
            FLAC__stream_decoder_reset(dec_);
            FLAC__stream_decoder_process_until_end_of_metadata(dec_);
            frames_ = frames;
            buf_.clear();
            bufPos_ = 0;
            position_ = 0;
            error_ = "Cannot seek in " + pathToUtf8(path_.filename()) + ".";
            return false;
        }
        position_ = frame;  // the write callback got the frame from `frame` on
        return true;
    }

    std::size_t readPcm(std::int32_t* out, std::size_t count)
    {
        std::size_t done = 0;
        if (dec_ == nullptr || channels_ == 0) return 0;
        while (done < count && position_ < frames_) {
            if (bufPos_ >= buf_.size()) {
                buf_.clear();
                bufPos_ = 0;
                if (!FLAC__stream_decoder_process_single(dec_) ||
                    FLAC__stream_decoder_get_state(dec_) == FLAC__STREAM_DECODER_END_OF_STREAM) {
                    if (buf_.empty()) {
                        error_ = "Read error in " + pathToUtf8(path_.filename()) + ".";
                        break;
                    }
                }
                if (buf_.empty()) continue;
            }
            const std::size_t have = (buf_.size() - bufPos_) / channels_;
            const auto n = static_cast<std::size_t>(
                std::min<std::uint64_t>(std::min<std::uint64_t>(count - done, have), frames_ - position_));
            std::copy_n(buf_.begin() + static_cast<std::ptrdiff_t>(bufPos_), n * channels_, out + done * channels_);
            bufPos_ += n * channels_;
            done += n;
            position_ += n;
        }
        return done;
    }

    const std::string& error() const { return error_; }

private:
    // Decodes the whole file once to count its frames, then goes back to the start.
    bool countFrames()
    {
        counting_ = true;
        const bool ok = FLAC__stream_decoder_process_until_end_of_stream(dec_) != 0;
        counting_ = false;
        const std::uint64_t counted = frames_;
        if (!ok || !FLAC__stream_decoder_reset(dec_) || !FLAC__stream_decoder_process_until_end_of_metadata(dec_)) {
            return false;
        }
        frames_ = counted;  // the header read again says 0
        buf_.clear();
        bufPos_ = 0;
        position_ = 0;
        return true;
    }

    static FLAC__StreamDecoderWriteStatus onWrite(const FLAC__StreamDecoder*, const FLAC__Frame* frame,
                                                  const FLAC__int32* const buffer[], void* user)
    {
        auto* self = static_cast<FlacSource*>(user);
        const unsigned n = frame->header.blocksize;
        if (self->counting_) {
            self->frames_ += n;
            return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
        }
        const unsigned ch = std::min(frame->header.channels, self->channels_);
        self->buf_.resize(self->buf_.size() + static_cast<std::size_t>(n) * self->channels_);
        std::int32_t* o = self->buf_.data() + self->buf_.size() - static_cast<std::size_t>(n) * self->channels_;
        for (unsigned i = 0; i < n; ++i) {
            for (unsigned c = 0; c < self->channels_; ++c) o[i * self->channels_ + c] = c < ch ? buffer[c][i] : 0;
        }
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }
    static void onMeta(const FLAC__StreamDecoder*, const FLAC__StreamMetadata* m, void* user)
    {
        if (m->type != FLAC__METADATA_TYPE_STREAMINFO) return;
        auto* self = static_cast<FlacSource*>(user);
        self->rate_ = m->data.stream_info.sample_rate;
        self->channels_ = m->data.stream_info.channels;
        self->bits_ = m->data.stream_info.bits_per_sample;
        self->frames_ = m->data.stream_info.total_samples;
    }
    static void onError(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void* user)
    {
        auto* self = static_cast<FlacSource*>(user);
        self->error_ = "Damaged data in " + pathToUtf8(self->path_.filename()) + ".";
    }

    fs::path path_;
    FLAC__StreamDecoder* dec_ = nullptr;
    unsigned int rate_ = 0, channels_ = 0, bits_ = 0;
    std::uint64_t frames_ = 0;
    std::uint64_t position_ = 0;
    bool counting_ = false;
    std::vector<std::int32_t> buf_;
    std::size_t bufPos_ = 0;
    std::string error_;
};

} // namespace

bool isFlacFile(const fs::path& file) { return lowerExtension(file) == ".flac"; }

bool isAudioTakeFile(const fs::path& file)
{
    const std::string ext = lowerExtension(file);
    return ext == ".wav" || ext == ".flac";
}

struct AudioReader::Impl {
    bool flac = false;
    WavReader wav;
    FlacSource fl;
    std::vector<std::int32_t> ints;  // FLAC: integers before scaling
    std::string error;
};

AudioReader::AudioReader() : impl_(std::make_unique<Impl>()) {}
AudioReader::~AudioReader() = default;

bool AudioReader::open(const fs::path& path, std::string& error, Kind kind)
{
    close();
    if (kind == Kind::ByExtension && lowerExtension(path) == ".wav") {
        // A take compacted meanwhile: its WAV is gone, the FLAC of that name is
        // there (also in the moment before a track is moved to it; crosscheck #27 CC5).
        std::error_code ec;
        const fs::path flac = fs::path(path).replace_extension(".flac");
        if (!fs::exists(path, ec) && fs::exists(flac, ec)) {
            impl_->flac = true;
            return impl_->fl.open(flac, error);
        }
    }
    impl_->flac = kind == Kind::Flac || (kind == Kind::ByExtension && isFlacFile(path));
    return impl_->flac ? impl_->fl.open(path, error) : impl_->wav.open(path, error);
}

void AudioReader::close()
{
    impl_->wav.close();
    impl_->fl.close();
}

bool AudioReader::isOpen() const { return impl_->flac ? impl_->fl.isOpen() : impl_->wav.isOpen(); }
unsigned int AudioReader::sampleRate() const { return impl_->flac ? impl_->fl.sampleRate() : impl_->wav.sampleRate(); }
unsigned int AudioReader::channels() const { return impl_->flac ? impl_->fl.channels() : impl_->wav.channels(); }
unsigned int AudioReader::bitsPerSample() const { return impl_->flac ? impl_->fl.bits() : impl_->wav.bitsPerSample(); }
bool AudioReader::isFloat() const { return !impl_->flac && impl_->wav.isFloat(); }
std::uint64_t AudioReader::frames() const { return impl_->flac ? impl_->fl.frames() : impl_->wav.frames(); }
bool AudioReader::seek(std::uint64_t frame) { return impl_->flac ? impl_->fl.seek(frame) : impl_->wav.seek(frame); }

std::size_t AudioReader::readPcm(std::int32_t* out, std::size_t count)
{
    return impl_->flac ? impl_->fl.readPcm(out, count) : impl_->wav.readPcm(out, count);
}

std::size_t AudioReader::read(float* out, std::size_t count)
{
    if (!impl_->flac) return impl_->wav.read(out, count);
    const unsigned ch = impl_->fl.channels();
    impl_->ints.resize(count * ch);
    const std::size_t got = impl_->fl.readPcm(impl_->ints.data(), count);
    // The same scaling as WavReader, so a compacted take plays exactly as before.
    const unsigned bits = impl_->fl.bits();
    const double scale = bits == 16 ? 32768.0 : bits == 24 ? 8388607.0 : bits == 32 ? 2147483648.0
                                                                                      : static_cast<double>(1u << (bits - 1));
    for (std::size_t i = 0; i < got * ch; ++i) {
        out[i] = static_cast<float>(static_cast<double>(impl_->ints[i]) / scale);
    }
    return got;
}

const std::string& AudioReader::error() const { return impl_->flac ? impl_->fl.error() : impl_->wav.error(); }

} // namespace minidaw
