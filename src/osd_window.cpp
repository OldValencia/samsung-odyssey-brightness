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
constexpr wchar_t kRetroFont[] = L"Consolas";
constexpr wchar_t kBrightnessGlyph[] = L"\xE706";

constexpr UINT_PTR kHideTimer = 1;
constexpr UINT_PTR kFadeTimer = 2;
constexpr UINT kVisibleMs = 1500;
constexpr UINT kFadeIntervalMs = 15;
constexpr BYTE kFadeStep = 25;
constexpr BYTE kGlassOpacity = 245;

// Sizes in device-independent pixels; they are scaled to the monitor DPI.
constexpr int kBottomMargin = 56;
constexpr int kPadding = 18;
constexpr int kIconSize = 18;
constexpr int kTextSize = 15;
constexpr int kGap = 14;
constexpr int kNumberWidth = 28;

struct Palette {
    COLORREF background;
    COLORREF border;
    COLORREF text;
    COLORREF track;
    COLORREF accent;
    COLORREF highlight = 0;
};

struct Layout {
    int width = 240;
    int height = 52;
    int cornerRadius = 8;
    int trackHeight = 4;
};

Layout LayoutFor(OsdTheme theme)
{
    switch (theme) {
    case OsdTheme::Frost:
        return {204, 184, 27, 8};
    case OsdTheme::Modern:
        return {270, 60, 24, 14};
    case OsdTheme::Neon:
        return {250, 56, 12, 8};
    case OsdTheme::Glass:
        return {292, 66, 21, 7};
    case OsdTheme::Retro:
        return {256, 50, 4, 12};
    default:
        return {};
    }
}

bool IsLightTheme()
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    auto status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                               L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);

    return status == ERROR_SUCCESS && value != 0;
}

Palette PaletteFor(OsdTheme theme, bool light)
{
    switch (theme) {
    case OsdTheme::Frost:
        return {RGB(160, 165, 171), RGB(153, 159, 166), RGB(29, 31, 35), RGB(45, 49, 55), RGB(250, 251, 253)};
    case OsdTheme::Light:
        return {RGB(255, 250, 241), RGB(231, 218, 194), RGB(57, 43, 26), RGB(230, 219, 200), RGB(164, 86, 21)};
    case OsdTheme::Dark:
        return {RGB(27, 27, 32), RGB(59, 59, 68), RGB(238, 235, 244), RGB(68, 66, 79), RGB(183, 166, 230)};
    case OsdTheme::Modern:
        return {RGB(22, 31, 53), RGB(55, 74, 113), RGB(231, 240, 255), RGB(53, 69, 102), RGB(126, 162, 255)};
    case OsdTheme::Neon:
        return {RGB(18, 18, 25), RGB(76, 109, 88), RGB(219, 255, 225), RGB(54, 70, 60), RGB(133, 255, 163)};
    case OsdTheme::Glass:
        if (light) {
            return {RGB(231, 234, 238), RGB(231, 234, 238), RGB(32, 37, 43),
                    RGB(202, 207, 214), RGB(64, 83, 105), RGB(251, 251, 251)};
        }
        return {RGB(38, 41, 46), RGB(38, 41, 46), RGB(244, 244, 243),
                RGB(84, 88, 94), RGB(221, 226, 233), RGB(64, 67, 72)};
    case OsdTheme::Retro:
        return {RGB(29, 21, 16), RGB(106, 67, 34), RGB(255, 201, 120), RGB(73, 47, 29), RGB(255, 162, 61)};
    case OsdTheme::System:
        break;
    }

    // Colors close to the Windows 11 volume and brightness flyout.
    if (light) {
        return {RGB(249, 249, 249), RGB(222, 222, 222), RGB(26, 26, 26), RGB(196, 196, 196), RGB(0, 95, 184)};
    }
    return {RGB(44, 44, 44), RGB(68, 68, 68), RGB(255, 255, 255), RGB(110, 110, 110), RGB(76, 194, 255)};
}

bool FollowsWindowsTheme(OsdTheme theme)
{
    return theme == OsdTheme::System || theme == OsdTheme::Glass;
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
    if (rect.left >= rect.right || rect.top >= rect.bottom) {
        return;
    }
    const auto diameter = (std::min)({static_cast<LONG>(radius * 2), rect.right - rect.left, rect.bottom - rect.top});
    GdiPtr<HBRUSH> brush(CreateSolidBrush(fill));
    GdiPtr<HPEN> pen(CreatePen(PS_SOLID, 1, border));
    ScopedSelect selectBrush(dc, brush.get());
    ScopedSelect selectPen(dc, pen.get());

    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, diameter, diameter);
}

TRIVERTEX GradientCorner(LONG x, LONG y, COLORREF color)
{
    return {x, y, static_cast<COLOR16>(GetRValue(color) << 8), static_cast<COLOR16>(GetGValue(color) << 8),
            static_cast<COLOR16>(GetBValue(color) << 8), 0};
}

void PaintGlassSurface(HDC dc, RECT area, int radius, const Palette& palette)
{
    // Leave the rim painted by FillRoundRect visible outside the gradient.
    InflateRect(&area, -1, -1);
    const int diameter = std::max(1, 2 * radius - 2);
    GdiPtr<HRGN> clip(CreateRoundRectRgn(area.left, area.top, area.right, area.bottom, diameter, diameter));
    POINT origin{};
    if (clip && GetViewportOrgEx(dc, &origin)) {
        OffsetRgn(clip.get(), origin.x, origin.y);
    }
    const auto saved = SaveDC(dc);
    if (saved != 0) {
        if (clip && ExtSelectClipRgn(dc, clip.get(), RGN_AND) != ERROR) {
            TRIVERTEX corners[]{GradientCorner(area.left, area.top, palette.highlight),
                                GradientCorner(area.right, area.bottom, palette.background)};
            GRADIENT_RECT mesh{0, 1};
            GdiGradientFill(dc, corners, 2, &mesh, 1, GRADIENT_FILL_RECT_V);
        }
        RestoreDC(dc, saved);
    }
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

void OsdWindow::SetTheme(OsdTheme theme)
{
    theme_ = theme;
}

void OsdWindow::SetSizePercent(int percent)
{
    sizePercent_ = percent;
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
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        if (FollowsWindowsTheme(theme_) && IsWindowVisible(window_)) {
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
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
    hideAfter_ = GetTickCount64() + kVisibleMs;
    UpdatePlacement();

    alpha_ = theme_ == OsdTheme::Glass ? kGlassOpacity : 255;
    SetLayeredWindowAttributes(window_, 0, alpha_, LWA_ALPHA);
    SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(window_, nullptr, FALSE);
    UpdateWindow(window_);

    // Restarts the countdown if the OSD is already visible.
    SetTimer(window_, kHideTimer, kVisibleMs, nullptr);
}

// Bottom center of the primary monitor, above the taskbar.
void OsdWindow::UpdatePlacement()
{
    auto monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        info.rcWork = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }

    UINT dpiX = USER_DEFAULT_SCREEN_DPI;
    UINT dpiY = USER_DEFAULT_SCREEN_DPI;
    dpi_ = SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))
        ? dpiX : USER_DEFAULT_SCREEN_DPI;

    auto layout = LayoutFor(theme_);
    auto width = Scale(layout.width);
    auto height = Scale(layout.height);
    const auto& workArea = info.rcWork;
    auto x = workArea.left + (workArea.right - workArea.left - width) / 2;
    auto bottomMargin = MulDiv(kBottomMargin, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
    auto y = workArea.bottom - height - bottomMargin;

    RECT previous{};
    GetClientRect(window_, &previous);
    SetWindowPos(window_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);

    // Rounded corners. The window takes ownership of the region.
    auto corner = Scale(layout.cornerRadius) * 2;
    if (previous.right == width && previous.bottom == height && regionDiameter_ == corner) {
        return;
    }
    GdiPtr<HRGN> region(CreateRoundRectRgn(0, 0, width, height, corner, corner));
    if (region && SetWindowRgn(window_, region.get(), TRUE)) {
        region.release();
        regionDiameter_ = corner;
    }
}

void OsdWindow::OnTimer(UINT_PTR timerId)
{
    const auto now = GetTickCount64();
    if (now < hideAfter_) {
        if (timerId == kHideTimer) {
            SetTimer(window_, kHideTimer, static_cast<UINT>(hideAfter_ - now), nullptr);
        }
        return;
    }
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

void OsdWindow::Paint(HDC dc, const RECT& bounds) const
{
    auto palette = PaletteFor(theme_, FollowsWindowsTheme(theme_) && IsLightTheme());
    auto layout = LayoutFor(theme_);
    GdiPtr<HBRUSH> background(CreateSolidBrush(palette.background));
    FillRect(dc, &bounds, background.get());
    FillRoundRect(dc, bounds, Scale(layout.cornerRadius), palette.background, palette.border);
    if (theme_ == OsdTheme::Glass) {
        PaintGlassSurface(dc, bounds, Scale(layout.cornerRadius), palette);
    }
    SetBkMode(dc, TRANSPARENT);

    auto iconRect = bounds;
    iconRect.left += Scale(kPadding);
    iconRect.right = iconRect.left + Scale(kIconSize);
    auto content = bounds;
    content.left = iconRect.right + Scale(kGap);
    content.right -= Scale(kPadding);

    auto iconSize = kIconSize;
    auto alignment = DT_LEFT;
    if (theme_ == OsdTheme::Glass) {
        iconSize = 22;
        const auto badgeSize = Scale(38);
        const auto top = (bounds.bottom - badgeSize) / 2;
        iconRect = {Scale(14), top, Scale(14) + badgeSize, top + badgeSize};
        FillRoundRect(dc, iconRect, badgeSize / 2, palette.background, palette.background);
        content.left = iconRect.right + Scale(12);
    } else if (theme_ == OsdTheme::Frost) {
        iconSize = 62;
        iconRect = {bounds.left, Scale(23), bounds.right, Scale(121)};
        content.left = bounds.left + Scale(27);
        content.right = bounds.right - Scale(27);
        content.top = Scale(120);
        content.bottom -= Scale(23);
        alignment = DT_CENTER;
    }

    auto iconFont = CreateUiFont(kIconFont, Scale(iconSize), FW_NORMAL);
    DrawTextLine(dc, kBrightnessGlyph, iconRect, iconFont.get(), palette.text, DT_CENTER);
    const auto textFace = theme_ == OsdTheme::Retro ? kRetroFont : kTextFont;
    auto textFont = CreateUiFont(textFace, Scale(kTextSize), FW_SEMIBOLD);

    if (!message_.empty()) {
        auto lineBreak = message_.find(L'\n');
        if (lineBreak == std::wstring::npos) {
            DrawTextLine(dc, message_.c_str(), content, textFont.get(), palette.text, alignment);
        } else {
            auto title = content;
            title.bottom = (content.top + content.bottom) / 2;
            content.top = title.bottom;
            DrawTextLine(dc, message_.substr(0, lineBreak).c_str(), title, textFont.get(), palette.text, alignment);
            auto detailFont = CreateUiFont(kTextFont, Scale(kTextSize - 2), FW_NORMAL);
            DrawTextLine(dc, message_.c_str() + lineBreak + 1, content, detailFont.get(), palette.text, alignment);
        }
        return;
    }

    const auto number = std::to_wstring(value_);
    if (theme_ == OsdTheme::Frost) {
        auto numberRect = content;
        numberRect.bottom = Scale(144);
        DrawTextLine(dc, number.c_str(), numberRect, textFont.get(), palette.text, DT_CENTER);
        content.top = Scale(153);
        content.bottom = content.top + Scale(layout.trackHeight);
        PaintSegments(dc, content, Scale(1), Scale(1), palette.track, palette.accent);
        return;
    }

    auto numberRect = content;
    numberRect.left = content.right - Scale(kNumberWidth);
    if (theme_ == OsdTheme::Glass) {
        auto heading = content;
        heading.top = Scale(12);
        heading.bottom = Scale(37);
        heading.right = numberRect.left;
        DrawTextLine(dc, L"Brightness", heading, textFont.get(), palette.text, DT_LEFT);
        numberRect.top = heading.top;
        numberRect.bottom = heading.bottom;
        DrawTextLine(dc, number.c_str(), numberRect, textFont.get(), palette.text, DT_RIGHT);
        content.top = Scale(40);
        content.bottom = Scale(51);
        PaintLevelBar(dc, content, layout.trackHeight, palette.track, palette.accent);
        return;
    }
    DrawTextLine(dc, number.c_str(), numberRect, textFont.get(), palette.text, DT_RIGHT);

    auto barRect = content;
    barRect.right = numberRect.left - Scale(kGap);
    if (theme_ == OsdTheme::Retro) {
        const auto height = Scale(layout.trackHeight);
        barRect.top = (barRect.top + barRect.bottom - height) / 2;
        barRect.bottom = barRect.top + height;
        PaintSegments(dc, barRect, Scale(3), Scale(1), palette.track, palette.accent);
    } else {
        PaintLevelBar(dc, barRect, layout.trackHeight, palette.track, palette.accent);
    }
}

void OsdWindow::PaintLevelBar(HDC dc, RECT area, int trackHeight, COLORREF trackColor, COLORREF fillColor) const
{
    auto height = Scale(trackHeight);
    area.top = (area.top + area.bottom - height) / 2;
    area.bottom = area.top + height;
    FillRoundRect(dc, area, height / 2, trackColor, trackColor);

    auto filled = area;
    filled.right = area.left + MulDiv(area.right - area.left, value_, maxValue_);
    if (filled.right > filled.left) {
        FillRoundRect(dc, filled, height / 2, fillColor, fillColor);
    }
}

void OsdWindow::PaintSegments(HDC dc, RECT area, int gap, int radius, COLORREF trackColor, COLORREF fillColor) const
{
    constexpr int kSegments = 10;
    const auto width = area.right - area.left + gap;
    const auto fillRight = area.left + MulDiv(width - gap, value_, maxValue_);
    for (int i = 0; i < kSegments; ++i) {
        RECT segment{area.left + MulDiv(width, i, kSegments), area.top,
                     area.left + MulDiv(width, i + 1, kSegments) - gap, area.bottom};
        FillRoundRect(dc, segment, radius, trackColor, trackColor);
        segment.right = std::min(segment.right, fillRight);
        if (segment.right > segment.left) {
            FillRoundRect(dc, segment, radius, fillColor, fillColor);
        }
    }
}

int OsdWindow::Scale(int dips) const
{
    return MulDiv(dips, static_cast<int>(dpi_) * sizePercent_, USER_DEFAULT_SCREEN_DPI * 100);
}
