#pragma once

#include <vector>

// Decides which brightness values the keys step through.
//
// Linear: every step changes the value by `step`.
// Nonlinear (default): the same number of steps, but the levels follow a power curve. Steps are small
// in the dark range, where the eye notices changes most, and larger near the maximum.
// For max 50 and step 5: 0, 1, 2, 5, 8, 13, 18, 25, 32, 41, 50.
class BrightnessScale {
public:
    BrightnessScale() = default;
    BrightnessScale(int maxValue, int step, bool nonlinear);

    // Moves `steps` steps up (positive) or down (negative) from value, within 0..max.
    int Advance(int value, int steps) const;

    int Clamp(int value) const;

    int MaxValue() const
    {
        return maxValue_;
    }

    // Values offered in the tray menu.
    std::vector<int> Presets() const;

private:
    int Next(int value, int direction) const;

    int maxValue_ = 50;
    int step_ = 5;
    bool nonlinear_ = false;
    // Nonlinear levels from 0 to maxValue_, ascending.
    std::vector<int> levels_;
};
