#pragma once

// Band-limited interpolation for audio at another sample rate (P28: imported
// clips are written at the stream's rate, since playback does not resample).
// A windowed-sinc kernel (Kaiser window, beta 9.5, 24 zero crossings each
// side), read from a table with linear interpolation. The cutoff follows the
// ratio, so converting down does not alias: 0.92 x the lower Nyquist frequency
// (20.3 kHz for 44.1 kHz; a 23 kHz tone at 48 kHz comes out about -90 dB).
//
// A sample at fractional input position s is
//     sum over k = floor(s) - halfWidth() + 1 .. floor(s) + halfWidth() of
//         input[k] * weight(s - k)

#include <vector>

namespace minidaw {

class SincKernel {
public:
    // ratio = output rate / input rate.
    explicit SincKernel(double ratio);
    // Input frames used on each side of the position.
    int halfWidth() const { return halfWidth_; }
    // Weight of an input frame `distance` frames from the position (0 outside the window).
    float weight(double distance) const;

private:
    double cutoff_ = 1.0;    // of the input's Nyquist frequency
    int halfWidth_ = 16;
    double scale_ = 0.0;     // table steps per input frame
    std::vector<float> table_;
};

} // namespace minidaw
