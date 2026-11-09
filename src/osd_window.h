#pragma once

#include <windows.h>

#include <string>

// A small Windows 11 style overlay at the bottom of the primary screen.
// It shows the brightness level for a moment and then fades out.
// It never takes focus and lets mouse clicks pass through.
class OsdWindow {
public:
    bool Create(HINSTANCE instance);

    void ShowLevel(int value, int maxValue);
    void ShowMessage(const std::wstring& message);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void ShowNow();
    void UpdatePlacement();
    void OnTimer(UINT_PTR timerId);
    void OnPaint();
    void Paint(HDC dc, const RECT& bounds) const;
    void PaintLevelBar(HDC dc, RECT area, COLORREF trackColor, COLORREF fillColor) const;
    int Scale(int dips) const;

    HWND window_ = nullptr;
    UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
    BYTE alpha_ = 0;

    int value_ = 0;
    int maxValue_ = 1;
    // When not empty, the message is shown instead of the level.
    std::wstring message_;
};
