#include "logitech_key_listener.h"

#include "log.h"

#include <algorithm>
#include <string>

namespace {

constexpr DWORD kLogitechVendorId = 0x046D;

// Top-level collections that carry HID++ long reports.
constexpr USHORT kReceiverPage = 0xFF00;
constexpr USHORT kReceiverLongUsage = 0x0002;
constexpr USHORT kBluetoothPage = 0xFF43;
constexpr USHORT kBluetoothLongUsage = 0x0202;

constexpr BYTE kLongReportId = 0x11;
constexpr BYTE kBluetoothDeviceIndex = 0xFF;
constexpr BYTE kFirstReceiverSlot = 1;
constexpr BYTE kLastReceiverSlot = 6;

constexpr BYTE kRootFeatureIndex = 0x00;  // IRoot is always at index 0
constexpr BYTE kGetFeatureFunction = 0x00;
constexpr BYTE kDivertedButtonsEvent = 0x00;
// Marks our own requests, so their answers can be told apart from those of Options+.
constexpr BYTE kSoftwareId = 0x0E;
constexpr USHORT kReprogControlsV4 = 0x1B04;

constexpr USHORT kBrightnessDownCid = 0x00C7;
constexpr USHORT kBrightnessUpCid = 0x00C8;

// Diverted key notifications list up to four CIDs (big-endian) in bytes 4..11.
constexpr size_t kFirstCidOffset = 4;
constexpr size_t kCidCount = 4;

constexpr size_t kMaxPendingEvents = 8;
// A query without an answer (e.g. an empty receiver slot or a sleeping keyboard) is repeated
// on the next notification from that device, but not more often than this.
constexpr ULONGLONG kQueryRetryMs = 2000;

std::wstring DevicePath(HANDLE device)
{
    UINT size = 0;
    GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &size);

    std::wstring path(size, L'\0');
    GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, path.data(), &size);
    return path.c_str();
}

std::vector<HANDLE> ConnectedDevices()
{
    UINT count = 0;
    GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST));

    std::vector<RAWINPUTDEVICELIST> list(count);
    count = GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
    if (count == static_cast<UINT>(-1)) {
        return {};
    }

    std::vector<HANDLE> devices;
    for (UINT index = 0; index < count; ++index) {
        devices.push_back(list[index].hDevice);
    }
    return devices;
}

bool IsOurFeatureAnswer(const std::array<BYTE, 20>& report)
{
    return report[2] == kRootFeatureIndex && report[3] == (kGetFeatureFunction << 4 | kSoftwareId);
}

// Notifications have software id 0; answers to requests of other programs do not.
bool IsDivertedButtonsCandidate(const std::array<BYTE, 20>& report)
{
    return report[3] == (kDivertedButtonsEvent << 4);
}

}

bool LogitechKeyListener::Register(HWND window)
{
    RAWINPUTDEVICE devices[] = {
        {kReceiverPage, kReceiverLongUsage, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window},
        {kBluetoothPage, kBluetoothLongUsage, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window},
    };
    if (!RegisterRawInputDevices(devices, ARRAYSIZE(devices), sizeof(RAWINPUTDEVICE))) {
        return false;
    }

    // Ask the connected keyboards right away, so the first key press is recognized immediately.
    for (auto device : ConnectedDevices()) {
        ChannelFor(device);
    }
    return true;
}

void LogitechKeyListener::HandleInput(const RAWINPUT& input, const BrightnessKeyHandler& onKey)
{
    if (input.header.dwType != RIM_TYPEHID) {
        return;
    }

    auto* channel = ChannelFor(input.header.hDevice);
    const auto& hid = input.data.hid;
    if (channel == nullptr || hid.dwSizeHid < std::tuple_size_v<Report>) {
        return;
    }

    for (DWORD index = 0; index < hid.dwCount; ++index) {
        Report report;
        std::copy_n(hid.bRawData + static_cast<size_t>(index) * hid.dwSizeHid, report.size(), report.begin());
        HandleReport(*channel, report, onKey);
    }
}

void LogitechKeyListener::HandleDeviceChange(WPARAM change, HANDLE device)
{
    if (change == GIDC_REMOVAL) {
        channels_.erase(device);
    } else if (change == GIDC_ARRIVAL) {
        ChannelFor(device);
    }
}

// Returns the channel of a Logitech HID++ collection (created and queried on first use),
// or nullptr for any other device.
LogitechKeyListener::Channel* LogitechKeyListener::ChannelFor(HANDLE device)
{
    auto existing = channels_.find(device);
    if (existing != channels_.end()) {
        return existing->second.get();
    }

    RID_DEVICE_INFO info{};
    info.cbSize = sizeof(info);
    UINT size = sizeof(info);
    GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, &info, &size);

    const auto& hid = info.hid;
    auto isReceiver = hid.usUsagePage == kReceiverPage && hid.usUsage == kReceiverLongUsage;
    auto isBluetooth = hid.usUsagePage == kBluetoothPage && hid.usUsage == kBluetoothLongUsage;
    auto isLogitech = info.dwType == RIM_TYPEHID && hid.dwVendorId == kLogitechVendorId;

    if (!isLogitech || !(isReceiver || isBluetooth)) {
        channels_.emplace(device, nullptr);
        return nullptr;
    }

    auto channel = std::make_unique<Channel>();
    channel->isBluetooth = isBluetooth;

    auto path = DevicePath(device);
    auto file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, 0, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        channel->file.reset(file);
    }
    logging::Write(L"Logitech HID++ channel %ls (pid %04lX): %ls", isBluetooth ? L"Bluetooth" : L"receiver",
                   hid.dwProductId, file != INVALID_HANDLE_VALUE ? L"ready" : L"cannot be opened");

    auto* result = channel.get();
    channels_.emplace(device, std::move(channel));
    QueryAllPeripherals(*result);
    return result;
}

void LogitechKeyListener::QueryAllPeripherals(Channel& channel)
{
    if (channel.isBluetooth) {
        SendFeatureQuery(channel, kBluetoothDeviceIndex);
        return;
    }

    for (BYTE slot = kFirstReceiverSlot; slot <= kLastReceiverSlot; ++slot) {
        SendFeatureQuery(channel, slot);
    }
}

// Asks the device at deviceIndex where its REPROG_CONTROLS_V4 feature is.
void LogitechKeyListener::SendFeatureQuery(Channel& channel, BYTE deviceIndex)
{
    auto& peripheral = channel.peripherals[deviceIndex];
    if (!channel.file || peripheral.reprogFeatureIndex) {
        return;
    }

    Report request{};
    request[0] = kLongReportId;
    request[1] = deviceIndex;
    request[2] = kRootFeatureIndex;
    request[3] = kGetFeatureFunction << 4 | kSoftwareId;
    request[4] = HIBYTE(kReprogControlsV4);
    request[5] = LOBYTE(kReprogControlsV4);

    DWORD written = 0;
    WriteFile(channel.file.get(), request.data(), static_cast<DWORD>(request.size()), &written, nullptr);
    peripheral.lastQueryTick = GetTickCount64();
}

void LogitechKeyListener::HandleReport(Channel& channel, const Report& report, const BrightnessKeyHandler& onKey)
{
    if (report[0] != kLongReportId) {
        return;
    }

    auto deviceIndex = report[1];
    auto& peripheral = channel.peripherals[deviceIndex];

    if (IsOurFeatureAnswer(report)) {
        OnFeatureAnswer(peripheral, deviceIndex, report[4], onKey);
        return;
    }

    if (!IsDivertedButtonsCandidate(report)) {
        return;
    }

    if (peripheral.reprogFeatureIndex) {
        if (report[2] == *peripheral.reprogFeatureIndex) {
            HandleDivertedKeys(peripheral, report, onKey);
        }
        return;
    }

    // The feature index is not known yet: keep the event and ask the device.
    if (peripheral.pendingEvents.size() < kMaxPendingEvents) {
        peripheral.pendingEvents.push_back(report);
    }
    if (GetTickCount64() - peripheral.lastQueryTick > kQueryRetryMs) {
        SendFeatureQuery(channel, deviceIndex);
    }
}

void LogitechKeyListener::OnFeatureAnswer(Peripheral& peripheral, BYTE deviceIndex, BYTE featureIndex,
                                          const BrightnessKeyHandler& onKey)
{
    peripheral.reprogFeatureIndex = featureIndex;
    logging::Write(L"Logitech device %u: REPROG_CONTROLS_V4 is %ls (index 0x%02X)", deviceIndex,
                   featureIndex != 0 ? L"available" : L"missing", featureIndex);

    auto pendingEvents = std::move(peripheral.pendingEvents);
    peripheral.pendingEvents.clear();

    for (const auto& report : pendingEvents) {
        if (featureIndex != 0 && report[2] == featureIndex) {
            HandleDivertedKeys(peripheral, report, onKey);
        }
    }
}

void LogitechKeyListener::HandleDivertedKeys(Peripheral& peripheral, const Report& report,
                                             const BrightnessKeyHandler& onKey)
{
    auto upDown = false;
    auto downDown = false;

    for (size_t index = 0; index < kCidCount; ++index) {
        auto offset = kFirstCidOffset + index * 2;
        auto cid = static_cast<USHORT>(report[offset] << 8 | report[offset + 1]);

        if (cid != 0) {
            logging::Write(L"Logitech key CID 0x%04X is down", cid);
        }

        upDown = upDown || cid == kBrightnessUpCid;
        downDown = downDown || cid == kBrightnessDownCid;
    }

    UpdateKeyState(BrightnessKey::Up, upDown, peripheral.upPressed, onKey);
    UpdateKeyState(BrightnessKey::Down, downDown, peripheral.downPressed, onKey);
}
