#include "osd_theme.h"

#include <windows.h>

namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\SamsungBrightness";
constexpr wchar_t kThemeValue[] = L"OsdTheme";
constexpr wchar_t kSizeValue[] = L"OsdSizePercent";

DWORD ReadPreference(const wchar_t* name, DWORD fallback)
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    auto status = RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD,
                               nullptr, &value, &size);
    return status == ERROR_SUCCESS ? value : fallback;
}

bool WritePreference(const wchar_t* name, DWORD value)
{
    return RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD,
                           &value, sizeof(value)) == ERROR_SUCCESS;
}

}

OsdTheme LoadOsdTheme()
{
    const auto value = ReadPreference(kThemeValue, static_cast<DWORD>(OsdTheme::System));
    for (const auto& option : kOsdThemes) {
        if (value == static_cast<DWORD>(option.theme)) {
            return option.theme;
        }
    }
    return OsdTheme::System;
}

bool SaveOsdTheme(OsdTheme theme)
{
    return WritePreference(kThemeValue, static_cast<DWORD>(theme));
}

int LoadOsdSize()
{
    const auto value = ReadPreference(kSizeValue, kDefaultOsdSize);
    return value >= kMinOsdSize && value <= kMaxOsdSize ? static_cast<int>(value) : kDefaultOsdSize;
}

bool SaveOsdSize(int percent)
{
    return WritePreference(kSizeValue, static_cast<DWORD>(percent));
}
