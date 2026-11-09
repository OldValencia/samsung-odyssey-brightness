#pragma once

#include "config.h"
#include "samsung_client.h"

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

// Runs all network calls on a background thread, so the tray menu and the keys never freeze.
//
// Requests that arrive while the monitor is still busy are merged and only the newest one is sent.
// Holding a brightness key therefore never builds up a queue of outdated requests.
//
// When a request finishes, WM_APP_BRIGHTNESS_RESULT is posted to the result window
// with wParam = request id and lParam = brightness (or -1 on failure).
class BrightnessWorker {
public:
    BrightnessWorker(const Config& config, HWND resultWindow);

    // Each method returns the id of the request; it comes back with the result.
    uint32_t RequestRefresh();
    uint32_t RequestSet(int value);
    // Reads the current brightness from the monitor first and then changes it by delta.
    uint32_t RequestStep(int delta);

    std::wstring LastError() const;

private:
    // Everything requested since the worker last picked up work.
    struct PendingWork {
        bool refresh = false;
        int stepDelta = 0;
        std::optional<int> absoluteValue;
        uint32_t requestId = 0;

        bool IsEmpty() const;
    };

    uint32_t NotifyNewWorkLocked();
    void Run(std::stop_token stopToken);
    std::optional<int> Execute(const PendingWork& work);
    int Clamp(int value) const;

    SamsungClient client_;
    std::wstring accessToken_;
    int maxBrightness_;
    HWND resultWindow_;

    mutable std::mutex mutex_;
    std::condition_variable_any workAvailable_;
    PendingWork pending_;
    std::wstring lastError_;

    // Declared last: the thread starts after all other members are ready and stops before they are destroyed.
    std::jthread thread_;
};
