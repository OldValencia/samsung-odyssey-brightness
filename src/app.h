#pragma once

#include "brightness_controller.h"
#include "command_line.h"
#include "config.h"
#include "key_listener.h"
#include "osd_window.h"
#include "tray_icon.h"
#include "tray_menu.h"

#include <windows.h>

#include <optional>
#include <string>

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
    void OnRawInput(HRAWINPUT input);
    void OnBrightnessKey(BrightnessKey key, bool pressed);
    void OnRepeatTimer();
    void StepFor(BrightnessKey key);
    void StopRepeat();
    void OnPowerEvent(WPARAM event);

    // Tray, settings and pairing (app_menu.cpp)
    void OnTrayEvent(UINT event);
    void ShowMenu();
    void HandleMenuChoice(const MenuChoice& choice);
    void UpdateTooltip();
    void LoadSettings();
    void CheckSettings();
    void OpenSettingsFile();
    void StartPairingFlow();
    void OnPairingResult(PairingResult* result);
    int ShowMessageBox(const std::wstring& text, UINT flags);

    HINSTANCE instance_;
    HWND window_ = nullptr;
    HICON icon_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;

    Config config_;
    OsdWindow osd_;
    BrightnessController brightness_;
    BrightnessKeyListener keyListener_;
    TrayIcon tray_;

    // The brightness key that is held down, for auto-repeat.
    std::optional<BrightnessKey> heldKey_;
    int repeatCount_ = 0;
    bool pairingInProgress_ = false;
};
