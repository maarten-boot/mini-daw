#include "minidaw/recording_session.hpp"

#include "minidaw/path_util.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

namespace minidaw {

bool SessionResult::lostData() const
{
    if (!error.empty() || audio.lostData()) return true;
    return std::any_of(midi.begin(), midi.end(), [](const MidiTakeResult& m) { return m.lostMessages > 0 || m.disconnects > 0; });
}

RecordingSession::~RecordingSession()
{
    if (active_) stop();
}

bool RecordingSession::start(const std::vector<Track>& tracks, const RecordingSettings& settings, SessionAudio& audio,
                             SessionMidi& midi, std::string& error)
{
    return begin(tracks, settings, audio, midi, error, false);
}

bool RecordingSession::startAuto(const std::vector<Track>& tracks, const RecordingSettings& settings,
                                 SessionAudio& audio, SessionMidi& midi, std::string& error)
{
    return begin(tracks, settings, audio, midi, error, true);
}

bool RecordingSession::begin(const std::vector<Track>& tracks, const RecordingSettings& settings, SessionAudio& audio,
                             SessionMidi& midi, std::string& error, bool autoMode)
{
    if (active_) stop();
    error.clear();
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        error_.clear();
        midiRetryError_.clear();
    }
    std::vector<const Track*> armed;
    for (const auto& t : tracks) {
        // A linked sound track records with its MIDI track, never on its own (R9).
        if (t.armed && !(t.kind == TrackKind::Audio && t.instrumentOf > 0)) armed.push_back(&t);
    }
    if (armed.empty()) {
        error = "No track is armed. Arm at least one track to record.";
        return false;
    }
    if (!audio.streamRunning() || audio.streamSampleRate() == 0) {
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
        if (t->kind == TrackKind::Audio && audio.streamInputChannels() == 0) {
            error = "Track " + std::to_string(t->number) + ": there is no audio input device.";
            return false;
        }
    }

    // Auto record: a folder per part, made when it begins; the take paths
    // below are then only file names.
    std::filesystem::path folder;
    if (autoMode) {
        autoBase_ = recordingFolder(settings) / "auto";
    } else {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        folder = createTakeFolder(recordingFolder(settings), takeFolderName(local), error);
        if (folder.empty()) return false;
    }

    settings_ = settings;
    audio_ = &audio;
    midi_ = &midi;
    sampleRate_ = audio.streamSampleRate();
    folder_ = autoMode ? autoBase_ : folder;
    takeName_ = pathToUtf8(folder.filename());
    auto_ = autoMode;
    audioFileNames_.clear();

    std::vector<TakeSpec> audioTakes;
    midiTakes_.clear();
    portEvents_.clear();
    for (const Track* t : armed) {
        const auto path = folder / pathFromUtf8(takeFileName(*t));
        if (t->kind == TrackKind::Audio) {
            audioTakes.push_back({path, t->channels});
        } else {
            // An instrument track with a linked audio track: its sound is
            // recorded too, into that track's file (P11, SPEC-VST-03a).
            if (!t->instrumentClass.empty()) {
                if (const Track* linked = linkedSoundTrack(tracks, t->number)) {
                    TakeSpec spec;
                    spec.path = folder / pathFromUtf8(takeFileName(*linked));
                    spec.instrumentTrack = t->number;
                    audioTakes.push_back(spec);
                }
            }
            midiTakes_.push_back({*t, path, midi.disconnects(t->midiPort)});
            const bool known = std::any_of(portEvents_.begin(), portEvents_.end(),
                                           [&](const PortEvents& p) { return p.port == t->midiPort; });
            if (!known) portEvents_.push_back({t->midiPort, {}, 0, {}});
        }
    }

    for (const auto& t : audioTakes) audioFileNames_.push_back(t.path.filename());

    auto fail = [&](const std::string& text) {
        error = text;
        std::error_code ec;
        if (!autoMode) std::filesystem::remove_all(folder, ec);
        folder_.clear();
        auto_ = false;
        return false;
    };

    hasAudio_ = !audioTakes.empty();
    midiMessages_.store(0);
    midiUnplaced_.store(0);
    if (!midiTakes_.empty()) midi.setCapture(true);
    if (hasAudio_) {
        RecorderOptions options;
        options.syncToDisk = settings.syncToDisk;
        if (autoMode) {
            options.parts = true;
            options.partBufferSeconds = 4.0;  // 1 s preroll plus room for a slow GUI frame
            options.activityLevel = static_cast<float>(std::pow(10.0, settings.autoThresholdDb / 20.0));
        }
        std::string err;
        if (!audio.startRecording(audioTakes, err, options)) {
            midi.setCapture(false);
            return fail(err);
        }
    } else {
        const auto f = audio.currentFrame();
        midiOnlyStart_ = f ? *f : 0;
    }
    if (autoMode) {
        tracker_ = AutoPartTracker(autoRecordTiming(settings.autoPauseSeconds, sampleRate_));
        const auto now = audio.currentFrame();
        tracker_.reset(now ? *now : 0, 0);
        partId_ = 0;
        partsDone_ = 0;
        partLost_ = 0;
        partFolder_.clear();
        {
            std::lock_guard<std::mutex> lock(partMutex_);
            midiPartOpen_ = false;
            midiJobs_.clear();
        }
        keepFrom_.store(0);
        midiEarliest_.store(UINT64_MAX);
        midiLastActivity_.store(0);
        midiHeld_.store(0);
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
                if (!frame) {  // no timeline (stream not running): counted, not silently dropped
                    ++pe.unplaced;
                    midiUnplaced_.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                const TimedMidi m{*frame, buf[i].data, buf[i].size};
                if (auto_) noteActivity(pe.held, m);
                if (pe.events.empty() || pe.events.back().frame <= m.frame) {
                    pe.events.push_back(m);
                } else {  // slightly out of order (clock adjustments): keep the list sorted
                    const auto at = std::upper_bound(pe.events.begin(), pe.events.end(), m,
                                                     [](const TimedMidi& a, const TimedMidi& b) { return a.frame < b.frame; });
                    pe.events.insert(at, m);
                }
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
    std::string firstError;
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
            if (firstError.empty()) firstError = err;
            ok = false;
        }
        if (results) {
            results->push_back({mt.path, mt.track.midiPort, st.events.size(),
                                midi_->messagesLost(mt.track.midiPort) + pe->unplaced,
                                midi_->disconnects(mt.track.midiPort) - mt.disconnectsAtStart});
        }
    }
    if (final) {
        if (!ok) setError(firstError);
    } else {
        // A failed periodic write is retried a second later; it only stays
        // an error if the final write fails too (J10).
        std::lock_guard<std::mutex> lock(errorMutex_);
        midiRetryError_ = ok ? std::string() : firstError + " (retrying)";
    }
    return ok;
}

void RecordingSession::midiLoop()
{
    auto lastWrite = std::chrono::steady_clock::now();
    std::size_t written = 0;
    while (midiRun_.load()) {
        collectMidi();
        if (auto_) {
            runMidiPartJobs(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
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
        e = !error_.empty() ? error_ : midiRetryError_;
    }
    return e;
}

std::uint64_t RecordingSession::midiLost() const
{
    if (!active_) return 0;
    std::uint64_t n = midiUnplaced_.load(std::memory_order_relaxed);
    for (const auto& mt : midiTakes_) n += midi_->messagesLost(mt.track.midiPort);
    return n;
}

SessionResult RecordingSession::stop()
{
    SessionResult result;
    if (!active_) return result;
    result.folder = folder_;
    if (auto_) {
        // End a part that is open, now.
        if (const auto now = audio_->currentFrame()) {
            const AutoPartTracker::Action a = tracker_.finish(*now);
            if (a.kind == AutoPartTracker::Kind::End) endAutoPart(a);
        } else if (tracker_.inPart()) {
            endAutoPart(tracker_.finish(partFrom_));
        }
        if (hasAudio_) result.audio = audio_->stopRecording();
        collectPartResults();
        if (midiThread_.joinable()) {
            midiRun_.store(false);
            midiThread_.join();
        }
        if (!midiTakes_.empty()) {
            collectMidi();
            midi_->setCapture(false);
            runMidiPartJobs(true);
            for (const auto& mt : midiTakes_) {
                result.midi.push_back({mt.path, mt.track.midiPort, 0, midi_->messagesLost(mt.track.midiPort),
                                       midi_->disconnects(mt.track.midiPort) - mt.disconnectsAtStart});
            }
        }
        {
            std::lock_guard<std::mutex> lock(errorMutex_);
            result.error = error_;
            midiRetryError_.clear();
        }
        active_ = false;
        auto_ = false;
        midiTakes_.clear();
        portEvents_.clear();
        return result;
    }

    // End of the take: the audio timeline (all frames the recorder took in,
    // whatever happened to single files: J4), or "now" for a MIDI-only
    // recording. The start is read after the recorder has stopped (J5).
    std::uint64_t endFrame = 0;
    std::optional<std::uint64_t> start;
    bool haveEnd = true;
    if (hasAudio_) {
        result.audio = audio_->stopRecording();
        start = timelineStart();
        endFrame = (start ? *start : 0) + audio_->recorder().framesCaptured();
    } else {
        start = midiOnlyStart_;
        const auto f = audio_->currentFrame();
        haveEnd = f.has_value();
        endFrame = f ? *f : midiOnlyStart_;
    }

    if (midiThread_.joinable()) {
        midiRun_.store(false);
        midiThread_.join();
    }
    if (!midiTakes_.empty()) {
        collectMidi();
        midi_->setCapture(false);
        if (!start) {
            setError("No audio arrived, so the MIDI takes could not be placed on the timeline.");
        } else if (!haveEnd) {
            // The stream stopped during the recording: keep the files of the
            // last periodic write instead of replacing them (J8).
            setError("The audio stream stopped during the recording; the MIDI files hold the state of the last "
                     "update (about one second before).");
        } else {
            writeMidiFiles(endFrame, true, &result.midi);
        }
    }
    result.startFrame = start;
    if (sampleRate_ > 0 && start) result.seconds = static_cast<double>(endFrame - std::min(endFrame, *start)) / sampleRate_;
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        result.error = error_;
        midiRetryError_.clear();
    }
    active_ = false;
    midiTakes_.clear();
    portEvents_.clear();
    return result;
}

// --- Auto record (P16, SPEC-REC-08) ---------------------------------------------

void RecordingSession::noteActivity(PortEventsHeld& held, const TimedMidi& m)
{
    const std::uint8_t status = m.data[0];
    if (status < 0x80 || status >= 0xF0) return;  // only channel messages are playing
    const int ch = status & 0x0F;
    const int type = status & 0xF0;
    if ((type == 0x90 && m.size >= 3 && m.data[2] > 0) && held.down[ch * 128 + (m.data[1] & 0x7F)] == 0) {
        held.down[ch * 128 + (m.data[1] & 0x7F)] = 1;
        ++held.count;
        midiHeld_.fetch_add(1, std::memory_order_relaxed);
    } else if ((type == 0x80 || (type == 0x90 && m.size >= 3 && m.data[2] == 0)) && m.size >= 2 &&
               held.down[ch * 128 + (m.data[1] & 0x7F)] != 0) {
        held.down[ch * 128 + (m.data[1] & 0x7F)] = 0;
        --held.count;
        midiHeld_.fetch_sub(1, std::memory_order_relaxed);
    } else if (type == 0xB0 && m.size >= 3 && m.data[1] == 64) {  // sustain pedal
        const bool on = m.data[2] >= 64;
        if (on != held.sustain[ch]) {
            held.sustain[ch] = on;
            held.count += on ? 1 : -1;
            midiHeld_.fetch_add(on ? 1 : -1, std::memory_order_relaxed);
        }
    }
    std::uint64_t e = midiEarliest_.load(std::memory_order_relaxed);
    while (m.frame < e && !midiEarliest_.compare_exchange_weak(e, m.frame, std::memory_order_relaxed)) {
    }
    if (m.frame > midiLastActivity_.load(std::memory_order_relaxed)) {
        midiLastActivity_.store(m.frame, std::memory_order_relaxed);
    }
}

void RecordingSession::writeMidiPart(const MidiPart& part, bool final)
{
    std::string firstError;
    const std::string name = pathToUtf8(part.folder.filename());
    for (const auto& mt : midiTakes_) {
        const auto pe = std::find_if(portEvents_.begin(), portEvents_.end(),
                                     [&](const PortEvents& p) { return p.port == mt.track.midiPort; });
        if (pe == portEvents_.end()) continue;
        SmfTrack st;
        st.name = mt.track.name;
        st.events = buildMidiTake(pe->events, mt.track.midiChannel, part.from, part.to);
        std::string err;
        if (!writeSmf(part.folder / mt.path.filename(), name, {st}, sampleRate_, settings_.midiTiming,
                      part.to - std::min(part.to, part.from), settings_.syncToDisk || final, err)) {
            if (firstError.empty()) firstError = err;
        }
    }
    if (final) {
        if (!firstError.empty()) setError(firstError);
    } else {
        std::lock_guard<std::mutex> lock(errorMutex_);
        midiRetryError_ = firstError.empty() ? std::string() : firstError + " (retrying)";
    }
}

// MIDI thread (and stop() after it ended): write ended parts, refresh the open
// one about once a second, forget events no part can need any more.
void RecordingSession::runMidiPartJobs(bool final)
{
    static thread_local auto lastWrite = std::chrono::steady_clock::now();
    std::vector<MidiPart> jobs;
    bool open = false;
    MidiPart openPart;
    {
        std::lock_guard<std::mutex> lock(partMutex_);
        jobs.swap(midiJobs_);
        open = midiPartOpen_;
        openPart = midiOpenPart_;
    }
    for (const auto& j : jobs) writeMidiPart(j, true);
    const auto now = std::chrono::steady_clock::now();
    if (!final && open && now - lastWrite >= std::chrono::seconds(1)) {
        lastWrite = now;
        if (const auto f = audio_->currentFrame()) {
            openPart.to = *f;
            writeMidiPart(openPart, false);
        }
    }
    const std::uint64_t keep = keepFrom_.load(std::memory_order_relaxed);
    for (auto& pe : portEvents_) {
        const auto first = std::lower_bound(pe.events.begin(), pe.events.end(), keep,
                                            [](const TimedMidi& m, std::uint64_t f) { return m.frame < f; });
        if (first != pe.events.begin()) pe.events.erase(pe.events.begin(), first);
    }
}

std::uint64_t RecordingSession::lastActivity() const
{
    std::uint64_t f = midiLastActivity_.load(std::memory_order_relaxed);
    if (hasAudio_) f = std::max(f, audio_->recorder().lastActiveFrame());
    return f;
}

void RecordingSession::collectPartResults()
{
    if (!hasAudio_) return;
    for (const auto& r : audio_->recorder().takeFinishedParts()) {
        partLost_ += r.lostFrames;
        if (!r.error.empty()) setError(r.error);
    }
}

AutoPartEvent RecordingSession::endAutoPart(const AutoPartTracker::Action& a)
{
    AutoPartEvent ev;
    if (hasAudio_) {
        const auto start = audio_->recorder().startFrame();
        const std::uint64_t s0 = start ? *start : 0;
        audio_->recorder().endPart(a.frame > s0 ? a.frame - s0 : 0);
    }
    {
        std::lock_guard<std::mutex> lock(partMutex_);
        if (midiPartOpen_) {
            MidiPart job = midiOpenPart_;
            job.to = a.frame;
            midiJobs_.push_back(job);
            midiPartOpen_ = false;
        }
    }
    ++partsDone_;
    ev.kind = AutoPartEvent::Kind::Ended;
    ev.folder = partFolder_;
    ev.seconds = sampleRate_ > 0 ? static_cast<double>(a.frame - std::min(a.frame, partFrom_)) / sampleRate_ : 0.0;
    return ev;
}

AutoPartEvent RecordingSession::autoUpdate()
{
    if (!autoMode()) return {};
    collectPartResults();
    const auto now = audio_->currentFrame();
    if (!now) return {};
    std::optional<std::uint64_t> recStart;
    if (hasAudio_) {
        recStart = audio_->recorder().startFrame();
        if (!recStart) return {};  // the first block has not arrived yet
    }
    const AutoPartTracker::Action a = tracker_.update(*now, lastActivity(), midiHeld_.load(std::memory_order_relaxed) > 0,
                                                      midiEarliest_.exchange(UINT64_MAX, std::memory_order_relaxed));
    AutoPartEvent ev;
    if (a.kind == AutoPartTracker::Kind::Begin) {
        const std::time_t wall = std::time(nullptr);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &wall);
#else
        localtime_r(&wall, &local);
#endif
        std::string err;
        const std::filesystem::path folder = createTakeFolder(autoBase_, autoPartName(local), err);
        if (folder.empty()) {
            tracker_.finish(*now);  // try again at the next playing
            setError(err);
            ev.kind = AutoPartEvent::Kind::Failed;
            ev.error = err;
            return ev;
        }
        partFolder_ = folder;
        // Not before auto record was switched on: nothing was recorded then.
        partFrom_ = std::max(a.frame, hasAudio_ ? *recStart : midiOnlyStart_);
        ++partId_;
        if (hasAudio_) {
            std::vector<std::filesystem::path> paths;
            for (const auto& n : audioFileNames_) paths.push_back(folder / n);
            audio_->recorder().beginPart(partId_, partFrom_ - *recStart, paths);
        }
        {
            std::lock_guard<std::mutex> lock(partMutex_);
            midiPartOpen_ = true;
            midiOpenPart_ = {folder, partFrom_, partFrom_};
        }
        ev.kind = AutoPartEvent::Kind::Began;
        ev.folder = folder;
    } else if (a.kind == AutoPartTracker::Kind::End) {
        ev = endAutoPart(a);
    }
    // MIDI events a part may still need: from the open part's start, or a
    // few seconds back while waiting (the preroll).
    const std::uint64_t back = 5ull * sampleRate_;
    keepFrom_.store(tracker_.inPart() ? tracker_.partStart() : (*now > back ? *now - back : 0),
                    std::memory_order_relaxed);
    return ev;
}

double RecordingSession::autoPartSeconds() const
{
    if (!autoInPart() || sampleRate_ == 0) return 0.0;
    const auto now = audio_->currentFrame();
    return now && *now > partFrom_ ? static_cast<double>(*now - partFrom_) / sampleRate_ : 0.0;
}

double RecordingSession::autoQuietSeconds() const
{
    if (!autoMode() || sampleRate_ == 0) return 0.0;
    const auto now = audio_->currentFrame();
    const std::uint64_t last = lastActivity();
    return now && *now > last ? static_cast<double>(*now - last) / sampleRate_ : 0.0;
}

} // namespace minidaw
