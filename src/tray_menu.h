#pragma once

#include <windows.h>

#include <string>

enum class TrayCommand {
    None,
    ShowCurrent,
    Increase,
    Decrease,
    Settings,
    Exit,
};

// Shows the tray context menu at the mouse cursor and waits for the user's choice.
TrayCommand ShowTrayMenu(HWND owner, const std::wstring& status);
