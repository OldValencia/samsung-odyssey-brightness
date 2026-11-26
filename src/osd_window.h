#pragma once

#include "osd_theme.h"

#include <windows.h>

#include <string>

// A themed overlay at the bottom of the primary screen.
// It shows the brightness level for a moment and then fades out.
// It never takes focus and lets mouse clicks pass through.
class OsdWindow {
public:
    bool Create(HINSTANCE instance);

    void SetTheme(OsdTheme theme);
    OsdTheme Theme() const { return theme_; }
    void SetSizePercent(int percent);
    int SizePercent() const { return sizePercent_; }
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
    void PaintLevelBar(HDC dc, RECT area, int trackHeight, COLORREF trackColor, COLORREF fillColor) const;
    void PaintSegments(HDC dc, RECT area, int gap, int radius, COLORREF trackColor, COLORREF fillColor) const;
    int Scale(int dips) const;

    HWND window_ = nullptr;
    UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
    BYTE alpha_ = 0;
    ULONGLONG hideAfter_ = 0;
    int regionDiameter_ = 0;
    OsdTheme theme_ = OsdTheme::System;
    int sizePercent_ = kDefaultOsdSize;

    int value_ = 0;
    int maxValue_ = 1;
    // When not empty, the message is shown instead of the level.
    std::wstring message_;
};
