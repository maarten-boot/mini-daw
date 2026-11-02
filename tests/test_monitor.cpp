// Input monitoring and level meters (SPEC-MON-01, SPEC-MON-02, SPEC-UI-07).

#include "minidaw/meter.hpp"
#include "minidaw/monitor.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace minidaw;
using Catch::Approx;

namespace {

Track audioTrack(std::vector<unsigned int> channels, bool armed, bool monitor)
{
    Track t;
    t.channels = std::move(channels);
    t.armed = armed;
    t.monitor = monitor;
    return t;
}

} // namespace

TEST_CASE("T-MON-03: monitor gains follow armed tracks with monitoring on", "[monitor][T-MON-03]")
{
    std::vector<Track> tracks = {
        audioTrack({0}, true, true),       // mono: both sides
        audioTrack({2, 3}, true, true),    // stereo: 3 left, 4 right
        audioTrack({4}, false, true),      // not armed: silent
        audioTrack({5}, true, false),      // monitoring off: silent
        audioTrack({0}, true, true),       // same channel again: still heard once
        audioTrack({40}, true, true),      // beyond the device: ignored
    };
    Track midi;
    midi.kind = TrackKind::Midi;
    midi.armed = midi.monitor = true;
    tracks.push_back(midi);

    const MonitorGains g = monitorGains(tracks, 8);
    CHECK(g.left[0] == 1.0f);
    CHECK(g.right[0] == 1.0f);
    CHECK(g.left[2] == 1.0f);
    CHECK(g.right[2] == 0.0f);
    CHECK(g.left[3] == 0.0f);
    CHECK(g.right[3] == 1.0f);
    for (unsigned int ch : {1u, 4u, 5u, 6u, 7u}) {
        CHECK(g.left[ch] == 0.0f);
        CHECK(g.right[ch] == 0.0f);
    }
    // Fewer input channels than a track uses: that channel is ignored.
    const MonitorGains small = monitorGains(tracks, 3);
    CHECK(small.left[2] == 1.0f);
    CHECK(small.right[3] == 0.0f);
    CHECK(monitorGains({}, 8) == MonitorGains{});
}

TEST_CASE("T-MON-04: mixing monitored input to the output", "[monitor][T-MON-04]")
{
    constexpr unsigned int frames = 4;
    constexpr unsigned int inCh = 3;
    // Channel 0 = 0.5, channel 1 = 0.25, channel 2 = -0.1, every frame.
    std::vector<float> in;
    for (unsigned int f = 0; f < frames; ++f) in.insert(in.end(), {0.5f, 0.25f, -0.1f});

    MonitorGains on;
    on.left[1] = on.right[1] = 1.0f;  // mono track on input 2
    on.left[2] = 1.0f;                // input 3 to the left only

    SECTION("steady gains: mono to both sides, others as routed")
    {
        MonitorGains current = on;
        std::vector<float> out(frames * 2, 0.0f);
        const float peak = mixMonitor(in.data(), frames, inCh, out.data(), 2, current, on);
        for (unsigned int f = 0; f < frames; ++f) {
            CHECK(out[f * 2] == Approx(0.15f));
            CHECK(out[f * 2 + 1] == Approx(0.25f));
        }
        CHECK(peak == Approx(0.25f));
        CHECK(current == on);
    }
    SECTION("switching on ramps over the block, without a jump")
    {
        MonitorGains current;  // off
        std::vector<float> out(frames * 2, 0.0f);
        mixMonitor(in.data(), frames, inCh, out.data(), 2, current, on);
        CHECK(out[1] == Approx(0.25f * 0.25f));  // first frame: a quarter of the way
        CHECK(out[3] == Approx(0.25f * 0.5f));
        CHECK(out[7] == Approx(0.25f));          // last frame: full level
        CHECK(current == on);
    }
    SECTION("switching off ramps down to silence")
    {
        MonitorGains current = on;
        std::vector<float> out(frames * 2, 0.0f);
        mixMonitor(in.data(), frames, inCh, out.data(), 2, current, MonitorGains{});
        CHECK(out[1] == Approx(0.25f * 0.75f));
        CHECK(out[7] == Approx(0.0f).margin(1e-7));
        std::vector<float> next(frames * 2, 0.0f);
        CHECK(mixMonitor(in.data(), frames, inCh, next.data(), 2, current, MonitorGains{}) == 0.0f);
        for (float v : next) CHECK(v == 0.0f);
    }
    SECTION("one output channel: left and right averaged")
    {
        MonitorGains current = on;
        std::vector<float> out(frames, 0.0f);
        mixMonitor(in.data(), frames, inCh, out.data(), 1, current, on);
        CHECK(out[0] == Approx(0.5f * (0.15f + 0.25f)));
    }
    SECTION("the output is limited to -1..1; the peak shows the overload")
    {
        MonitorGains all;
        all.left[0] = all.right[0] = 1.0f;
        all.left[1] = all.right[1] = 1.0f;
        std::vector<float> loud;
        for (unsigned int f = 0; f < frames; ++f) loud.insert(loud.end(), {0.9f, 0.8f, 0.0f});
        MonitorGains current = all;
        std::vector<float> out(frames * 2, 0.0f);
        const float peak = mixMonitor(loud.data(), frames, inCh, out.data(), 2, current, all);
        CHECK(peak == Approx(1.7f));
        for (float v : out) CHECK(v == 1.0f);
    }
    SECTION("per-channel peaks before limiting (output meter, K7)")
    {
        MonitorGains left;
        left.left[0] = 1.0f;  // input 1 to the left only
        std::vector<float> loud;
        for (unsigned int f = 0; f < frames; ++f) loud.insert(loud.end(), {1.5f, 0.0f, 0.0f});
        MonitorGains current = left;
        std::vector<float> out(frames * 2, 0.0f);
        float peaks[2] = {-1.0f, -1.0f};
        mixMonitor(loud.data(), frames, inCh, out.data(), 2, current, left, peaks);
        CHECK(peaks[0] == Approx(1.5f));  // +3.5 dBFS shown, although the output is limited
        CHECK(peaks[1] == 0.0f);
        CHECK(out[0] == 1.0f);
    }
    SECTION("a NaN input sample becomes silence, not NaN on the output (K4)")
    {
        std::vector<float> bad = in;
        bad[1 * inCh + 1] = std::nanf("");  // frame 1, input 2
        MonitorGains current = on;
        std::vector<float> out(frames * 2, 0.0f);
        float peaks[2] = {};
        const float peak = mixMonitor(bad.data(), frames, inCh, out.data(), 2, current, on, peaks);
        for (float v : out) CHECK(v == v);
        CHECK(out[2] == 0.0f);  // frame 1: the NaN was summed into both sides, so that sample is silence
        CHECK(out[3] == 0.0f);
        CHECK(out[4] == Approx(0.15f));  // the next frame is normal again
        CHECK(peak == Approx(0.25f));
    }
    SECTION("no input (output-only stream) leaves silence")
    {
        MonitorGains current = on;
        std::vector<float> out(frames * 2, 0.0f);
        CHECK(mixMonitor(nullptr, frames, 0, out.data(), 2, current, on) == 0.0f);
        for (float v : out) CHECK(v == 0.0f);
    }
}

TEST_CASE("T-MON-02: meter scale and dBFS value", "[meter][T-MON-02]")
{
    const float minus6 = std::pow(10.0f, -6.0f / 20.0f);
    CHECK(peakToDb(minus6) == Approx(-6.0).margin(1e-4));
    CHECK(std::isinf(peakToDb(0.0f)));
    CHECK(meterFraction(0.0f) == 0.0f);
    CHECK(meterFraction(1.0f) == 1.0f);
    CHECK(meterFraction(2.0f) == 1.0f);
    CHECK(meterFraction(minus6) == Approx(0.9f));
    CHECK(meterFraction(std::pow(10.0f, -30.0f / 20.0f)) == Approx(0.5f));
    CHECK(meterFraction(1e-4f) == 0.0f);  // -80 dBFS: below the scale

    // A steady -6 dBFS sine: the text shows -6.0 dBFS (T-MON-02 with a real signal is manual).
    LevelMeter m;
    for (int i = 0; i <= 60; ++i) m.update(minus6, i / 60.0);
    CHECK(peakToDb(m.text()) == Approx(-6.0).margin(0.01));
    CHECK(peakToDb(m.bar()) == Approx(-6.0).margin(0.01));
    CHECK_FALSE(m.clipped());
}

TEST_CASE("T-MON-05: meter ballistics, peak hold and clip indicator", "[meter][T-MON-05][T-UI-09]")
{
    LevelMeter m;
    m.update(1.0f / 2, 0.0);  // about -6 dBFS
    CHECK(m.bar() == 0.5f);
    CHECK(m.hold() == 0.5f);

    // Silence: the bar falls 20 dB per second; the hold stays for 1.5 s.
    m.update(0.0f, 0.5);
    CHECK(peakToDb(m.bar()) == Approx(peakToDb(0.5f) - 10.0).margin(0.01));
    CHECK(m.hold() == 0.5f);
    m.update(0.0f, 1.5);
    CHECK(m.hold() == 0.5f);
    m.update(0.0f, 2.0);  // 0.5 s past the hold time: 10 dB lower
    CHECK(peakToDb(m.hold()) == Approx(peakToDb(0.5f) - 10.0).margin(0.01));
    // The text holds the window's peak, then shows the quiet window.
    CHECK(m.text() == 0.0f);
    for (double t = 2.0; t < 6.0; t += 0.1) m.update(0.0f, t);
    CHECK(m.bar() == 0.0f);
    CHECK(m.hold() == 0.0f);

    // A louder peak shows at once.
    m.update(0.25f, 6.1);
    CHECK(m.bar() == 0.25f);
    CHECK(m.text() == 0.25f);

    // Clip: reaching full scale latches until cleared.
    CHECK_FALSE(m.clipped());
    m.update(0.9989f, 6.2);
    CHECK_FALSE(m.clipped());
    m.update(1.0f, 6.3);
    CHECK(m.clipped());
    for (double t = 6.4; t < 20.0; t += 0.5) m.update(0.0f, t);
    CHECK(m.clipped());
    m.clearClip();
    CHECK_FALSE(m.clipped());

    // Bad values count as silence.
    m.update(std::nanf(""), 21.0);
    CHECK(m.bar() >= 0.0f);
    m.reset();
    CHECK(m.bar() == 0.0f);
    CHECK_FALSE(m.clipped());
}
