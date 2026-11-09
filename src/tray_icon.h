#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>

// The icon in the notification area. Events arrive at the owner window as WM_APP_TRAY.
class TrayIcon {
public:
    bool Add(HWND owner, HICON icon);
    void Remove();
    void SetTooltip(const std::wstring& text);

    // Explorer forgets all icons when it restarts; call this on the "TaskbarCreated" message.
    void Restore();

private:
    NOTIFYICONDATAW data_{};
    bool added_ = false;
};
