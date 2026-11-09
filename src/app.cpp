#include "app.h"

#include "app_constants.h"
#include "log.h"
#include "raw_input.h"
#include "resource.h"
#include "win_util.h"

namespace {

// Holding a brightness key repeats it like a normal key.
constexpr UINT_PTR kRepeatTimer = 1;
constexpr UINT kRepeatDelayMs = 400;
constexpr UINT kRepeatIntervalMs = 100;
// Safety limit in case a key release report gets lost.
constexpr int kMaxRepeats = 100;

}

App::App(HINSTANCE instance)
    : instance_(instance),
      brightness_(osd_)
{
}

bool App::Initialize()
{
    // Sent by Explorer after it restarts; the tray icon has to be added again.
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");

    if (!CreateMainWindow() || !osd_.Create(instance_)) {
        return false;
    }

    auto iconSize = GetSystemMetrics(SM_CXSMICON);
    icon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                          iconSize, iconSize, LR_DEFAULTCOLOR));
    tray_.Add(window_, icon_);

    // Settings first: they decide whether the key listeners log their findings.
    LoadSettings();
    RegisterKeyListeners();
    CheckSettings();
    return true;
}

void App::Execute(const Command& command)
{
    switch (command.kind) {
    case CommandKind::Start:
        break;
    case CommandKind::Up:
        brightness_.Step(+1);
        break;
    case CommandKind::Down:
        brightness_.Step(-1);
        break;
    case CommandKind::Set:
        brightness_.Set(command.value);
        break;
    case CommandKind::Show:
        brightness_.ShowCurrent();
        break;
    case CommandKind::Exit:
        DestroyWindow(window_);
        break;
    }
}

int App::Run()
{
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool App::CreateMainWindow()
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = kMainWindowClass;
    RegisterClassExW(&windowClass);

    // A regular top-level window that is never shown. (Message-only windows do not work well with tray menus.)
    CreateWindowExW(WS_EX_TOOLWINDOW, kMainWindowClass, kAppName, WS_POPUP, 0, 0, 0, 0,
                    nullptr, nullptr, instance_, this);
    return window_ != nullptr;
}

LRESULT CALLBACK App::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* app = WindowObject<App>(window, message, lParam);
    if (app == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    app->window_ = window;
    return app->HandleMessage(message, wParam, lParam);
}

LRESULT App::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_INPUT:
        OnRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        break;  // DefWindowProc must still run for WM_INPUT
    case WM_INPUT_DEVICE_CHANGE:
        OnInputDeviceChange(wParam, reinterpret_cast<HANDLE>(lParam));
        return 0;
    case WM_TIMER:
        if (wParam == kRepeatTimer) {
            OnRepeatTimer();
        }
        return 0;
    case WM_APP_BRIGHTNESS_RESULT:
        brightness_.OnResult(static_cast<uint32_t>(wParam), static_cast<int>(lParam));
        UpdateTooltip();
        return 0;
    case WM_APP_TRAY:
        OnTrayEvent(LOWORD(lParam));
        return 0;
    case WM_APP_PAIRING_RESULT:
        OnPairingResult(reinterpret_cast<PairingResult*>(lParam));
        return 0;
    case WM_COPYDATA: {
        auto command = CommandFromCopyData(*reinterpret_cast<const COPYDATASTRUCT*>(lParam));
        if (command) {
            Execute(*command);
        }
        return command ? TRUE : FALSE;
    }
    case WM_POWERBROADCAST:
        OnPowerEvent(wParam);
        return TRUE;
    case WM_DESTROY:
        tray_.Remove();
        PostQuitMessage(0);
        return 0;
    default:
        if (message == taskbarCreatedMessage_ && message != 0) {
            tray_.Restore();
            return 0;
        }
        break;
    }

    return DefWindowProcW(window_, message, wParam, lParam);
}

void App::RegisterKeyListeners()
{
    if (!consumerKeys_.Register(window_)) {
        logging::Write(L"Cannot listen for standard brightness keys (error %lu)", GetLastError());
    }
    if (!logitechKeys_.Register(window_)) {
        logging::Write(L"Cannot listen for Logitech brightness keys (error %lu)", GetLastError());
    }
}

void App::OnRawInput(HRAWINPUT input)
{
    const auto* raw = ReadRawInput(input, rawInputBuffer_);
    if (raw == nullptr) {
        return;
    }

    auto onKey = [this](BrightnessKey key, bool pressed) { OnBrightnessKey(key, pressed); };
    consumerKeys_.HandleInput(*raw, onKey);
    logitechKeys_.HandleInput(*raw, onKey);
}

void App::OnInputDeviceChange(WPARAM change, HANDLE device)
{
    consumerKeys_.HandleDeviceChange(change, device);
    logitechKeys_.HandleDeviceChange(change, device);
}

void App::OnBrightnessKey(BrightnessKey key, bool pressed)
{
    logging::Write(L"Brightness %ls key %ls", key == BrightnessKey::Up ? L"up" : L"down",
                   pressed ? L"pressed" : L"released");

    if (!pressed) {
        if (heldKey_ == key) {
            StopRepeat();
        }
        return;
    }

    heldKey_ = key;
    repeatCount_ = 0;
    StepFor(key);
    SetTimer(window_, kRepeatTimer, kRepeatDelayMs, nullptr);
}

void App::OnRepeatTimer()
{
    if (!heldKey_ || ++repeatCount_ > kMaxRepeats) {
        StopRepeat();
        return;
    }

    StepFor(*heldKey_);
    // Replaces the initial delay with the faster repeat interval.
    SetTimer(window_, kRepeatTimer, kRepeatIntervalMs, nullptr);
}

void App::StepFor(BrightnessKey key)
{
    brightness_.Step(key == BrightnessKey::Up ? +1 : -1);
}

void App::StopRepeat()
{
    KillTimer(window_, kRepeatTimer);
    heldKey_.reset();
}

void App::OnPowerEvent(WPARAM event)
{
    if (event == PBT_APMRESUMEAUTOMATIC) {
        // The brightness could have been changed on the monitor while the PC was asleep.
        brightness_.MarkStale();
        brightness_.Refresh(false);
    }
}
