#pragma once

#include "brightness_key.h"

#include <windows.h>

#include <unordered_map>
#include <vector>

// Listens for the standard HID brightness keys on any keyboard:
// Consumer page 0x0C, usages 0x6F (Brightness Increment) and 0x70 (Brightness Decrement).
//
// Uses Raw Input, which works without focus and without keyboard hooks. Windows itself does nothing
// with these keys for external monitors, so nothing else reacts to them.
class ConsumerKeyListener {
public:
    bool Register(HWND window);

    // Call from WM_INPUT.
    void HandleInput(const RAWINPUT& input, const BrightnessKeyHandler& onKey);

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
    void HandleReport(DeviceState& device, const BYTE* report, ULONG reportSize, const BrightnessKeyHandler& onKey);

    std::unordered_map<HANDLE, DeviceState> devices_;
};
