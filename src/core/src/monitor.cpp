#include "minidaw/monitor.hpp"

#include <algorithm>
#include <cmath>

namespace minidaw {

MonitorGains monitorGains(const std::vector<Track>& tracks, unsigned int inputChannels)
{
    MonitorGains g;
    const unsigned int limit = std::min(inputChannels, kMonitorMaxChannels);
    for (const Track& t : tracks) {
        if (t.kind != TrackKind::Audio || !t.armed || !t.monitor || t.channels.empty()) continue;
        const unsigned int a = t.channels[0];
        if (t.channels.size() == 1) {
            if (a < limit) g.left[a] = g.right[a] = 1.0f;
        } else {
            const unsigned int b = t.channels[1];
            if (a < limit) g.left[a] = 1.0f;
            if (b < limit) g.right[b] = 1.0f;
        }
    }
    return g;
}

void mixMonitor(const float* in, unsigned int frames, unsigned int inChannels, float* out,
                unsigned int outChannels, MonitorGains& current, const MonitorGains& target)
{
    if (out == nullptr || outChannels == 0 || frames == 0) {
        current = target;
        return;
    }
    const unsigned int channels = std::min(inChannels, kMonitorMaxChannels);
    const float step = 1.0f / static_cast<float>(frames);
    if (in != nullptr) {
        for (unsigned int ch = 0; ch < channels; ++ch) {
            const float l0 = current.left[ch], l1 = target.left[ch];
            const float r0 = current.right[ch], r1 = target.right[ch];
            if (l0 == 0.0f && l1 == 0.0f && r0 == 0.0f && r1 == 0.0f) continue;
            for (unsigned int f = 0; f < frames; ++f) {
                const float t = static_cast<float>(f + 1) * step;  // reaches the target on the last frame
                const float s = in[f * inChannels + ch];
                const float l = s * (l0 + (l1 - l0) * t);
                const float r = s * (r0 + (r1 - r0) * t);
                if (outChannels == 1) {
                    out[f] += 0.5f * (l + r);
                } else {
                    out[f * outChannels] += l;
                    out[f * outChannels + 1] += r;
                }
            }
        }
    }
    current = target;
}

float limitOutput(float* out, unsigned int frames, unsigned int outChannels, float* channelPeaks)
{
    if (channelPeaks != nullptr) {
        for (unsigned int c = 0; c < outChannels; ++c) channelPeaks[c] = 0.0f;
    }
    if (out == nullptr) return 0.0f;
    float peak = 0.0f;
    for (unsigned int f = 0; f < frames; ++f) {
        for (unsigned int c = 0; c < outChannels; ++c) {
            float& v = out[f * outChannels + c];
            if (!(v == v)) v = 0.0f;  // NaN: std::clamp would pass it on (K4)
            const float a = std::fabs(v);
            if (a > peak) peak = a;
            if (channelPeaks != nullptr && a > channelPeaks[c]) channelPeaks[c] = a;
            v = std::clamp(v, -1.0f, 1.0f);
        }
    }
    return peak;
}

} // namespace minidaw
