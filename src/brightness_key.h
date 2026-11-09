#pragma once

#include <functional>

enum class BrightnessKey {
    Up,
    Down,
};

// Called for every press (pressed = true) and release (pressed = false) of a brightness key.
using BrightnessKeyHandler = std::function<void(BrightnessKey key, bool pressed)>;

// Reports a press or release when the key state differs from the previous one.
inline void UpdateKeyState(BrightnessKey key, bool isDown, bool& wasDown, const BrightnessKeyHandler& onKey)
{
    if (isDown == wasDown) {
        return;
    }

    wasDown = isDown;
    onKey(key, isDown);
}
