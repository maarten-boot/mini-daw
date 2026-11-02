#include "minidaw/midi_player.hpp"

#include "console_capture.hpp"
#include "minidaw/midi_input.hpp"

#include <RtMidi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <tuple>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace minidaw {

namespace {

class RtMidiOutputSink : public MidiOutputSink {
public:
    std::vector<std::string> ports() override
    {
        std::vector<std::string> names;
        detail::ConsoleCapture capture;
        try {
            if (!scanner_) scanner_ = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "mini-daw scan");
            for (unsigned int i = 0; i < scanner_->getPortCount(); ++i) {
                std::string name = scanner_->getPortName(i);
                if (!isOwnMidiPort(name)) names.push_back(std::move(name));
            }
        } catch (const RtMidiError&) {
        }
        return names;
    }
    int open(const std::string& name, std::string& error) override
    {
        detail::ConsoleCapture capture;
        try {
            auto out = std::make_unique<RtMidiOut>();
            std::vector<std::string> names;
            for (unsigned int i = 0; i < out->getPortCount(); ++i) names.push_back(out->getPortName(i));
            const auto index = matchMidiPort(names, name);
            if (!index) {
                error = "MIDI output \"" + name + "\" was not found.";
                return -1;
            }
            out->openPort(static_cast<unsigned int>(*index), "mini-daw");
            if (!out->isPortOpen()) {
                error = "MIDI output \"" + name + "\" could not be opened.";
                return -1;
            }
            outs_.push_back(std::move(out));
            return static_cast<int>(outs_.size()) - 1;
        } catch (const RtMidiError& e) {
            error = "MIDI output \"" + name + "\": " + e.getMessage();
            return -1;
        }
    }
    void send(int handle, const std::uint8_t* data, std::size_t size) override
    {
        if (handle < 0 || static_cast<std::size_t>(handle) >= outs_.size()) return;
        try {
            outs_[static_cast<std::size_t>(handle)]->sendMessage(data, size);
        } catch (const RtMidiError&) {
            // An unplugged output: nothing more to do while playing.
        }
    }
    void closeAll() override
    {
        detail::ConsoleCapture capture;
        outs_.clear();
    }

private:
    std::unique_ptr<RtMidiOut> scanner_;
    std::vector<std::unique_ptr<RtMidiOut>> outs_;
};

} // namespace

std::unique_ptr<MidiOutputSink> makeRtMidiOutputSink()
{
    return std::make_unique<RtMidiOutputSink>();
}

MidiPlayer::MidiPlayer(std::unique_ptr<MidiOutputSink> sink) : sink_(std::move(sink)) {}

MidiPlayer::~MidiPlayer()
{
    stop();
}

void MidiPlayer::setGain(int track, float gain)
{
    requested_[track] = gain;
    const auto it = gains_.find(track);
    if (it != gains_.end()) it->second->store(gain, std::memory_order_relaxed);
}

void MidiPlayer::prepare(const std::vector<MidiPlaybackTrack>& tracks, std::int64_t position,
                         std::vector<std::string>& warnings)
{
    stop();
    items_.clear();
    next_ = 0;
    held_.clear();
    channelsUsed_.clear();
    gains_.clear();
    gainStore_.clear();
    lastGain_.clear();
    sent_.store(0);
    std::map<std::string, int> handles;
    for (const auto& t : tracks) {
        if (t.port.empty() || t.events.empty()) continue;
        int handle = -1;
        const auto known = handles.find(t.port);
        if (known != handles.end()) {
            handle = known->second;
        } else {
            std::string err;
            handle = sink_->open(t.port, err);
            if (handle < 0) {
                warnings.push_back("Track " + std::to_string(t.track) + ": " + err);
                handles[t.port] = -1;
                continue;
            }
            handles[t.port] = handle;
        }
        if (handle < 0) continue;
        if (!gains_.count(t.track)) {
            gainStore_.push_back(std::make_unique<std::atomic<float>>(
                requested_.count(t.track) ? requested_[t.track] : 1.0f));
            gains_[t.track] = gainStore_.back().get();
            lastGain_[t.track] = gains_.at(t.track)->load();
        }
        for (const auto& e : t.events) {
            const std::int64_t frame = t.start + static_cast<std::int64_t>(e.frame);
            if (frame < position) continue;  // before the start: skipped
            items_.push_back({frame, handle, t.track, e.data, e.size});
            channelsUsed_[handle] |= static_cast<std::uint16_t>(1u << (e.data[0] & 0x0F));
        }
    }
    std::stable_sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) { return a.frame < b.frame; });
}

void MidiPlayer::start(const std::vector<MidiPlaybackTrack>& tracks, std::int64_t position, Clock clock,
                       std::vector<std::string>& warnings)
{
    prepare(tracks, position, warnings);
    if (items_.empty()) return;
    clock_ = std::move(clock);
#if defined(_WIN32)
    timeBeginPeriod(1);
#endif
    run_.store(true);
    thread_ = std::thread(&MidiPlayer::loop, this);
}

void MidiPlayer::startForTest(const std::vector<MidiPlaybackTrack>& tracks, std::int64_t position,
                              std::vector<std::string>& warnings)
{
    prepare(tracks, position, warnings);
    testMode_ = true;
}

void MidiPlayer::stop()
{
    if (thread_.joinable()) {
        run_.store(false);
        thread_.join();
#if defined(_WIN32)
        timeEndPeriod(1);
#endif
    }
    if (thread_.joinable() || testMode_ || !held_.empty() || !channelsUsed_.empty()) releaseAll();
    testMode_ = false;
    channelsUsed_.clear();
    sink_->closeAll();
}

void MidiPlayer::sendItem(const Item& it)
{
    std::array<std::uint8_t, 3> d = it.data;
    const std::uint8_t kind = d[0] & 0xF0;
    const int channel = d[0] & 0x0F;
    const auto key = std::make_tuple(it.handle, channel, static_cast<int>(d[1]));
    const float gain = gains_.count(it.track) ? gains_.at(it.track)->load(std::memory_order_relaxed) : 1.0f;
    const bool noteOn = kind == 0x90 && d[2] > 0;
    const bool noteOff = kind == 0x80 || (kind == 0x90 && d[2] == 0);
    if (noteOn) {
        if (gain <= 0.0f) return;  // muted: no new notes
        const long v = std::lround(d[2] * std::min(gain, 2.0f));
        d[2] = static_cast<std::uint8_t>(std::clamp(v, 1L, 127L));
        held_[key] = it.track;
    } else if (noteOff) {
        if (!held_.erase(key)) return;  // released already (mute) or never sent
    } else if (gain <= 0.0f) {
        return;  // a muted track sends nothing else either
    }
    sink_->send(it.handle, d.data(), it.size);
    sent_.fetch_add(1, std::memory_order_relaxed);
}

void MidiPlayer::releaseTrack(int track)
{
    for (auto it = held_.begin(); it != held_.end();) {
        if (it->second != track) { ++it; continue; }
        const auto [handle, channel, note] = it->first;
        const std::uint8_t off[3] = {static_cast<std::uint8_t>(0x80 | channel), static_cast<std::uint8_t>(note), 0};
        sink_->send(handle, off, 3);
        it = held_.erase(it);
    }
}

void MidiPlayer::releaseAll()
{
    for (const auto& [key, track] : held_) {
        const auto [handle, channel, note] = key;
        const std::uint8_t off[3] = {static_cast<std::uint8_t>(0x80 | channel), static_cast<std::uint8_t>(note), 0};
        sink_->send(handle, off, 3);
    }
    held_.clear();
    for (const auto& [handle, bits] : channelsUsed_) {
        for (int ch = 0; ch < 16; ++ch) {
            if (!(bits & (1u << ch))) continue;
            const std::uint8_t allOff[3] = {static_cast<std::uint8_t>(0xB0 | ch), 123, 0};    // all notes off
            const std::uint8_t pedalUp[3] = {static_cast<std::uint8_t>(0xB0 | ch), 64, 0};    // sustain off
            sink_->send(handle, allOff, 3);
            sink_->send(handle, pedalUp, 3);
        }
    }
}

void MidiPlayer::step(std::int64_t now)
{
    // A track that was muted since the last step releases its notes.
    for (auto& [track, last] : lastGain_) {
        const float g = gains_.at(track)->load(std::memory_order_relaxed);
        if (g <= 0.0f && last > 0.0f) releaseTrack(track);
        last = g;
    }
    while (next_ < items_.size() && items_[next_].frame <= now) sendItem(items_[next_++]);
}

void MidiPlayer::loop()
{
    while (run_.load(std::memory_order_relaxed)) {
        if (const auto now = clock_()) step(*now);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace minidaw
