// Recording sessions (SPEC-REC-01..04, 04a-c, 05b, 07): T-REC-23 .. T-REC-26.
// The real RecordingSession and Recorder run against stand-ins for the audio
// stream and the MIDI inputs. In the stand-in a MIDI message's clock time
// *is* its stream frame, so positions are exact.

#include "minidaw/midi_input.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/recording_session.hpp"
#include "minidaw/smf.hpp"
#include "minidaw/wav.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

using namespace minidaw;

namespace {

class FakeAudio : public SessionAudio {
public:
    std::atomic<bool> running{true};
    unsigned int rate = 48000;
    unsigned int inputs = 2;
    Recorder rec;
    std::atomic<std::uint64_t> pos{1000};  // the stream ran for a while before Record

    bool streamRunning() const override { return running; }
    unsigned int streamSampleRate() const override { return rate; }
    unsigned int streamInputChannels() const override { return inputs; }
    bool startRecording(const std::vector<TakeSpec>& takes, std::string& error,
                        const RecorderOptions& options) override
    {
        RecorderOptions o = options;
        o.startDiskThread = false;  // the test decides when the disk catches up
        o.syncToDisk = false;
        return rec.start(takes, rate, inputs, error, o);
    }
    RecordingResult stopRecording() override { return rec.stop(); }
    const Recorder& recorder() const override { return rec; }
    std::optional<std::uint64_t> frameAtTime(std::int64_t ns) const override
    {
        if (!running || ns < 0) return std::nullopt;
        return static_cast<std::uint64_t>(ns);
    }
    std::optional<std::uint64_t> currentFrame() const override
    {
        if (!running) return std::nullopt;
        return pos.load();
    }

    // One audio block from the "audio thread".
    void feed(unsigned int frames, float value = 0.25f)
    {
        std::vector<float> buf(static_cast<std::size_t>(frames) * inputs, value);
        rec.process(buf.data(), frames, inputs, pos.load());
        pos += frames;
    }
};

class FakeMidi : public SessionMidi {
public:
    bool isOpen(const std::string& port) const override
    {
        std::lock_guard<std::mutex> lock(m_);
        return open_.count(port) != 0;
    }
    void setCapture(bool on) override
    {
        std::lock_guard<std::mutex> lock(m_);
        capture_ = on;
    }
    std::size_t read(const std::string& port, RawMidi* out, std::size_t max) override
    {
        std::lock_guard<std::mutex> lock(m_);
        auto& q = queues_[port];
        std::size_t n = 0;
        while (n < max && !q.empty()) {
            out[n++] = q.front();
            q.pop_front();
        }
        return n;
    }
    std::uint64_t messagesLost(const std::string&) const override { return 0; }
    std::uint64_t disconnects(const std::string& port) const override
    {
        std::lock_guard<std::mutex> lock(m_);
        const auto it = disconnects_.find(port);
        return it == disconnects_.end() ? 0 : it->second;
    }

    void openPort(const std::string& port)
    {
        std::lock_guard<std::mutex> lock(m_);
        open_[port] = true;
    }
    // The device goes away and (optionally) comes back, as MidiInputs reports it.
    void unplug(const std::string& port)
    {
        std::lock_guard<std::mutex> lock(m_);
        ++disconnects_[port];
    }
    // A message "arriving" at clock time `ns` (= stream frame in this test).
    void send(const std::string& port, std::int64_t ns, std::uint8_t a, std::uint8_t b, std::uint8_t c)
    {
        std::lock_guard<std::mutex> lock(m_);
        if (!capture_) return;
        RawMidi m;
        m.ns = ns;
        m.data = {a, b, c};
        m.size = 3;
        queues_[port].push_back(m);
    }

private:
    mutable std::mutex m_;
    std::map<std::string, bool> open_;
    std::map<std::string, std::deque<RawMidi>> queues_;
    std::map<std::string, std::uint64_t> disconnects_;
    bool capture_ = false;
};

Track audioTrack(int number, std::string name, std::vector<unsigned int> ch, bool armed = true)
{
    Track t;
    t.number = number;
    t.name = std::move(name);
    t.kind = TrackKind::Audio;
    t.channels = std::move(ch);
    t.armed = armed;
    return t;
}

Track midiTrack(int number, std::string name, std::string port, int channel = 0, bool armed = true)
{
    Track t;
    t.number = number;
    t.name = std::move(name);
    t.kind = TrackKind::Midi;
    t.midiPort = std::move(port);
    t.midiChannel = channel;
    t.armed = armed;
    return t;
}

std::vector<SmfReadEvent> notes(const SmfData& d)
{
    std::vector<SmfReadEvent> out;
    for (const auto& e : d.tracks.at(1)) {
        if (!e.meta) out.push_back(e);
    }
    return out;
}

std::uint32_t endTick(const SmfData& d)
{
    for (const auto& e : d.tracks.at(1)) {
        if (e.meta && e.metaType == 0x2F) return e.tick;
    }
    return 0;
}

RecordingSettings settingsFor(const test::TempDir& dir)
{
    RecordingSettings s;
    s.folder = pathToUtf8(dir.path());
    s.syncToDisk = false;
    return s;
}

std::size_t entries(const std::filesystem::path& p)
{
    std::size_t n = 0;
    for (const auto& e : std::filesystem::directory_iterator(p)) {
        (void)e;
        ++n;
    }
    return n;
}

} // namespace

TEST_CASE("T-REC-23: audio and MIDI tracks record together on one timeline", "[session][T-REC-23]")
{
    test::TempDir dir;
    FakeAudio audio;
    FakeMidi midi;
    midi.openPort("Keys");
    const std::vector<Track> tracks = {audioTrack(1, "Gtr", {0, 1}), midiTrack(2, "All", "Keys"),
                                       midiTrack(3, "Ch 2", "Keys", 2), audioTrack(4, "Off", {0}, false)};
    RecordingSession session;
    std::string err;
    REQUIRE(session.start(tracks, settingsFor(dir), audio, midi, err));
    REQUIRE(session.recording());

    midi.send("Keys", 900, 0x90, 50, 100);   // before the first recorded frame (1000): dropped
    audio.feed(480);                         // first block starts at stream frame 1000
    midi.send("Keys", 1500, 0x90, 60, 100);  // ch 1 note on, 500 frames into the take
    midi.send("Keys", 2000, 0x91, 62, 90);   // ch 2 note on, held until Stop
    midi.send("Keys", 3000, 0x80, 60, 0);    // ch 1 note off
    for (int i = 0; i < 9; ++i) audio.feed(480);
    CHECK(session.elapsedSeconds() > 0.09);
    const SessionResult res = session.stop();
    CHECK_FALSE(session.recording());
    CHECK(res.error.empty());
    CHECK_FALSE(res.lostData());
    CHECK(res.seconds > 0.0999);
    CHECK(res.seconds < 0.1001);

    REQUIRE(res.audio.takes.size() == 1);
    CHECK(res.audio.takes[0].frames == 4800);
    CHECK(res.audio.takes[0].path.filename() == "01-Gtr.wav");
    REQUIRE(res.midi.size() == 2);
    CHECK(res.midi[0].path.filename() == "02-All.mid");
    CHECK(res.midi[0].events == 4);  // 3 + the note-off added for the held note
    CHECK(res.midi[1].path.filename() == "03-Ch 2.mid");
    CHECK(res.midi[1].events == 2);
    CHECK_FALSE(std::filesystem::exists(res.folder / "04-Off.wav"));
    CHECK(entries(res.folder) == 3);  // no .tmp files left

    SmfData all;
    REQUIRE(readSmf(res.midi[0].path, all, err));
    const auto a = notes(all);
    REQUIRE(a.size() == 4);
    CHECK(a[0].tick == 20);  // 500 frames at 48 kHz = 10.4 ms = 20 ticks (1920/s)
    CHECK(a[0].bytes[1] == 60);
    CHECK(a[1].tick == 40);
    CHECK(a[2].tick == 80);
    CHECK(a[3].tick == 192);  // added note-off at the end: 4800 frames = 100 ms
    CHECK(a[3].bytes == std::vector<std::uint8_t>{0x81, 62, 0});
    CHECK(endTick(all) == 192);

    SmfData ch2;
    REQUIRE(readSmf(res.midi[1].path, ch2, err));
    const auto c = notes(ch2);
    REQUIRE(c.size() == 2);
    CHECK(c[0].bytes[0] == 0x91);
    CHECK(c[1].tick == 192);
}

TEST_CASE("T-REC-24: a MIDI-only recording uses the stream clock", "[session][T-REC-24]")
{
    test::TempDir dir;
    FakeAudio audio;
    FakeMidi midi;
    midi.openPort("Keys");
    audio.pos = 2000;
    RecordingSession session;
    std::string err;
    REQUIRE(session.start({midiTrack(1, "Solo", "Keys")}, settingsFor(dir), audio, midi, err));
    midi.send("Keys", 2100, 0x90, 64, 100);
    midi.send("Keys", 2340, 0x80, 64, 0);
    audio.pos = 6800;  // the stream moved on by 100 ms
    const SessionResult res = session.stop();
    CHECK(res.error.empty());
    CHECK(res.audio.takes.empty());
    REQUIRE(res.midi.size() == 1);
    SmfData d;
    REQUIRE(readSmf(res.midi[0].path, d, err));
    const auto n = notes(d);
    REQUIRE(n.size() == 2);
    CHECK(n[0].tick == 4);   // 100 frames
    CHECK(n[1].tick == 14);  // 340 frames = 13.6 ticks, rounded
    CHECK(endTick(d) == 192);
    CHECK(res.seconds > 0.0999);
}

TEST_CASE("T-REC-25: a recording that cannot start leaves nothing behind", "[session][T-REC-25]")
{
    test::TempDir dir;
    FakeAudio audio;
    FakeMidi midi;
    midi.openPort("Keys");
    RecordingSession session;
    std::string err;

    CHECK_FALSE(session.start({audioTrack(1, "A", {0}, false)}, settingsFor(dir), audio, midi, err));
    CHECK(err.find("No track is armed") != std::string::npos);

    CHECK_FALSE(session.start({midiTrack(1, "M", "")}, settingsFor(dir), audio, midi, err));
    CHECK(err.find("no MIDI input port") != std::string::npos);

    CHECK_FALSE(session.start({midiTrack(1, "M", "Gone")}, settingsFor(dir), audio, midi, err));
    CHECK(err.find("is not open") != std::string::npos);

    audio.running = false;
    CHECK_FALSE(session.start({audioTrack(1, "A", {0})}, settingsFor(dir), audio, midi, err));
    CHECK(err.find("not running") != std::string::npos);
    audio.running = true;

    audio.inputs = 0;
    CHECK_FALSE(session.start({audioTrack(1, "A", {0})}, settingsFor(dir), audio, midi, err));
    CHECK(err.find("no audio input") != std::string::npos);
    audio.inputs = 2;

    // Fails after the take folder was made (channel 5 does not exist): the folder goes again.
    CHECK_FALSE(session.start({audioTrack(1, "A", {4})}, settingsFor(dir), audio, midi, err));
    CHECK(err.find("does not exist") != std::string::npos);

    CHECK_FALSE(session.recording());
    CHECK(entries(dir.path()) == 0);
}

TEST_CASE("T-REC-26: MIDI takes keep the full timeline when an audio take stops early", "[session][T-REC-26]")
{
    test::TempDir dir;
    FakeAudio audio;
    FakeMidi midi;
    midi.openPort("Keys");
    RecordingSession session;
    std::string err;
    REQUIRE(session.start({audioTrack(1, "A", {0}), audioTrack(2, "B", {1}), midiTrack(3, "M", "Keys")},
                          settingsFor(dir), audio, midi, err));
    audio.feed(480);
    audio.feed(480);
    audio.rec.drainForTest();
    audio.rec.failNextDiskWriteForTest(0);  // take 1's disk "fills up"
    for (int i = 0; i < 8; ++i) audio.feed(480);
    audio.rec.drainForTest();
    midi.send("Keys", 1000 + 4000, 0x90, 70, 100);  // late in the take
    const SessionResult res = session.stop();

    REQUIRE(res.audio.takes.size() == 2);
    CHECK(res.audio.takes[0].frames == 960);   // stopped by the disk error
    CHECK(res.audio.takes[1].frames == 4800);
    CHECK(res.lostData());
    REQUIRE(res.midi.size() == 1);
    SmfData d;
    REQUIRE(readSmf(res.midi[0].path, d, err));
    const auto n = notes(d);
    REQUIRE(n.size() == 2);    // the late note is kept (J4), plus its note-off at the end
    CHECK(n[0].tick == 160);   // 4000 frames
    CHECK(endTick(d) == 192);  // 4800 frames: the timeline, not take 1's length
}

TEST_CASE("T-REC-26: Stop before any audio arrived", "[session][T-REC-26]")
{
    test::TempDir dir;
    FakeAudio audio;
    FakeMidi midi;
    midi.openPort("Keys");
    RecordingSession session;
    std::string err;
    REQUIRE(session.start({audioTrack(1, "A", {0}), midiTrack(2, "M", "Keys")}, settingsFor(dir), audio, midi, err));
    midi.send("Keys", 1200, 0x90, 60, 100);
    const SessionResult res = session.stop();  // no audio block yet
    CHECK(res.error.find("No audio arrived") != std::string::npos);
    CHECK(res.midi.empty());
}

TEST_CASE("T-REC-26: unplaceable MIDI is counted as lost", "[session][T-REC-26]")
{
    test::TempDir dir;
    FakeAudio audio;
    FakeMidi midi;
    midi.openPort("Keys");
    RecordingSession session;
    std::string err;
    REQUIRE(session.start({midiTrack(1, "M", "Keys")}, settingsFor(dir), audio, midi, err));
    midi.send("Keys", -1, 0x90, 60, 100);  // the stand-in has no timeline for it
    midi.send("Keys", 1100, 0x90, 61, 100);
    for (int i = 0; i < 100 && session.midiMessages() < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(session.midiLost() == 1);  // visible while recording
    audio.pos = 2000;
    const SessionResult res = session.stop();
    REQUIRE(res.midi.size() == 1);
    CHECK(res.midi[0].lostMessages == 1);
    CHECK(res.lostData());
}

TEST_CASE("T-REC-26: a failed MIDI rewrite is retried; the stream stopping keeps the last file",
          "[session][T-REC-26]")
{
    test::TempDir dir;
    FakeAudio audio;
    FakeMidi midi;
    midi.openPort("Keys");
    RecordingSession session;
    std::string err;
    REQUIRE(session.start({midiTrack(1, "M", "Keys")}, settingsFor(dir), audio, midi, err));
    const auto midPath = session.folder() / "01-M.mid";
    std::filesystem::create_directory(midPath);  // blocks the rename onto the take file
    midi.send("Keys", 1100, 0x90, 60, 100);
    audio.pos = 1500;

    auto waitFor = [&](auto pred) {
        for (int i = 0; i < 300 && !pred(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return pred();
    };
    CHECK(waitFor([&] { return session.diskError().find("retrying") != std::string::npos; }));
    std::filesystem::remove(midPath);
    CHECK(waitFor([&] { return session.diskError().empty() && std::filesystem::is_regular_file(midPath); }));

    // The stream goes away during the recording: the file of the last
    // periodic write is kept, not replaced by an empty one (J8).
    audio.running = false;
    const SessionResult res = session.stop();
    CHECK(res.error.find("stopped during the recording") != std::string::npos);
    SmfData d;
    REQUIRE(readSmf(midPath, d, err));
    CHECK(notes(d).size() == 2);  // the note and its note-off at the end of that write
}

TEST_CASE("T-REC-28: track and folder names with non-ASCII characters", "[session][T-REC-28]")
{
    test::TempDir dir;
    const std::string name = "Gitaar \xC3\xA9\xC3\xA8 \xE2\x99\xAA";  // "Gitaar éè ♪" in UTF-8
    CHECK(pathToUtf8(pathFromUtf8(name)) == name);
    FakeAudio audio;
    FakeMidi midi;
    RecordingSettings settings = settingsFor(dir);
    settings.folder = pathToUtf8(dir.path() / pathFromUtf8("Opnamen \xC3\xBC"));
    RecordingSession session;
    std::string err;
    REQUIRE(session.start({audioTrack(1, name, {0})}, settings, audio, midi, err));
    audio.feed(480);
    const SessionResult res = session.stop();
    REQUIRE(res.audio.takes.size() == 1);
    CHECK(pathToUtf8(res.audio.takes[0].path.filename()) == "01-" + name + ".wav");
    CHECK(std::filesystem::is_regular_file(res.audio.takes[0].path));
    CHECK(pathToUtf8(res.folder.parent_path().filename()) == "Opnamen \xC3\xBC");
}

TEST_CASE("T-REC-30: a MIDI device unplugged during a take is reported", "[session][T-REC-30]")
{
    test::TempDir dir;
    FakeAudio audio;
    FakeMidi midi;
    midi.openPort("Keys");
    midi.openPort("Pads");
    midi.unplug("Keys");  // before the take: does not count
    RecordingSession session;
    std::string err;
    REQUIRE(session.start({audioTrack(1, "Gtr", {0}), midiTrack(2, "K", "Keys"), midiTrack(3, "P", "Pads")},
                          settingsFor(dir), audio, midi, err));
    audio.feed(480);
    midi.send("Keys", 1200, 0x90, 60, 100);
    midi.unplug("Keys");  // gone for a while, then back
    midi.send("Keys", 1400, 0x80, 60, 0);
    audio.feed(480);
    const SessionResult res = session.stop();
    REQUIRE(res.midi.size() == 2);
    CHECK(res.midi[0].port == "Keys");
    CHECK(res.midi[0].disconnects == 1);
    CHECK(res.midi[0].events == 2);  // what did arrive is kept
    CHECK(res.midi[1].disconnects == 0);
    CHECK(res.lostData());           // --record-test exits with 1
}
