#include "minidaw/meter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace minidaw {

double peakToDb(float peak)
{
    if (!(peak > 0.0f)) return -std::numeric_limits<double>::infinity();
    return 20.0 * std::log10(static_cast<double>(peak));
}

float meterFraction(float peak)
{
    const double db = peakToDb(peak);
    if (db <= kMeterFloorDb) return 0.0f;
    if (db >= 0.0) return 1.0f;
    return static_cast<float>(1.0 - db / kMeterFloorDb);
}

float LevelMeter::fall(float value, double seconds)
{
    if (!(value > 0.0f) || seconds <= 0.0) return value;
    const double factor = std::pow(10.0, -kMeterFallDbPerSecond * seconds / 20.0);
    const float v = static_cast<float>(value * factor);
    return peakToDb(v) <= kMeterFloorDb ? 0.0f : v;
}

void LevelMeter::update(float peak, double now)
{
    if (!(peak >= 0.0f)) peak = 0.0f;  // NaN or negative: treat as silence
    if (!started_) {
        started_ = true;
        last_ = now;
        windowStart_ = now;
        holdSince_ = now;
    }
    const double dt = std::max(0.0, now - last_);
    last_ = now;

    bar_ = std::max(fall(bar_, dt), peak);

    if (peak >= hold_) {
        hold_ = peak;
        holdSince_ = now;
    } else if (now - holdSince_ > kMeterHoldSeconds) {
        // Fall only for the time past the hold period.
        hold_ = std::max(fall(hold_, std::min(dt, now - holdSince_ - kMeterHoldSeconds)), peak);
    }

    windowPeak_ = std::max(windowPeak_, peak);
    if (now - windowStart_ >= kMeterTextSeconds) {
        text_ = windowPeak_;
        windowPeak_ = 0.0f;
        windowStart_ = now;
    } else {
        text_ = std::max(text_, peak);  // a louder peak shows at once
    }

    if (peak >= kClipLevel) clipped_ = true;
}

} // namespace minidaw
