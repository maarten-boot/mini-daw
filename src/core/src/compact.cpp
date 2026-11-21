#include "minidaw/compact.hpp"

#include "file_util.hpp"
#include "minidaw/audio_reader.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/track.hpp"

#include <FLAC/stream_encoder.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace minidaw {

namespace {

bool isWavTake(const fs::path& p)
{
    int n = 0;
    std::string name;
    bool audio = false;
    if (!parseTakeFileName(p, n, name, audio) || !audio) return false;
    std::string ext = pathToUtf8(p.extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".wav";
}

void removeQuietly(const fs::path& p)
{
    std::error_code ec;
    fs::remove(p, ec);
}

// The folder's entries onto the disk (Linux; Windows has no such call for a
// folder and keeps its metadata journal itself).
void syncFolder(const fs::path& folder)
{
#if !defined(_WIN32)
    const int fd = ::open(folder.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#else
    (void)folder;
#endif
}

// Work files of a compaction that never ended (crash, kill): only one file is
// converted at a time, so any ".*.flac.part" there now is left over (CC10).
void removeStaleParts(const fs::path& folder)
{
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = pathToUtf8(it->path().filename());
        if (n.size() > 11 && n[0] == '.' && n.compare(n.size() - 10, 10, ".flac.part") == 0) removeQuietly(it->path());
    }
}

std::string comparable(const fs::path& p)
{
    std::error_code ec;
    fs::path a = fs::absolute(p, ec);
    if (ec) a = p;
    std::string s = pathToUtf8(a.lexically_normal());
#if defined(_WIN32)
    for (char& c : s) {  // Windows names: any case (ASCII), either slash
        c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
#endif
    return s;
}

} // namespace

bool sameTakePath(const fs::path& a, const fs::path& b)
{
    if (a == b) return true;
    return comparable(a) == comparable(b);
}

std::vector<fs::path> wavTakeFiles(const fs::path& folder, bool recursive)
{
    std::vector<fs::path> out;
    std::error_code ec;
    if (!recursive) {
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) && isWavTake(it->path())) out.push_back(it->path());
        }
    } else {
        fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (it->is_directory(ec) && it->path().filename() == "deleted") {  // kept as they were
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(ec) && isWavTake(it->path())) out.push_back(it->path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

CompactFileResult compactFile(const fs::path& wav, const std::atomic<bool>* cancel)
{
    CompactFileResult r;
    r.wav = wav;
    r.flac = fs::path(wav).replace_extension(".flac");
    const std::string name = pathToUtf8(wav.filename());
    const auto cancelled = [&] { return cancel != nullptr && cancel->load(); };
    std::error_code ec;
    if (fs::exists(r.flac, ec)) {
        r.skipped = true;
        r.error = name + ": " + pathToUtf8(r.flac.filename()) + " is there already";
        return r;
    }
    AudioReader in;
    if (!in.open(wav, r.error, AudioReader::Kind::Wav)) {
        // A format mini-daw does not read (8-bit, ...): kept as it is, not an error (CC11).
        if (r.error.find("not supported") != std::string::npos) r.skipped = true;
        return r;
    }
    if (in.isFloat()) {
        r.skipped = true;
        r.error = name + ": a float WAV, which FLAC cannot hold exactly; kept as WAV";
        return r;
    }
    if (in.channels() > 8) {
        r.skipped = true;
        r.error = name + ": more than 8 channels, which FLAC cannot hold; kept as WAV";
        return r;
    }
    r.before = fs::file_size(wav, ec);
    const fs::path part = wav.parent_path() / ("." + pathToUtf8(r.flac.filename()) + ".part");
    removeStaleParts(wav.parent_path());
    const unsigned ch = in.channels();
    const std::size_t block = 8192;
    std::vector<std::int32_t> a(block * ch), b(block * ch);
    // 1. Encode.
    {
        std::FILE* f = detail::openForWriting(part, false);
        if (f == nullptr) {
            r.error = "cannot write " + pathToUtf8(part) + " (disk full or no permission?)";
            return r;
        }
        FLAC__StreamEncoder* enc = FLAC__stream_encoder_new();
        if (enc == nullptr) {
            std::fclose(f);
            removeQuietly(part);
            r.error = "FLAC: out of memory";
            return r;
        }
        FLAC__stream_encoder_set_channels(enc, ch);
        FLAC__stream_encoder_set_bits_per_sample(enc, in.bitsPerSample());
        FLAC__stream_encoder_set_sample_rate(enc, in.sampleRate());
        FLAC__stream_encoder_set_compression_level(enc, 5);
        FLAC__stream_encoder_set_total_samples_estimate(enc, in.frames());
        // libFLAC owns `f` from here, also when init fails (as in the audio export, crosscheck #26 CC1).
        bool ok = FLAC__stream_encoder_init_FILE(enc, f, nullptr, nullptr) == FLAC__STREAM_ENCODER_INIT_STATUS_OK;
        std::uint64_t done = 0;
        while (ok && done < in.frames()) {
            if (cancelled()) {
                ok = false;
                break;
            }
            const std::size_t n = in.readPcm(a.data(), block);
            if (n == 0) {
                ok = false;
                r.error = "cannot read " + name + (in.error().empty() ? "" : ": " + in.error());
                break;
            }
            if (!FLAC__stream_encoder_process_interleaved(enc, a.data(), static_cast<unsigned>(n))) {
                ok = false;
                r.error = "FLAC: " + std::string(FLAC__stream_encoder_get_resolved_state_string(enc));
            }
            done += n;
        }
        const bool finished = FLAC__stream_encoder_finish(enc) != 0;  // closes the file
        FLAC__stream_encoder_delete(enc);
        in.close();  // Windows cannot remove a file that is still open (seen under Wine: "Sharing violation")
        if (!ok || !finished) {
            removeQuietly(part);
            if (r.error.empty() && !cancelled()) r.error = "cannot write " + pathToUtf8(part) + " (disk full?)";
            return r;
        }
    }
    // 2. Check: the FLAC decoded again equals the WAV, sample by sample.
    {
        AudioReader wavIn, flacIn;
        std::string err;
        bool same = wavIn.open(wav, err, AudioReader::Kind::Wav) && flacIn.open(part, err, AudioReader::Kind::Flac);
        if (same) {
            same = flacIn.frames() == wavIn.frames() && flacIn.channels() == wavIn.channels() &&
                   flacIn.sampleRate() == wavIn.sampleRate();
        }
        std::uint64_t left = same ? wavIn.frames() : 0;
        while (same && left > 0) {
            if (cancelled()) {
                same = false;
                break;
            }
            const std::size_t n = wavIn.readPcm(a.data(), block);
            const std::size_t m = flacIn.readPcm(b.data(), block);
            same = n == m && n > 0 && std::equal(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(n * ch), b.begin());
            left -= std::min<std::uint64_t>(left, n);
        }
        if (!same) {
            removeQuietly(part);
            if (!cancelled()) {
                r.error = name + ": the FLAC file did not match the WAV (" + (err.empty() ? "samples differ" : err) +
                          "); kept as WAV";
            }
            return r;
        }
    }
    // 3. On the disk for sure before the WAV goes (takes survive a power loss;
    //    crosscheck #27 CC4).
    {
        std::FILE* f = detail::openForAppending(part);
        const bool synced = f != nullptr && detail::syncToDisk(f);
        if (f != nullptr) std::fclose(f);
        if (!synced) {
            removeQuietly(part);
            r.error = "cannot write " + pathToUtf8(part) + " to the disk; kept as WAV";
            return r;
        }
    }
    // 4. Into place, then the WAV goes. If it cannot (in use), the FLAC goes again.
    fs::rename(part, r.flac, ec);
    if (ec) {
        removeQuietly(part);
        r.error = "cannot name " + pathToUtf8(r.flac) + ": " + ec.message();
        return r;
    }
    syncFolder(wav.parent_path());  // the new name, before the WAV's goes
    fs::remove(wav, ec);
    if (ec) {
        removeQuietly(r.flac);
        r.error = name + " is in use (" + ec.message() + "); kept as WAV, try again later";
        return r;
    }
    r.after = fs::file_size(r.flac, ec);
    r.ok = true;
    return r;
}

// --- Compactor ---------------------------------------------------------------------

Compactor::~Compactor() { stop(); }

void Compactor::add(const std::vector<fs::path>& wavFiles)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& f : wavFiles) {
            const auto same = [&](const fs::path& q) { return sameTakePath(q, f); };
            if (std::none_of(queue_.begin(), queue_.end(), same) && !(working_ && same(currentPath_))) {
                queue_.push_back(f);
            }
        }
        if (!worker_.joinable() && !stopping_ && !queue_.empty()) worker_ = std::thread([this] { run(); });
    }
    wake_.notify_all();
}

void Compactor::setPaused(bool paused)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (paused_ == paused) return;
        paused_ = paused;
    }
    wake_.notify_all();
}

bool Compactor::paused() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return paused_;
}

std::size_t Compactor::pending() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool Compactor::working() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return working_;
}

std::string Compactor::current() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return current_;
}

std::vector<CompactFileResult> Compactor::takeResults()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CompactFileResult> r;
    r.swap(results_);
    return r;
}

void Compactor::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        queue_.clear();
    }
    cancel_.store(true);
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void Compactor::run()
{
    for (;;) {
        fs::path next;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || (!paused_ && !queue_.empty()); });
            if (stopping_) return;
            next = queue_.front();
            queue_.pop_front();
            working_ = true;
            current_ = pathToUtf8(next.filename());
            currentPath_ = next;
        }
        CompactFileResult r = compactFile(next, &cancel_);
        std::lock_guard<std::mutex> lock(mutex_);
        working_ = false;
        current_.clear();
        currentPath_.clear();
        if (!stopping_) results_.push_back(std::move(r));
    }
}

} // namespace minidaw
