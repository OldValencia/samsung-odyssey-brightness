#pragma once

#include <windows.h>

#include <string>

enum class MenuCommand {
    None,
    Increase,
    Decrease,
    SetValue,
    Refresh,
    ToggleAutostart,
    EditSettings,
    ReloadSettings,
    Pair,
    Exit,
};

struct MenuChoice {
    MenuCommand command = MenuCommand::None;
    // Used by MenuCommand::SetValue.
    int value = 0;
};

// What the menu shows.
struct TrayMenuState {
    std::wstring status;
    // Shown under the status when not empty.
    std::wstring error;
    // -1 when unknown.
    int currentValue = -1;
    int maxValue = 50;
    int step = 5;
    bool autostartEnabled = false;
};

// Shows the tray context menu at the mouse cursor and waits for the user's choice.
MenuChoice ShowTrayMenu(HWND owner, const TrayMenuState& state);
