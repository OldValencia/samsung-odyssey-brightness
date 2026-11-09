#pragma once

#include "brightness_scale.h"
#include "brightness_worker.h"
#include "config.h"
#include "osd_window.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Brightness logic on the UI thread: decides what to ask the worker and what to show on screen.
//
// While the user is actively changing the brightness, the new value is shown immediately
// (optimistically) and sent in the background. After a pause the cached value may be outdated,
// e.g. after a change in the monitor's own menu, so the next step reads the real value first.
class BrightnessController {
public:
    explicit BrightnessController(OsdWindow& osd);

    // Applies new settings. Must be called before any other method.
    void Configure(const Config& config, HWND resultWindow);

    // direction: +1 brighter, -1 darker.
    void Step(int direction);
    void Set(int value);
    void Refresh(bool showResultOnOsd);
    // Shows the last known value right away and refreshes it.
    void ShowCurrent();
    // Forget how fresh the cached value is (e.g. after the PC wakes up).
    void MarkStale();

    // Handles WM_APP_BRIGHTNESS_RESULT.
    void OnResult(uint32_t requestId, int value);

    // -1 when unknown.
    int Value() const
    {
        return value_;
    }

    int MaxValue() const
    {
        return scale_.MaxValue();
    }

    std::vector<int> Presets() const
    {
        return scale_.Presets();
    }

    std::wstring StatusText() const;
    std::wstring LastError() const;

private:
    bool HasFreshValue() const;
    void Remember(int value);

    OsdWindow& osd_;
    std::unique_ptr<BrightnessWorker> worker_;
    BrightnessScale scale_;

    int value_ = -1;
    ULONGLONG valueTimestamp_ = 0;
    bool lastRequestFailed_ = false;

    // Results of older requests are ignored, so the display never jumps back.
    uint32_t latestRequestId_ = 0;
    bool showResultOnOsd_ = false;
};
