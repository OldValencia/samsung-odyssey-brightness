// The part of BrightnessWorker that talks to the monitor, including smooth transitions.

#include "brightness_worker.h"

#include "log.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace {

// A transition takes at most this many requests, so even 0 -> 50 stays short.
constexpr int kMaxTransitionFrames = 6;
// Pause between two requests of a transition (on top of the request time itself).
constexpr auto kFrameInterval = std::chrono::milliseconds(15);

}

std::optional<int> BrightnessWorker::Execute(const PendingWork& work, std::stop_token stopToken)
{
    if (work.absoluteValue) {
        return MoveTo(*work.absoluteValue, stopToken);
    }

    // Refresh, or a step from a value that may be outdated: read the real value first.
    auto current = client_.GetBrightness(accessToken_);
    lastKnownValue_ = current;

    if (!current || work.steps == 0) {
        return current;
    }
    return MoveTo(scale_.Advance(*current, work.steps), stopToken);
}

// Changes the brightness to target. With smooth transitions the value gets there in a few small
// requests, like a fade. A newer request interrupts the fade and continues from where it is.
std::optional<int> BrightnessWorker::MoveTo(int target, std::stop_token stopToken)
{
    if (!smoothTransitions_ || !lastKnownValue_ || *lastKnownValue_ == target) {
        return SetAndRemember(target);
    }

    auto current = *lastKnownValue_;
    auto distance = std::abs(target - current);
    auto stepSize = std::max(1, (distance + kMaxTransitionFrames - 1) / kMaxTransitionFrames);

    while (current != target) {
        auto next = current < target ? std::min(current + stepSize, target) : std::max(current - stepSize, target);

        auto reported = SetAndRemember(next);
        if (!reported || *reported != next) {
            return reported;  // failed, or the monitor adjusted the value itself
        }
        current = next;

        if (current != target && WaitForNextFrame(stopToken)) {
            return current;  // a newer request takes over from here
        }
    }
    return current;
}

std::optional<int> BrightnessWorker::SetAndRemember(int value)
{
    auto reported = client_.SetBrightness(accessToken_, value);
    logging::Write(L"Set %d -> %d", value, reported.value_or(-1));

    // After a failure the real value is unknown.
    lastKnownValue_ = reported;
    return reported;
}

// Waits between two requests of a transition. Returns true when the transition should stop
// because a newer request arrived or the app is shutting down.
bool BrightnessWorker::WaitForNextFrame(std::stop_token stopToken)
{
    std::unique_lock lock(mutex_);
    auto hasNewWork = workAvailable_.wait_for(lock, stopToken, kFrameInterval,
                                              [this] { return !pending_.IsEmpty(); });
    return hasNewWork || stopToken.stop_requested();
}
