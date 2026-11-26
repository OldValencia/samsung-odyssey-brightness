#include "brightness_controller.h"

#include <format>
#include <utility>

namespace {

// How long a known brightness value is trusted before it is read from the monitor again.
constexpr ULONGLONG kFreshValueMs = 10'000;

}

BrightnessController::BrightnessController(OsdWindow& osd)
    : osd_(osd)
{
}

void BrightnessController::Configure(const Config& config, HWND resultWindow, bool showConnectionError)
{
    if (!worker_) {
        worker_ = std::make_unique<BrightnessWorker>(resultWindow);
    }

    scale_ = BrightnessScale(config.maxBrightness, config.step, config.nonlinearSteps);
    value_ = -1;
    pendingStepLevel_.reset();
    valueTimestamp_ = 0;
    lastRequestFailed_ = false;
    latestRequestId_ = worker_->Configure(config);
    resultDisplay_ = showConnectionError && !config.monitorIp.empty() && !config.accessToken.empty()
        ? ResultDisplay::ErrorOnly : ResultDisplay::Silent;
}

void BrightnessController::Step(int direction)
{
    if (HasFreshValue()) {
        Set(scale_.Advance(value_, direction));
        return;
    }

    // The cached value may be outdated, so the worker reads the real one before changing it.
    resultDisplay_ = ResultDisplay::Always;
    latestRequestId_ = worker_->RequestStep(direction, scale_);
    if (value_ >= 0) {
        pendingStepLevel_ = scale_.Advance(pendingStepLevel_.value_or(value_), direction);
        osd_.ShowLevel(*pendingStepLevel_, MaxValue());
    } else {
        osd_.ShowMessage(L"Reading brightness...");
    }
}

void BrightnessController::Set(int value)
{
    auto target = scale_.Clamp(value);

    if (target == value_ && HasFreshValue()) {
        osd_.ShowLevel(value_, MaxValue());  // e.g. already at the maximum: nothing to send
        return;
    }

    Remember(target);
    osd_.ShowLevel(target, MaxValue());

    resultDisplay_ = ResultDisplay::Always;
    latestRequestId_ = worker_->RequestSet(target);
}

void BrightnessController::Refresh(bool showResultOnOsd)
{
    if (showResultOnOsd) {
        resultDisplay_ = ResultDisplay::Always;
    }
    latestRequestId_ = worker_->RequestRefresh();
}

void BrightnessController::ShowCurrent()
{
    if (value_ >= 0) {
        osd_.ShowLevel(value_, MaxValue());
    }
    Refresh(true);
}

void BrightnessController::Preview()
{
    if (value_ >= 0) {
        osd_.ShowLevel(pendingStepLevel_.value_or(value_), MaxValue());
    } else if (resultDisplay_ == ResultDisplay::Always) {
        osd_.ShowMessage(L"Reading brightness...");
    } else {
        osd_.ShowMessage(L"OSD preview");
    }
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
    pendingStepLevel_.reset();
    if (lastRequestFailed_) {
        value_ = -1;
    } else {
        Remember(value);
    }

    const auto display = std::exchange(resultDisplay_, ResultDisplay::Silent);
    if (lastRequestFailed_ && display != ResultDisplay::Silent) {
        osd_.ShowMessage(worker_->ErrorSummary());
    } else if (!lastRequestFailed_ && display == ResultDisplay::Always) {
        osd_.ShowLevel(value_, MaxValue());
    }
}

std::wstring BrightnessController::StatusText() const
{
    if (value_ >= 0) {
        return std::format(L"Brightness: {} / {}", value_, MaxValue());
    }
    return lastRequestFailed_ ? L"Brightness unavailable" : L"Brightness: checking...";
}

std::wstring BrightnessController::LastError() const
{
    return lastRequestFailed_ ? worker_->LastError() : std::wstring();
}

bool BrightnessController::HasFreshValue() const
{
    return value_ >= 0 && GetTickCount64() - valueTimestamp_ < kFreshValueMs;
}

void BrightnessController::Remember(int value)
{
    value_ = value;
    pendingStepLevel_.reset();
    valueTimestamp_ = GetTickCount64();
}
