#include "app.h"

#include "autostart.h"
#include "resource.h"

#include <commctrl.h>

#include <cstdlib>

namespace {

constexpr int kSizeStops[]{75, 100, 125, 150};
constexpr wchar_t kGitHubUrl[] = L"https://github.com/OldValencia/samsung-odyssey-brightness";
constexpr wchar_t kBuyMeACoffeeUrl[] = L"https://buymeacoffee.com/oldvalencia";
constexpr wchar_t kKoFiUrl[] = L"https://ko-fi.com/oldvalencia";

void ShowSizeLabel(HWND page, int percent)
{
    SetDlgItemTextW(page, IDC_OSD_SIZE_VALUE, (std::to_wstring(percent) + L"%").c_str());
}

}

void App::UpdateAppearanceControls()
{
    const auto page = settingsPages_[Appearance];
    const auto list = GetDlgItem(page, IDC_SETTINGS_THEME);
    const auto count = SendMessageW(list, LB_GETCOUNT, 0, 0);
    for (LRESULT i = 0; i < count; ++i) {
        if (SendMessageW(list, LB_GETITEMDATA, i, 0) == static_cast<LPARAM>(osd_.Theme())) {
            SendMessageW(list, LB_SETCURSEL, i, 0);
            break;
        }
    }
    for (const auto& option : kOsdThemes) {
        if (option.theme == osd_.Theme()) {
            SetDlgItemTextW(page, IDC_SETTINGS_THEME_DESCRIPTION, option.description);
            break;
        }
    }
    ShowSizeLabel(page, osd_.SizePercent());
}

void App::ChangeOsdSize(UINT action)
{
    const auto page = settingsPages_[Appearance];
    const auto slider = GetDlgItem(page, IDC_OSD_SIZE_SLIDER);
    auto percent = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
    if (action == TB_THUMBTRACK || action == TB_THUMBPOSITION) {
        for (const auto stop : kSizeStops) {
            if (std::abs(percent - stop) <= 2) {
                percent = stop;
                SendMessageW(slider, TBM_SETPOS, TRUE, percent);
                break;
            }
        }
    }
    if (action != TB_THUMBTRACK && percent != savedOsdSize_) {
        if (SaveOsdSize(percent)) {
            savedOsdSize_ = percent;
        } else {
            percent = savedOsdSize_;
            SendMessageW(slider, TBM_SETPOS, TRUE, percent);
            ShowMessageBox(L"The OSD size could not be saved.", MB_OK | MB_ICONWARNING);
        }
    }
    if (percent != osd_.SizePercent()) {
        osd_.SetSizePercent(percent);
        ShowSizeLabel(page, percent);
        brightness_.Preview();
    }
}

void App::UpdateKeyboardControls()
{
    const auto page = settingsPages_[Keyboard];
    SetDlgItemTextW(page, IDC_SETTINGS_SHORTCUT_UP, shortcuts_.Label(BrightnessKey::Up).c_str());
    SetDlgItemTextW(page, IDC_SETTINGS_SHORTCUT_DOWN, shortcuts_.Label(BrightnessKey::Down).c_str());
    CheckDlgButton(page, IDC_SETTINGS_USE_BRIGHTNESS_KEYS, shortcuts_.UseBrightnessKeys() ? BST_CHECKED : BST_UNCHECKED);
}

bool App::HandleSettingsControl(HWND page, UINT control, UINT notification)
{
    switch (control) {
    case IDC_SETTINGS_THEME: {
        if (notification == LBN_SELCHANGE) {
            const auto list = GetDlgItem(page, IDC_SETTINGS_THEME);
            const auto row = SendMessageW(list, LB_GETCURSEL, 0, 0);
            if (row != LB_ERR) {
                const auto theme = SendMessageW(list, LB_GETITEMDATA, row, 0);
                ChangeOsdTheme(static_cast<OsdTheme>(theme));
                UpdateAppearanceControls();
            }
        }
        return true;
    }
    case IDC_SETTINGS_PREVIEW:
        brightness_.Preview();
        return true;
    case IDC_SETTINGS_PRESETS:
        if (notification == CBN_SELCHANGE) {
            EnableWindow(GetDlgItem(page, IDC_SETTINGS_SET_LEVEL), TRUE);
        }
        return true;
    case IDC_SETTINGS_SET_LEVEL: {
        const auto combo = GetDlgItem(page, IDC_SETTINGS_PRESETS);
        const auto row = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (row != CB_ERR) {
            brightness_.Set(static_cast<int>(SendMessageW(combo, CB_GETITEMDATA, row, 0)));
        }
        return true;
    }
    case IDC_SETTINGS_REFRESH:
        brightness_.ShowCurrent();
        return true;
    case IDC_SETTINGS_PAIR:
        StartPairingFlow();
        return true;
    case IDC_SETTINGS_EDIT_ENV:
        OpenSettingsFile();
        return true;
    case IDC_SETTINGS_RECORD_UP:
    case IDC_SETTINGS_RECORD_DOWN:
        ConfigureShortcut(control == IDC_SETTINGS_RECORD_UP ? BrightnessKey::Up : BrightnessKey::Down);
        UpdateKeyboardControls();
        return true;
    case IDC_SETTINGS_USE_BRIGHTNESS_KEYS:
        ToggleBrightnessKeys();
        UpdateKeyboardControls();
        return true;
    case IDC_SETTINGS_AUTOSTART: {
        const bool requested = IsDlgButtonChecked(page, control) == BST_CHECKED;
        if (!autostart::SetEnabled(requested)) {
            CheckDlgButton(page, control, autostart::IsEnabled() ? BST_CHECKED : BST_UNCHECKED);
            ShowMessageBox(L"Could not update the Windows startup setting.", MB_OK | MB_ICONWARNING);
        }
        return true;
    }
    case IDC_SETTINGS_GITHUB:
        OpenAboutLink(kGitHubUrl);
        return true;
    case IDC_SETTINGS_COFFEE:
        OpenAboutLink(kBuyMeACoffeeUrl);
        return true;
    case IDC_SETTINGS_KOFI:
        OpenAboutLink(kKoFiUrl);
        return true;
    default:
        return false;
    }
}
