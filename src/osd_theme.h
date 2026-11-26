#pragma once

#include <array>

enum class OsdTheme {
    System = 0,
    Frost = 1,
    Light = 2,
    Dark = 3,
    Modern = 4,
    Neon = 5,
    Glass = 6,
    Retro = 7,
};

struct OsdThemeOption {
    OsdTheme theme;
    const wchar_t* name;
    const wchar_t* description;
};

inline constexpr std::array kOsdThemes{
    OsdThemeOption{OsdTheme::System, L"Follow Windows theme", L"Windows-style indicator that follows the system's light or dark mode."},
    OsdThemeOption{OsdTheme::Frost, L"Frost (macOS-inspired)", L"A tall gray panel with a large sun and a segmented meter."},
    OsdThemeOption{OsdTheme::Light, L"Light", L"A warm ivory panel with an amber meter."},
    OsdThemeOption{OsdTheme::Dark, L"Dark", L"A charcoal panel with a muted violet meter."},
    OsdThemeOption{OsdTheme::Modern, L"Modern", L"A navy pill with a wide blue meter."},
    OsdThemeOption{OsdTheme::Neon, L"Neon", L"A dark panel with a high-contrast green meter."},
    OsdThemeOption{OsdTheme::Glass, L"Glass", L"A translucent card with a soft gradient that follows Windows light or dark mode."},
    OsdThemeOption{OsdTheme::Retro, L"Retro LED", L"An amber segmented meter and monospace numbers."},
};

inline constexpr int kDefaultOsdSize = 100;
inline constexpr int kMinOsdSize = 75;
inline constexpr int kMaxOsdSize = 150;

OsdTheme LoadOsdTheme();
bool SaveOsdTheme(OsdTheme theme);
int LoadOsdSize();
bool SaveOsdSize(int percent);
