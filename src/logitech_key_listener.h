#pragma once

#include "brightness_key.h"

#include <windows.h>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

// Listens for the brightness keys of Logitech keyboards while Logi Options+ is running.
//
// Options+ "diverts" the special keys of its keyboards: instead of standard HID codes the keyboard
// sends HID++ 2.0 notifications of the REPROG_CONTROLS_V4 feature (0x1B04) that list the control
// ids (CIDs) of the keys held down. Brightness Down is CID 0x00C7, Brightness Up is CID 0x00C8.
//
// The feature has a different index on every keyboard model, so the listener asks each keyboard
// for it once (HID++ IRoot.getFeature). Only the answer is needed; nothing on the keyboard changes.
class LogitechKeyListener {
public:
    bool Register(HWND window);

    // Call from WM_INPUT.
    void HandleInput(const RAWINPUT& input, const BrightnessKeyHandler& onKey);

    // Call from WM_INPUT_DEVICE_CHANGE.
    void HandleDeviceChange(WPARAM change, HANDLE device);

private:
    // HID++ long report: [0] report id, [1] device index, [2] feature index,
    // [3] function << 4 | software id, [4..19] parameters.
    using Report = std::array<BYTE, 20>;

    // A keyboard (or mouse) in one slot of a receiver, or a keyboard connected over Bluetooth.
    struct Peripheral {
        // Index of REPROG_CONTROLS_V4 on this device; 0 when the device does not have it.
        std::optional<BYTE> reprogFeatureIndex;
        ULONGLONG lastQueryTick = 0;
        // Notifications that arrived before the feature index was known.
        std::vector<Report> pendingEvents;
        bool upPressed = false;
        bool downPressed = false;
    };

    struct HandleCloser {
        void operator()(HANDLE handle) const
        {
            CloseHandle(handle);
        }
    };

    // One HID++ channel: a USB receiver (Bolt, Unifying) or a Bluetooth keyboard.
    struct Channel {
        bool isBluetooth = false;
        // Used to send requests; may be empty if the device cannot be opened.
        std::unique_ptr<void, HandleCloser> file;
        std::map<BYTE, Peripheral> peripherals;  // by device index
    };

    Channel* ChannelFor(HANDLE device);
    void QueryAllPeripherals(Channel& channel);
    void SendFeatureQuery(Channel& channel, BYTE deviceIndex);
    void HandleReport(Channel& channel, const Report& report, const BrightnessKeyHandler& onKey);
    void OnFeatureAnswer(Peripheral& peripheral, BYTE deviceIndex, BYTE featureIndex,
                         const BrightnessKeyHandler& onKey);
    void HandleDivertedKeys(Peripheral& peripheral, const Report& report, const BrightnessKeyHandler& onKey);

    // nullptr values mark devices that are not Logitech HID++ channels.
    std::unordered_map<HANDLE, std::unique_ptr<Channel>> channels_;
};
