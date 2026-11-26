#include "brightness_worker.h"

#include "app_constants.h"
#include "log.h"

#include <atomic>
#include <utility>

namespace {

// Shared by all workers so a late result never matches a new worker's request.
std::atomic<uint32_t> lastRequestId{0};

}

bool BrightnessWorker::PendingWork::IsEmpty() const
{
    return !refresh && steps == 0 && !absoluteValue;
}

BrightnessWorker::BrightnessWorker(HWND resultWindow)
    : resultWindow_(resultWindow),
      thread_([this](std::stop_token stopToken) { Run(stopToken); })
{
}

uint32_t BrightnessWorker::Configure(const Config& config)
{
    std::scoped_lock lock(mutex_);
    pendingConfig_ = config;
    pending_ = {};
    pending_.refresh = true;
    return NotifyNewWorkLocked();
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
    pending_.absoluteValue = value;
    pending_.steps = 0;
    return NotifyNewWorkLocked();
}

uint32_t BrightnessWorker::RequestStep(int steps, const BrightnessScale& scale)
{
    std::scoped_lock lock(mutex_);

    if (pending_.absoluteValue) {
        pending_.absoluteValue = scale.Advance(*pending_.absoluteValue, steps);
    } else {
        pending_.steps += steps;
        // Opposite steps can cancel out, but the newest request still needs a result.
        pending_.refresh = true;
    }
    return NotifyNewWorkLocked();
}

std::wstring BrightnessWorker::LastError() const
{
    std::scoped_lock lock(mutex_);
    return lastError_;
}

std::wstring BrightnessWorker::ErrorSummary() const
{
    std::scoped_lock lock(mutex_);
    return errorSummary_;
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
        std::optional<Config> config;

        {
            std::unique_lock lock(mutex_);
            auto hasWork = workAvailable_.wait(lock, stopToken, [this] { return !pending_.IsEmpty(); });
            if (!hasWork || stopToken.stop_requested()) {
                return;  // the app is shutting down
            }
            work = std::exchange(pending_, PendingWork{});
            config = std::exchange(pendingConfig_, std::nullopt);
        }

        if (config) {
            client_.emplace(config->monitorIp, config->monitorPort);
            accessToken_ = std::move(config->accessToken);
            scale_ = BrightnessScale(config->maxBrightness, config->step, config->nonlinearSteps);
            smoothTransitions_ = config->smoothTransitions;
            lastKnownValue_.reset();
        }

        auto value = Execute(work, stopToken);

        if (value) {
            logging::Write(L"Request %u: brightness is %d", work.requestId, *value);
        } else {
            {
                std::scoped_lock lock(mutex_);
                if (pendingConfig_ || stopToken.stop_requested()) {
                    continue;
                }
                lastError_ = client_->LastError();
                errorSummary_ = client_->ErrorSummary();
            }
            logging::Write(L"Request %u failed: %ls", work.requestId, client_->LastError().c_str());
        }

        // If a newer request interrupted a transition, this result is outdated and the UI ignores it.
        PostMessageW(resultWindow_, WM_APP_BRIGHTNESS_RESULT, work.requestId, value.value_or(-1));
    }
}
