#include "brightness_scale.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace {

// The linear tray menu shows about this many presets.
constexpr int kMaxLinearPresets = 10;

// level = round(max * (index / stepCount)^2). A quadratic curve is a gentle approximation of how the
// eye perceives brightness. Integer math keeps the levels exact (no floating-point rounding surprises).
int CurveLevel(int maxValue, int index, int stepCount)
{
    auto numerator = 2 * maxValue * index * index;
    auto denominator = 2 * stepCount * stepCount;
    return (numerator + stepCount * stepCount) / denominator;  // rounds half up
}

std::vector<int> BuildLevels(int maxValue, int step)
{
    auto stepCount = std::max(1, (maxValue + step / 2) / step);
    std::vector<int> levels{0};

    for (int index = 1; index <= stepCount; ++index) {
        auto level = CurveLevel(maxValue, index, stepCount);

        // Very small ranges can produce the same level twice.
        if (level > levels.back()) {
            levels.push_back(level);
        }
    }
    return levels;
}

}

BrightnessScale::BrightnessScale(int maxValue, int step, bool nonlinear)
    : maxValue_(std::max(maxValue, 1)),
      step_(std::clamp(step, 1, std::max(maxValue, 1))),
      nonlinear_(nonlinear)
{
    if (nonlinear_) {
        levels_ = BuildLevels(maxValue_, step_);
    }
}

int BrightnessScale::Advance(int value, int steps) const
{
    auto direction = steps > 0 ? 1 : -1;
    auto result = Clamp(value);

    for (int index = 0; index < std::abs(steps); ++index) {
        auto next = Next(result, direction);
        if (next == result) {
            break;  // reached 0 or the maximum
        }
        result = next;
    }
    return result;
}

int BrightnessScale::Clamp(int value) const
{
    return std::clamp(value, 0, maxValue_);
}

std::vector<int> BrightnessScale::Presets() const
{
    if (nonlinear_) {
        return levels_;
    }

    std::vector<int> presets;
    auto presetStep = std::max(step_, (maxValue_ + kMaxLinearPresets - 1) / kMaxLinearPresets);
    for (int value = 0; value < maxValue_; value += presetStep) {
        presets.push_back(value);
    }
    presets.push_back(maxValue_);
    return presets;
}

// The next value in the given direction. A value between two levels (e.g. set from the tray menu
// or the monitor's own menu) moves to the nearest level in that direction.
int BrightnessScale::Next(int value, int direction) const
{
    if (!nonlinear_) {
        return Clamp(value + direction * step_);
    }

    if (direction > 0) {
        auto above = std::upper_bound(levels_.begin(), levels_.end(), value);
        return above != levels_.end() ? *above : maxValue_;
    }

    auto notBelow = std::lower_bound(levels_.begin(), levels_.end(), value);
    return notBelow != levels_.begin() ? *std::prev(notBelow) : 0;
}
