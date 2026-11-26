#pragma once

#include "brightness_controller.h"
#include "brightness_key.h"
#include "command_line.h"
#include "config.h"
#include "consumer_key_listener.h"
#include "logitech_key_listener.h"
#include "osd_window.h"
#include "shortcuts.h"
#include "tray_icon.h"
#include "tray_menu.h"

#include <windows.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

struct PairingResult;

// Wires everything together around one hidden window that receives
// keyboard input, tray events, worker results and commands from other launches.
class App {
public:
    explicit App(HINSTANCE instance);

    bool Initialize();
    void Execute(const Command& command);
    // Runs the message loop until the app exits.
    int Run();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool CreateMainWindow();

    // Keys (app.cpp)
    void RegisterKeyListeners();
    void OnRawInput(HRAWINPUT input);
    void OnInputDeviceChange(WPARAM change, HANDLE device);
    void OnBrightnessKey(BrightnessKey key, bool pressed);
    void OnRepeatTimer();
    void StepFor(BrightnessKey key);
    void StopRepeat();
    void OnPowerEvent(WPARAM event);

    // Tray, settings and pairing (app_menu.cpp)
    void OnTrayEvent(UINT event);
    void ShowMenu();
    void HandleTrayCommand(TrayCommand command);
    void ChangeOsdTheme(OsdTheme theme);
    void ConfigureShortcut(BrightnessKey direction);
    void ToggleBrightnessKeys();
    void OpenAboutLink(const wchar_t* url);
    void UpdateTooltip();
    void LoadSettings(bool showConnectionError = false);
    void CheckSettings();
    void ShowSettings();
    static INT_PTR CALLBACK SettingsDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK SettingsPageProc(HWND page, UINT message, WPARAM wParam, LPARAM lParam);
    enum SettingsPage { Monitor, Appearance, Keyboard, Application, About, PageCount };
    void InitializeSettingsPages();
    void SelectSettingsPage(int index);
    void UpdateMonitorStatus();
    void UpdateBrightnessPresets();
    void UpdateAppearanceControls();
    void UpdateKeyboardControls();
    void ChangeOsdSize(UINT action);
    bool HandleSettingsControl(HWND page, UINT control, UINT notification);
    void SyncSettingsToken();
    HWND DialogOwner() const { return settingsWindow_ ? settingsWindow_ : window_; }
    void OpenSettingsFile();
    void StartPairingFlow();
    void OnPairingResult(PairingResult* result);
    int ShowMessageBox(const std::wstring& text, UINT flags);

    HINSTANCE instance_;
    HWND window_ = nullptr;
    HWND settingsWindow_ = nullptr;
    std::array<HWND, PageCount> settingsPages_{};
    int settingsTab_ = Monitor;
    int savedOsdSize_ = kDefaultOsdSize;
    HICON icon_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;

    Config config_;
    Config settingsSnapshot_;
    OsdWindow osd_;
    BrightnessController brightness_;
    // Standard HID brightness keys of any keyboard.
    ConsumerKeyListener consumerKeys_;
    // Brightness keys of Logitech keyboards while Logi Options+ is running.
    LogitechKeyListener logitechKeys_;
    std::vector<BYTE> rawInputBuffer_;
    TrayIcon tray_;
    Shortcuts shortcuts_;

    // The brightness key that is held down, for auto-repeat.
    std::optional<BrightnessKey> heldKey_;
    int repeatCount_ = 0;
    bool pairingInProgress_ = false;
};
