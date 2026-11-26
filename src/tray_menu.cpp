#include "tray_menu.h"

TrayCommand ShowTrayMenu(HWND owner, const std::wstring& status)
{
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return TrayCommand::None;
    }

    auto menu = CreatePopupMenu();
    if (!menu) {
        return TrayCommand::None;
    }
    AppendMenuW(menu, MF_STRING, static_cast<UINT>(TrayCommand::ShowCurrent), status.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, static_cast<UINT>(TrayCommand::Increase), L"Increase brightness (+)");
    AppendMenuW(menu, MF_STRING, static_cast<UINT>(TrayCommand::Decrease), L"Decrease brightness (-)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, static_cast<UINT>(TrayCommand::Settings), L"Settings...");
    AppendMenuW(menu, MF_STRING, static_cast<UINT>(TrayCommand::Exit), L"Exit");

    // Without this the menu does not close when the user clicks somewhere else.
    SetForegroundWindow(owner);
    auto id = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                               cursor.x, cursor.y, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
    return static_cast<TrayCommand>(id);
}
