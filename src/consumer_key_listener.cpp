#include "consumer_key_listener.h"

#include "log.h"

#include <hidsdi.h>

namespace {

constexpr USHORT kConsumerPage = 0x0C;
constexpr USHORT kConsumerControl = 0x01;
constexpr USAGE kBrightnessIncrement = 0x6F;
constexpr USAGE kBrightnessDecrement = 0x70;
constexpr ULONG kMaxUsagesPerReport = 32;

bool IsConsumerControl(HANDLE device)
{
    RID_DEVICE_INFO info{};
    info.cbSize = sizeof(info);
    UINT size = sizeof(info);

    auto copied = GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, &info, &size);
    return copied != static_cast<UINT>(-1)
        && info.dwType == RIM_TYPEHID
        && info.hid.usUsagePage == kConsumerPage
        && info.hid.usUsage == kConsumerControl;
}

}

bool ConsumerKeyListener::Register(HWND window)
{
    RAWINPUTDEVICE device{};
    device.usUsagePage = kConsumerPage;
    device.usUsage = kConsumerControl;
    // INPUTSINK: receive input although our (hidden) window is never in the foreground.
    // DEVNOTIFY: get WM_INPUT_DEVICE_CHANGE, so data of unplugged devices can be dropped.
    device.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    device.hwndTarget = window;

    return RegisterRawInputDevices(&device, 1, sizeof(device)) == TRUE;
}

void ConsumerKeyListener::HandleInput(const RAWINPUT& input, const BrightnessKeyHandler& onKey)
{
    if (input.header.dwType != RIM_TYPEHID) {
        return;
    }

    auto* device = StateFor(input.header.hDevice);
    if (device == nullptr) {
        return;
    }

    // One message may contain several reports of the same size.
    const auto& hid = input.data.hid;
    for (DWORD index = 0; index < hid.dwCount; ++index) {
        const BYTE* report = hid.bRawData + static_cast<size_t>(index) * hid.dwSizeHid;
        HandleReport(*device, report, hid.dwSizeHid, onKey);
    }
}

void ConsumerKeyListener::HandleDeviceChange(WPARAM change, HANDLE device)
{
    if (change == GIDC_REMOVAL) {
        devices_.erase(device);
    }
}

ConsumerKeyListener::DeviceState* ConsumerKeyListener::StateFor(HANDLE device)
{
    auto existing = devices_.find(device);
    if (existing != devices_.end()) {
        return &existing->second;
    }

    // Input for other listeners (e.g. vendor-specific collections) arrives here too.
    if (!IsConsumerControl(device)) {
        return nullptr;
    }

    // "Preparsed data" describes the layout of the device's reports; HidP_* functions need it.
    UINT size = 0;
    GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, nullptr, &size);
    if (size == 0) {
        return nullptr;
    }

    DeviceState state;
    state.preparsedData.resize(size);
    auto copied = GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, state.preparsedData.data(), &size);
    if (copied == static_cast<UINT>(-1)) {
        return nullptr;
    }

    return &devices_.emplace(device, std::move(state)).first->second;
}

void ConsumerKeyListener::HandleReport(DeviceState& device, const BYTE* report, ULONG reportSize,
                                       const BrightnessKeyHandler& onKey)
{
    // Lists the consumer keys that are held down in this report.
    USAGE usages[kMaxUsagesPerReport];
    ULONG usageCount = kMaxUsagesPerReport;

    auto* preparsedData = reinterpret_cast<PHIDP_PREPARSED_DATA>(device.preparsedData.data());
    auto* reportData = reinterpret_cast<PCHAR>(const_cast<BYTE*>(report));

    auto status = HidP_GetUsages(HidP_Input, kConsumerPage, 0, usages, &usageCount,
                                 preparsedData, reportData, reportSize);
    if (status != HIDP_STATUS_SUCCESS) {
        return;  // e.g. a report with another report id that has no consumer keys
    }

    auto upDown = false;
    auto downDown = false;

    for (ULONG index = 0; index < usageCount; ++index) {
        logging::Write(L"Consumer key 0x%02X is down", usages[index]);

        upDown = upDown || usages[index] == kBrightnessIncrement;
        downDown = downDown || usages[index] == kBrightnessDecrement;
    }

    UpdateKeyState(BrightnessKey::Up, upDown, device.upPressed, onKey);
    UpdateKeyState(BrightnessKey::Down, downDown, device.downPressed, onKey);
}
