#include "tray_icon.h"

#include "app_constants.h"

#include <cwchar>

bool TrayIcon::Add(HWND owner, HICON icon)
{
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = WM_APP_TRAY;
    data_.hIcon = icon;
    wcscpy_s(data_.szTip, kAppName);

    added_ = Shell_NotifyIconW(NIM_ADD, &data_) == TRUE;
    if (!added_) {
        return false;
    }

    // Version 4 reports left clicks as NIN_SELECT and right clicks as WM_CONTEXTMENU.
    data_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data_);
    return true;
}

void TrayIcon::Remove()
{
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &data_);
        added_ = false;
    }
}

void TrayIcon::SetTooltip(const std::wstring& text)
{
    wcsncpy_s(data_.szTip, text.c_str(), _TRUNCATE);

    if (added_) {
        Shell_NotifyIconW(NIM_MODIFY, &data_);
    }
}

void TrayIcon::Restore()
{
    if (data_.hWnd != nullptr) {
        Add(data_.hWnd, data_.hIcon);
    }
}
