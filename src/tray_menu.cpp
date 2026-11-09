#include "tray_menu.h"

#include <algorithm>

namespace {

enum MenuId : UINT {
    kIncreaseId = 1,
    kDecreaseId,
    kRefreshId,
    kAutostartId,
    kEditSettingsId,
    kReloadSettingsId,
    kPairId,
    kExitId,
    // Preset items use kSetValueBaseId + value.
    kSetValueBaseId = 1000,
};

constexpr size_t kMaxErrorLength = 70;
constexpr int kMaxPresets = 10;

void AddPreset(HMENU menu, int value, int currentValue)
{
    auto flags = MF_STRING | (value == currentValue ? MF_CHECKED : 0u);
    AppendMenuW(menu, flags, kSetValueBaseId + value, std::to_wstring(value).c_str());
}

// "Set brightness" submenu: 0, step, 2 * step, ..., max (at most about ten items).
HMENU BuildPresetMenu(const TrayMenuState& state)
{
    auto menu = CreatePopupMenu();
    auto presetStep = std::max(state.step, (state.maxValue + kMaxPresets - 1) / kMaxPresets);

    for (int value = 0; value < state.maxValue; value += presetStep) {
        AddPreset(menu, value, state.currentValue);
    }
    AddPreset(menu, state.maxValue, state.currentValue);

    return menu;
}

std::wstring Shorten(const std::wstring& text)
{
    if (text.size() <= kMaxErrorLength) {
        return text;
    }
    return text.substr(0, kMaxErrorLength - 3) + L"...";
}

HMENU BuildMenu(const TrayMenuState& state)
{
    auto menu = CreatePopupMenu();

    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, state.status.c_str());
    if (!state.error.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, Shorten(state.error).c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING, kIncreaseId, L"Increase");
    AppendMenuW(menu, MF_STRING, kDecreaseId, L"Decrease");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildPresetMenu(state)), L"Set brightness");
    AppendMenuW(menu, MF_STRING, kRefreshId, L"Refresh");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    auto autostartFlags = MF_STRING | (state.autostartEnabled ? MF_CHECKED : 0u);
    AppendMenuW(menu, autostartFlags, kAutostartId, L"Start with Windows");
    AppendMenuW(menu, MF_STRING, kEditSettingsId, L"Edit settings (.env)");
    AppendMenuW(menu, MF_STRING, kReloadSettingsId, L"Reload settings");
    AppendMenuW(menu, MF_STRING, kPairId, L"Pair with monitor...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING, kExitId, L"Exit");
    return menu;
}

MenuChoice ToChoice(UINT id)
{
    if (id >= kSetValueBaseId) {
        return {MenuCommand::SetValue, static_cast<int>(id - kSetValueBaseId)};
    }

    switch (id) {
    case kIncreaseId:
        return {MenuCommand::Increase};
    case kDecreaseId:
        return {MenuCommand::Decrease};
    case kRefreshId:
        return {MenuCommand::Refresh};
    case kAutostartId:
        return {MenuCommand::ToggleAutostart};
    case kEditSettingsId:
        return {MenuCommand::EditSettings};
    case kReloadSettingsId:
        return {MenuCommand::ReloadSettings};
    case kPairId:
        return {MenuCommand::Pair};
    case kExitId:
        return {MenuCommand::Exit};
    default:
        return {};
    }
}

}

MenuChoice ShowTrayMenu(HWND owner, const TrayMenuState& state)
{
    auto menu = BuildMenu(state);

    POINT cursor;
    GetCursorPos(&cursor);

    // Without this the menu does not close when the user clicks somewhere else.
    SetForegroundWindow(owner);
    auto id = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                               cursor.x, cursor.y, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);

    // Also destroys the submenu.
    DestroyMenu(menu);
    return ToChoice(static_cast<UINT>(id));
}
