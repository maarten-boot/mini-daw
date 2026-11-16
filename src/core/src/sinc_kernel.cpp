#include "minidaw/sinc_kernel.hpp"

#include <algorithm>
#include <cmath>

namespace minidaw {

namespace {

constexpr int kZeroCrossings = 24;
constexpr int kStepsPerCrossing = 512;
constexpr double kBeta = 9.5;
constexpr double kPi = 3.14159265358979323846;

// Modified Bessel function of the first kind, order 0 (series).
double besselI0(double x)
{
    double sum = 1.0;
    double term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 64; ++k) {
        term *= q / (static_cast<double>(k) * k);
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

} // namespace

SincKernel::SincKernel(double ratio)
{
    cutoff_ = std::min(1.0, ratio) * 0.92;
    // The kernel is stretched by 1/cutoff in input frames.
    halfWidth_ = static_cast<int>(std::ceil(kZeroCrossings / cutoff_));
    const int steps = kZeroCrossings * kStepsPerCrossing;  // over one side, in units of zero crossings
    table_.assign(static_cast<std::size_t>(steps) + 2, 0.0f);
    const double i0Beta = besselI0(kBeta);
    for (int i = 0; i <= steps; ++i) {
        const double x = static_cast<double>(i) / kStepsPerCrossing;  // zero crossings from the centre
        const double sinc = i == 0 ? 1.0 : std::sin(kPi * x) / (kPi * x);
        const double r = x / kZeroCrossings;
        const double window = besselI0(kBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0Beta;
        table_[static_cast<std::size_t>(i)] = static_cast<float>(sinc * window * cutoff_);
    }
    scale_ = cutoff_ * kStepsPerCrossing;
}

float SincKernel::weight(double distance) const
{
    const double pos = std::abs(distance) * scale_;
    const auto i = static_cast<std::size_t>(pos);
    if (i + 1 >= table_.size()) return 0.0f;
    const double frac = pos - static_cast<double>(i);
    return static_cast<float>(table_[i] + (table_[i + 1] - table_[i]) * frac);
}

} // namespace minidaw
