#pragma once

#include <windows.h>

inline constexpr wchar_t kAppName[] = L"Samsung Brightness";

// Window class of the hidden main window. A second launch uses it to find the running instance.
inline constexpr wchar_t kMainWindowClass[] = L"SamsungBrightnessMainWindow";

// Private window messages of the main window.
// wParam: request id, lParam: brightness value or -1 when the request failed.
inline constexpr UINT WM_APP_BRIGHTNESS_RESULT = WM_APP + 1;
// Tray icon notifications (NOTIFYICON_VERSION_4 format).
inline constexpr UINT WM_APP_TRAY = WM_APP + 2;
// lParam: PairingResult* allocated by the pairing thread; the receiver deletes it.
inline constexpr UINT WM_APP_PAIRING_RESULT = WM_APP + 3;
