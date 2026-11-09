#include "osd_window.h"

#include "win_util.h"

#include <ShellScalingApi.h>

#include <algorithm>
#include <memory>
#include <type_traits>

namespace {

constexpr wchar_t kClassName[] = L"SamsungBrightnessOsd";
constexpr wchar_t kIconFont[] = L"Segoe MDL2 Assets";
constexpr wchar_t kTextFont[] = L"Segoe UI";
constexpr wchar_t kBrightnessGlyph[] = L"\xE706";

constexpr UINT_PTR kHideTimer = 1;
constexpr UINT_PTR kFadeTimer = 2;
constexpr UINT kVisibleMs = 1500;
constexpr UINT kFadeIntervalMs = 15;
constexpr BYTE kFadeStep = 25;

// Sizes in device-independent pixels; they are scaled to the monitor DPI.
constexpr int kWidth = 240;
constexpr int kHeight = 52;
constexpr int kCornerRadius = 8;
constexpr int kBottomMargin = 56;
constexpr int kPadding = 18;
constexpr int kIconSize = 18;
constexpr int kTextSize = 15;
constexpr int kGap = 14;
constexpr int kNumberWidth = 28;
constexpr int kTrackHeight = 4;

struct Palette {
    COLORREF background;
    COLORREF border;
    COLORREF text;
    COLORREF track;
    COLORREF accent;
};

bool IsLightTheme()
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    auto status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                               L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);

    return status == ERROR_SUCCESS && value != 0;
}

// Colors close to the Windows 11 volume and brightness flyout.
Palette CurrentPalette()
{
    if (IsLightTheme()) {
        return {RGB(249, 249, 249), RGB(222, 222, 222), RGB(26, 26, 26), RGB(196, 196, 196), RGB(0, 95, 184)};
    }
    return {RGB(44, 44, 44), RGB(68, 68, 68), RGB(255, 255, 255), RGB(110, 110, 110), RGB(76, 194, 255)};
}

// Deletes GDI objects (brushes, pens, fonts, bitmaps) when they go out of scope.
struct GdiDeleter {
    void operator()(HGDIOBJ object) const
    {
        DeleteObject(object);
    }
};

template <typename Handle>
using GdiPtr = std::unique_ptr<std::remove_pointer_t<Handle>, GdiDeleter>;

// Selects a GDI object into a device context and restores the previous one when it goes out of scope.
class ScopedSelect {
public:
    ScopedSelect(HDC dc, HGDIOBJ object)
        : dc_(dc), previous_(SelectObject(dc, object))
    {
    }

    ~ScopedSelect()
    {
        SelectObject(dc_, previous_);
    }

    ScopedSelect(const ScopedSelect&) = delete;
    ScopedSelect& operator=(const ScopedSelect&) = delete;

private:
    HDC dc_;
    HGDIOBJ previous_;
};

GdiPtr<HFONT> CreateUiFont(const wchar_t* face, int pixelHeight, int weight)
{
    return GdiPtr<HFONT>(CreateFontW(-pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH, face));
}

void FillRoundRect(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF border)
{
    GdiPtr<HBRUSH> brush(CreateSolidBrush(fill));
    GdiPtr<HPEN> pen(CreatePen(PS_SOLID, 1, border));
    ScopedSelect selectBrush(dc, brush.get());
    ScopedSelect selectPen(dc, pen.get());

    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);
}

void DrawTextLine(HDC dc, const wchar_t* text, RECT rect, HFONT font, COLORREF color, UINT alignment)
{
    ScopedSelect selectFont(dc, font);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, alignment | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

}

bool OsdWindow::Create(HINSTANCE instance)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);

    // LAYERED: fade in/out; TOOLWINDOW: no taskbar button; NOACTIVATE + TRANSPARENT: never steals focus or clicks.
    auto extendedStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
    window_ = CreateWindowExW(extendedStyle, kClassName, L"", WS_POPUP, 0, 0, 0, 0,
                              nullptr, nullptr, instance, this);

    return window_ != nullptr;
}

void OsdWindow::ShowLevel(int value, int maxValue)
{
    maxValue_ = std::max(maxValue, 1);
    value_ = std::clamp(value, 0, maxValue_);
    message_.clear();
    ShowNow();
}

void OsdWindow::ShowMessage(const std::wstring& message)
{
    message_ = message;
    ShowNow();
}

LRESULT CALLBACK OsdWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* osd = WindowObject<OsdWindow>(window, message, lParam);
    if (osd == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    osd->window_ = window;
    return osd->HandleMessage(message, wParam, lParam);
}

LRESULT OsdWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_PAINT:
        OnPaint();
        return 0;
    case WM_ERASEBKGND:
        return 1;  // everything is painted in WM_PAINT
    case WM_TIMER:
        OnTimer(wParam);
        return 0;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    default:
        return DefWindowProcW(window_, message, wParam, lParam);
    }
}

void OsdWindow::ShowNow()
{
    if (window_ == nullptr) {
        return;
    }

    KillTimer(window_, kFadeTimer);
    UpdatePlacement();

    alpha_ = 255;
    SetLayeredWindowAttributes(window_, 0, alpha_, LWA_ALPHA);
    SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(window_, nullptr, FALSE);

    // Restarts the countdown if the OSD is already visible.
    SetTimer(window_, kHideTimer, kVisibleMs, nullptr);
}

// Bottom center of the primary monitor, above the taskbar.
void OsdWindow::UpdatePlacement()
{
    auto monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);

    UINT dpiX = USER_DEFAULT_SCREEN_DPI;
    UINT dpiY = USER_DEFAULT_SCREEN_DPI;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    dpi_ = dpiX;

    auto width = Scale(kWidth);
    auto height = Scale(kHeight);
    const auto& workArea = info.rcWork;
    auto x = workArea.left + (workArea.right - workArea.left - width) / 2;
    auto y = workArea.bottom - height - Scale(kBottomMargin);

    SetWindowPos(window_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);

    // Rounded corners. The window takes ownership of the region.
    auto corner = Scale(kCornerRadius) * 2;
    SetWindowRgn(window_, CreateRoundRectRgn(0, 0, width + 1, height + 1, corner, corner), TRUE);
}

void OsdWindow::OnTimer(UINT_PTR timerId)
{
    if (timerId == kHideTimer) {
        KillTimer(window_, kHideTimer);
        SetTimer(window_, kFadeTimer, kFadeIntervalMs, nullptr);
        return;
    }

    if (timerId != kFadeTimer) {
        return;
    }

    if (alpha_ <= kFadeStep) {
        KillTimer(window_, kFadeTimer);
        ShowWindow(window_, SW_HIDE);
        return;
    }

    alpha_ -= kFadeStep;
    SetLayeredWindowAttributes(window_, 0, alpha_, LWA_ALPHA);
}

// Paints into an off-screen bitmap first to avoid flicker.
void OsdWindow::OnPaint()
{
    PAINTSTRUCT paint;
    auto screenDc = BeginPaint(window_, &paint);

    RECT bounds;
    GetClientRect(window_, &bounds);

    auto bufferDc = CreateCompatibleDC(screenDc);
    GdiPtr<HBITMAP> buffer(CreateCompatibleBitmap(screenDc, bounds.right, bounds.bottom));

    {
        ScopedSelect selectBuffer(bufferDc, buffer.get());
        Paint(bufferDc, bounds);
        BitBlt(screenDc, 0, 0, bounds.right, bounds.bottom, bufferDc, 0, 0, SRCCOPY);
    }

    DeleteDC(bufferDc);
    EndPaint(window_, &paint);
}

// Layout: [sun icon]  [======level bar=====]  [number]
void OsdWindow::Paint(HDC dc, const RECT& bounds) const
{
    auto palette = CurrentPalette();
    FillRoundRect(dc, bounds, Scale(kCornerRadius), palette.background, palette.border);
    SetBkMode(dc, TRANSPARENT);

    auto iconRect = bounds;
    iconRect.left += Scale(kPadding);
    iconRect.right = iconRect.left + Scale(kIconSize);
    auto iconFont = CreateUiFont(kIconFont, Scale(kIconSize), FW_NORMAL);
    DrawTextLine(dc, kBrightnessGlyph, iconRect, iconFont.get(), palette.text, DT_CENTER);

    auto content = bounds;
    content.left = iconRect.right + Scale(kGap);
    content.right -= Scale(kPadding);
    auto textFont = CreateUiFont(kTextFont, Scale(kTextSize), FW_SEMIBOLD);

    if (!message_.empty()) {
        DrawTextLine(dc, message_.c_str(), content, textFont.get(), palette.text, DT_LEFT);
        return;
    }

    auto numberRect = content;
    numberRect.left = content.right - Scale(kNumberWidth);
    DrawTextLine(dc, std::to_wstring(value_).c_str(), numberRect, textFont.get(), palette.text, DT_RIGHT);

    auto barRect = content;
    barRect.right = numberRect.left - Scale(kGap);
    PaintLevelBar(dc, barRect, palette.track, palette.accent);
}

void OsdWindow::PaintLevelBar(HDC dc, RECT area, COLORREF trackColor, COLORREF fillColor) const
{
    auto height = Scale(kTrackHeight);
    area.top = (area.top + area.bottom - height) / 2;
    area.bottom = area.top + height;
    FillRoundRect(dc, area, height / 2, trackColor, trackColor);

    if (value_ == 0) {
        return;
    }

    auto filled = area;
    filled.right = area.left + MulDiv(area.right - area.left, value_, maxValue_);
    FillRoundRect(dc, filled, height / 2, fillColor, fillColor);
}

int OsdWindow::Scale(int dips) const
{
    return MulDiv(dips, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
}
