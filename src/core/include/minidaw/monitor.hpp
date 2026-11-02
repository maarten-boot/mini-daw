#pragma once

// Input monitoring (SPEC-MON-01): the inputs of armed audio tracks with
// monitoring on are mixed to the stereo output.
//
// The GUI computes a gain per input channel for the left and right output
// (monitorGains) and hands it to the audio stream; the audio callback mixes
// with mixMonitor. A mono track goes to both sides at full level, a stereo
// track's first channel to the left and its second to the right. A channel
// used by several monitored tracks is heard once. Gain changes are ramped
// over one block, so switching monitoring on or off does not click.

#include "minidaw/track.hpp"

#include <array>
#include <vector>

namespace minidaw {

inline constexpr unsigned int kMonitorMaxChannels = 32;

struct MonitorGains {
    std::array<float, kMonitorMaxChannels> left{};
    std::array<float, kMonitorMaxChannels> right{};
    friend bool operator==(const MonitorGains&, const MonitorGains&) = default;
};

// Gains for the armed audio tracks with `monitor` on; channels at or above
// `inputChannels` are ignored.
MonitorGains monitorGains(const std::vector<Track>& tracks, unsigned int inputChannels);

// Audio thread: adds the monitored input to `out` (interleaved, `outChannels`
// 1 or 2; with one output channel left and right are averaged). The gains
// move linearly from `current` to `target` over the block, then `current`
// becomes `target`. The output is limited to -1..1, and a sample that is not
// a number (a driver glitch) becomes silence. Returns the highest absolute
// output value before limiting; `channelPeaks` (if given, `outChannels`
// values) gets it per output channel, for the output meter, so it can show
// how far the mix went over 0 dBFS (crosscheck #5, K7). Both are 0 when
// nothing was mixed. No locks, no allocation.
float mixMonitor(const float* in, unsigned int frames, unsigned int inChannels, float* out,
                 unsigned int outChannels, MonitorGains& current, const MonitorGains& target,
                 float* channelPeaks = nullptr);

} // namespace minidaw
