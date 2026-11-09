#include "brightness_worker.h"

#include "app_constants.h"
#include "log.h"

#include <atomic>
#include <utility>

namespace {

// Shared by all workers, so a late result of a replaced worker never matches a new request.
std::atomic<uint32_t> lastRequestId{0};

}

bool BrightnessWorker::PendingWork::IsEmpty() const
{
    return !refresh && steps == 0 && !absoluteValue;
}

BrightnessWorker::BrightnessWorker(const Config& config, HWND resultWindow)
    : client_(config.monitorIp, config.monitorPort),
      accessToken_(config.accessToken),
      scale_(config.maxBrightness, config.step, config.nonlinearSteps),
      smoothTransitions_(config.smoothTransitions),
      resultWindow_(resultWindow),
      thread_([this](std::stop_token stopToken) { Run(stopToken); })
{
}

uint32_t BrightnessWorker::RequestRefresh()
{
    std::scoped_lock lock(mutex_);
    pending_.refresh = true;
    return NotifyNewWorkLocked();
}

uint32_t BrightnessWorker::RequestSet(int value)
{
    std::scoped_lock lock(mutex_);
    pending_.absoluteValue = scale_.Clamp(value);
    pending_.steps = 0;
    return NotifyNewWorkLocked();
}

uint32_t BrightnessWorker::RequestStep(int steps)
{
    std::scoped_lock lock(mutex_);

    if (pending_.absoluteValue) {
        pending_.absoluteValue = scale_.Advance(*pending_.absoluteValue, steps);
    } else {
        pending_.steps += steps;
    }
    return NotifyNewWorkLocked();
}

std::wstring BrightnessWorker::LastError() const
{
    std::scoped_lock lock(mutex_);
    return lastError_;
}

uint32_t BrightnessWorker::NotifyNewWorkLocked()
{
    pending_.requestId = ++lastRequestId;
    workAvailable_.notify_one();
    return pending_.requestId;
}

void BrightnessWorker::Run(std::stop_token stopToken)
{
    while (true) {
        PendingWork work;

        {
            std::unique_lock lock(mutex_);
            auto hasWork = workAvailable_.wait(lock, stopToken, [this] { return !pending_.IsEmpty(); });
            if (!hasWork) {
                return;  // the app is shutting down
            }
            work = std::exchange(pending_, PendingWork{});
        }

        auto value = Execute(work, stopToken);

        if (value) {
            logging::Write(L"Request %u: brightness is %d", work.requestId, *value);
        } else {
            std::scoped_lock lock(mutex_);
            lastError_ = client_.LastError();
            logging::Write(L"Request %u failed: %ls", work.requestId, lastError_.c_str());
        }

        // If a newer request interrupted a transition, this result is outdated and the UI ignores it.
        PostMessageW(resultWindow_, WM_APP_BRIGHTNESS_RESULT, work.requestId, value.value_or(-1));
    }
}
