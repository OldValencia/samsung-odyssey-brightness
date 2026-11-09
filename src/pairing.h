#pragma once

#include <windows.h>

#include <string>

struct PairingResult {
    bool success = false;
    // The new access token on success, otherwise the error message.
    std::wstring text;
};

// Requests a new access token on a background thread. The monitor shows a prompt that the user must accept.
// The result arrives as WM_APP_PAIRING_RESULT with lParam = PairingResult*; the receiver deletes it.
void StartPairing(HWND resultWindow, const std::wstring& monitorIp, int port);
