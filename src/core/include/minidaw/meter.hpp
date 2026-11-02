#pragma once

// Level meter logic (SPEC-MON-02, SPEC-UI-07). GUI thread only; the audio
// thread only delivers the peak per block (AudioStream::takeInputPeak).
//
// - bar:   follows a rising peak at once and falls at 20 dB per second;
// - hold:  the highest peak, kept for 1.5 s, then it falls like the bar;
// - text:  the highest peak of the last 0.5 s window, so the number is readable;
// - clip:  set when a peak reaches full scale, kept until clear() (the user
//          clicks it, or Record starts).
// The bar shows -60 .. 0 dBFS.

namespace minidaw {

inline constexpr double kMeterFloorDb = -60.0;
inline constexpr double kMeterFallDbPerSecond = 20.0;
inline constexpr double kMeterHoldSeconds = 1.5;
inline constexpr double kMeterTextSeconds = 0.5;
// A peak at or above this counts as clipping (-0.01 dBFS: integer devices
// never exceed 1.0, so "reached full scale" is what can be seen).
inline constexpr float kClipLevel = 0.999f;

// Peak value (linear) -> dBFS; -inf for 0.
double peakToDb(float peak);

// Peak value -> bar length 0..1 for the -60 .. 0 dBFS scale.
float meterFraction(float peak);

class LevelMeter {
public:
    // `peak` = highest absolute sample since the previous update, `now` in seconds.
    void update(float peak, double now);

    float bar() const { return bar_; }
    float hold() const { return hold_; }
    float text() const { return text_; }  // value for the dBFS text
    bool clipped() const { return clipped_; }

    void clearClip() { clipped_ = false; }
    void reset() { *this = LevelMeter{}; }

private:
    static float fall(float value, double seconds);

    bool started_ = false;
    double last_ = 0.0;
    float bar_ = 0.0f;
    float hold_ = 0.0f;
    double holdSince_ = 0.0;
    float text_ = 0.0f;
    float windowPeak_ = 0.0f;
    double windowStart_ = 0.0;
    bool clipped_ = false;
};

} // namespace minidaw
