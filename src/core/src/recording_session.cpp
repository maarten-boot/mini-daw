#include "minidaw/recording_session.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace minidaw {

bool SessionResult::lostData() const
{
    if (!error.empty() || audio.lostData()) return true;
    return std::any_of(midi.begin(), midi.end(), [](const MidiTakeResult& m) { return m.lostMessages > 0; });
}

RecordingSession::~RecordingSession()
{
    if (active_) stop();
}

bool RecordingSession::start(const std::vector<Track>& tracks, const RecordingSettings& settings, AudioStream& audio,
                             MidiInputs& midi, std::string& error)
{
    if (active_) stop();
    error.clear();
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        error_.clear();
    }
    std::vector<const Track*> armed;
    for (const auto& t : tracks) {
        if (t.armed) armed.push_back(&t);
    }
    if (armed.empty()) {
        error = "No track is armed. Arm at least one track to record.";
        return false;
    }
    if (!audio.info().running || audio.info().sampleRate == 0) {
        error = "The audio stream is not running; it is needed for the recording timeline.";
        return false;
    }
    for (const Track* t : armed) {
        if (t->kind == TrackKind::Midi && t->midiPort.empty()) {
            error = "Track " + std::to_string(t->number) + " has no MIDI input port chosen.";
            return false;
        }
        if (t->kind == TrackKind::Midi && !midi.isOpen(t->midiPort)) {
            error = "Track " + std::to_string(t->number) + ": MIDI input \"" + t->midiPort + "\" is not open.";
            return false;
        }
        if (t->kind == TrackKind::Audio && audio.info().inputChannels == 0) {
            error = "Track " + std::to_string(t->number) + ": there is no audio input device.";
            return false;
        }
    }

    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    const std::filesystem::path folder =
        createTakeFolder(recordingFolder(settings), takeFolderName(local), error);
    if (folder.empty()) return false;

    settings_ = settings;
    audio_ = &audio;
    midi_ = &midi;
    sampleRate_ = audio.info().sampleRate;
    folder_ = folder;
    takeName_ = folder.filename().string();

    std::vector<TakeSpec> audioTakes;
    midiTakes_.clear();
    portEvents_.clear();
    for (const Track* t : armed) {
        const auto path = folder / takeFileName(*t);
        if (t->kind == TrackKind::Audio) {
            audioTakes.push_back({path, t->channels});
        } else {
            midiTakes_.push_back({*t, path});
            const bool known = std::any_of(portEvents_.begin(), portEvents_.end(),
                                           [&](const PortEvents& p) { return p.port == t->midiPort; });
            if (!known) portEvents_.push_back({t->midiPort, {}});
        }
    }

    auto fail = [&](const std::string& text) {
        error = text;
        std::error_code ec;
        std::filesystem::remove_all(folder, ec);
        folder_.clear();
        return false;
    };

    hasAudio_ = !audioTakes.empty();
    midiMessages_.store(0);
    if (!midiTakes_.empty()) midi.setCapture(true);
    if (hasAudio_) {
        RecorderOptions options;
        options.syncToDisk = settings.syncToDisk;
        std::string err;
        if (!audio.startRecording(audioTakes, err, options)) {
            midi.setCapture(false);
            return fail(err);
        }
    } else {
        const auto f = audio.currentFrame();
        midiOnlyStart_ = f ? *f : 0;
    }
    if (!midiTakes_.empty()) {
        midiRun_.store(true);
        midiThread_ = std::thread(&RecordingSession::midiLoop, this);
    }
    active_ = true;
    return true;
}

std::optional<std::uint64_t> RecordingSession::timelineStart() const
{
    if (!hasAudio_) return midiOnlyStart_;
    return audio_->recorder().startFrame();  // known after the first recorded block
}

void RecordingSession::collectMidi()
{
    RawMidi buf[256];
    for (auto& pe : portEvents_) {
        for (;;) {
            const std::size_t n = midi_->read(pe.port, buf, 256);
            for (std::size_t i = 0; i < n; ++i) {
                const auto frame = audio_->frameAtTime(buf[i].ns);
                if (!frame) continue;
                pe.events.push_back({*frame, buf[i].data, buf[i].size});
            }
            midiMessages_.fetch_add(n, std::memory_order_relaxed);
            if (n < 256) break;
        }
    }
}

bool RecordingSession::writeMidiFiles(std::uint64_t endFrame, bool final, std::vector<MidiTakeResult>* results)
{
    const auto start = timelineStart();
    if (!start) return true;  // the first audio block has not arrived yet
    bool ok = true;
    for (const auto& mt : midiTakes_) {
        const auto pe = std::find_if(portEvents_.begin(), portEvents_.end(),
                                     [&](const PortEvents& p) { return p.port == mt.track.midiPort; });
        SmfTrack st;
        st.name = mt.track.name;
        st.events = buildMidiTake(pe->events, mt.track.midiChannel, *start, endFrame);
        std::string err;
        const bool sync = settings_.syncToDisk || final;
        if (!writeSmf(mt.path, takeName_, {st}, sampleRate_, settings_.midiTiming, endFrame - std::min(endFrame, *start),
                      sync, err)) {
            setError(err);
            ok = false;
        }
        if (results) results->push_back({mt.path, mt.track.midiPort, st.events.size(), midi_->messagesLost(mt.track.midiPort)});
    }
    return ok;
}

void RecordingSession::midiLoop()
{
    auto lastWrite = std::chrono::steady_clock::now();
    std::size_t written = 0;
    while (midiRun_.load()) {
        collectMidi();
        const auto now = std::chrono::steady_clock::now();
        if (now - lastWrite >= std::chrono::seconds(1)) {
            lastWrite = now;
            const std::size_t seen = midiMessages_.load(std::memory_order_relaxed);
            const auto end = audio_->currentFrame();
            if (seen != written && end) {
                if (writeMidiFiles(*end, false, nullptr)) written = seen;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void RecordingSession::setError(const std::string& text)
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    if (error_.empty()) error_ = text;
}

double RecordingSession::elapsedSeconds() const
{
    if (!active_ || sampleRate_ == 0) return 0.0;
    if (hasAudio_) return static_cast<double>(audio_->recorder().framesCaptured()) / sampleRate_;
    const auto f = audio_->currentFrame();
    return f && *f > midiOnlyStart_ ? static_cast<double>(*f - midiOnlyStart_) / sampleRate_ : 0.0;
}

std::uint64_t RecordingSession::lostFrames() const
{
    return active_ && hasAudio_ ? audio_->recorder().droppedFrames() : 0;
}

std::string RecordingSession::diskError() const
{
    std::string e;
    if (active_ && hasAudio_) e = audio_->recorder().diskError();
    if (e.empty()) {
        std::lock_guard<std::mutex> lock(errorMutex_);
        e = error_;
    }
    return e;
}

SessionResult RecordingSession::stop()
{
    SessionResult result;
    if (!active_) return result;
    result.folder = folder_;

    // End of the take: the audio length, or "now" for a MIDI-only recording.
    std::uint64_t endFrame = 0;
    std::optional<std::uint64_t> start = timelineStart();
    if (hasAudio_) {
        const std::uint64_t first = start ? *start : 0;
        result.audio = audio_->stopRecording();
        const std::uint64_t frames = result.audio.takes.empty() ? 0 : result.audio.takes.front().frames;
        endFrame = first + frames;
    } else {
        const auto f = audio_->currentFrame();
        endFrame = f ? *f : midiOnlyStart_;
    }

    if (midiThread_.joinable()) {
        midiRun_.store(false);
        midiThread_.join();
    }
    if (!midiTakes_.empty()) {
        collectMidi();
        midi_->setCapture(false);
        if (!start) start = timelineStart();
        if (start) {
            writeMidiFiles(endFrame, true, &result.midi);
        } else {
            setError("No audio arrived, so the MIDI takes could not be placed on the timeline.");
        }
    }
    if (sampleRate_ > 0 && start) result.seconds = static_cast<double>(endFrame - std::min(endFrame, *start)) / sampleRate_;
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        result.error = error_;
    }
    active_ = false;
    midiTakes_.clear();
    portEvents_.clear();
    return result;
}

} // namespace minidaw
