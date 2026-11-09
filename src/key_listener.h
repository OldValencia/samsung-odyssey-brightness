#pragma once

#include <windows.h>

#include <functional>
#include <unordered_map>
#include <vector>

enum class BrightnessKey {
    Up,
    Down,
};

// Listens for the standard HID brightness keys on any keyboard:
// Consumer page 0x0C, usages 0x6F (Brightness Increment) and 0x70 (Brightness Decrement).
//
// Uses Raw Input, which works without focus and without keyboard hooks. Windows itself does nothing
// with these keys for external monitors, so nothing else reacts to them.
class BrightnessKeyListener {
public:
    using KeyHandler = std::function<void(BrightnessKey key, bool pressed)>;

    bool Register(HWND window);

    // Call from WM_INPUT. onKey is called for every press and release.
    void HandleInput(HRAWINPUT input, const KeyHandler& onKey);

    // Call from WM_INPUT_DEVICE_CHANGE.
    void HandleDeviceChange(WPARAM change, HANDLE device);

private:
    // Parsing data and key state of one HID device (a keyboard, a receiver, ...).
    struct DeviceState {
        std::vector<BYTE> preparsedData;
        bool upPressed = false;
        bool downPressed = false;
    };

    DeviceState* StateFor(HANDLE device);
    void HandleReport(DeviceState& device, const BYTE* report, ULONG reportSize, const KeyHandler& onKey);

    std::vector<BYTE> inputBuffer_;
    std::unordered_map<HANDLE, DeviceState> devices_;
};
