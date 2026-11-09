#include "brightness_controller.h"

#include <algorithm>
#include <format>

namespace {

// How long a known brightness value is trusted before it is read from the monitor again.
constexpr ULONGLONG kFreshValueMs = 10'000;

}

BrightnessController::BrightnessController(OsdWindow& osd)
    : osd_(osd)
{
}

void BrightnessController::Configure(const Config& config, HWND resultWindow)
{
    // Stop the old worker before starting the new one.
    worker_.reset();
    worker_ = std::make_unique<BrightnessWorker>(config, resultWindow);

    maxValue_ = config.maxBrightness;
    step_ = config.step;
    value_ = -1;
    valueTimestamp_ = 0;
    lastRequestFailed_ = false;
    latestRequestId_ = 0;
    showResultOnOsd_ = false;
}

void BrightnessController::Step(int direction)
{
    auto delta = direction * step_;

    if (HasFreshValue()) {
        Set(value_ + delta);
        return;
    }

    // The cached value may be outdated, so the worker reads the real one before changing it.
    showResultOnOsd_ = true;
    latestRequestId_ = worker_->RequestStep(delta);
}

void BrightnessController::Set(int value)
{
    auto target = std::clamp(value, 0, maxValue_);

    if (target == value_ && HasFreshValue()) {
        osd_.ShowLevel(value_, maxValue_);  // e.g. already at the maximum: nothing to send
        return;
    }

    Remember(target);
    osd_.ShowLevel(target, maxValue_);

    showResultOnOsd_ = true;
    latestRequestId_ = worker_->RequestSet(target);
}

void BrightnessController::Refresh(bool showResultOnOsd)
{
    showResultOnOsd_ = showResultOnOsd_ || showResultOnOsd;
    latestRequestId_ = worker_->RequestRefresh();
}

void BrightnessController::ShowCurrent()
{
    if (value_ >= 0) {
        osd_.ShowLevel(value_, maxValue_);
    }
    Refresh(true);
}

void BrightnessController::MarkStale()
{
    valueTimestamp_ = 0;
}

void BrightnessController::OnResult(uint32_t requestId, int value)
{
    if (requestId != latestRequestId_) {
        return;  // a newer request is already on its way
    }

    lastRequestFailed_ = value < 0;
    if (lastRequestFailed_) {
        value_ = -1;
    } else {
        Remember(value);
    }

    if (!showResultOnOsd_) {
        return;
    }
    showResultOnOsd_ = false;

    if (lastRequestFailed_) {
        osd_.ShowMessage(L"Monitor is not reachable");
    } else {
        osd_.ShowLevel(value_, maxValue_);
    }
}

std::wstring BrightnessController::StatusText() const
{
    if (value_ >= 0) {
        return std::format(L"Brightness: {} / {}", value_, maxValue_);
    }
    return lastRequestFailed_ ? L"Brightness: monitor not reachable" : L"Brightness: checking...";
}

std::wstring BrightnessController::LastError() const
{
    return worker_ ? worker_->LastError() : std::wstring();
}

bool BrightnessController::HasFreshValue() const
{
    return value_ >= 0 && GetTickCount64() - valueTimestamp_ < kFreshValueMs;
}

void BrightnessController::Remember(int value)
{
    value_ = value;
    valueTimestamp_ = GetTickCount64();
}
