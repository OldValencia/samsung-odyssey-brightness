#include "app.h"

#include "app_constants.h"
#include "autostart.h"
#include "config.h"
#include "paths.h"
#include "resource.h"
#include "text.h"
#include "version.h"

#include <commctrl.h>

#include <algorithm>
#include <cstdlib>
#include <cwctype>

namespace {

constexpr int kSizeStops[]{75, 100, 125, 150};

struct SettingsTab {
    const wchar_t* label;
    int resource;
};

constexpr SettingsTab kTabs[]{
    {L"Monitor", IDD_SETTINGS_MONITOR},
    {L"Appearance", IDD_SETTINGS_APPEARANCE},
    {L"Keyboard", IDD_SETTINGS_KEYBOARD},
    {L"Application", IDD_SETTINGS_APPLICATION},
    {L"About", IDD_SETTINGS_ABOUT},
};

struct Toggle {
    int control;
    bool Config::* member;
};

constexpr Toggle kToggles[]{
    {IDC_SETTINGS_CURVE, &Config::nonlinearSteps},
    {IDC_SETTINGS_TRANSITIONS, &Config::smoothTransitions},
    {IDC_SETTINGS_LOG, &Config::debugLog},
};

void Populate(HWND dialog, const Config& config)
{
    SetDlgItemTextW(dialog, IDC_SETTINGS_HOST, config.monitorIp.c_str());
    SetDlgItemInt(dialog, IDC_SETTINGS_PORT, config.monitorPort, FALSE);
    SetDlgItemTextW(dialog, IDC_SETTINGS_TOKEN, config.accessToken.c_str());
    SetDlgItemInt(dialog, IDC_SETTINGS_STEP, config.step, FALSE);
    SetDlgItemInt(dialog, IDC_SETTINGS_MAX, config.maxBrightness, FALSE);
    for (const auto& [control, member] : kToggles) {
        CheckDlgButton(dialog, control, config.*member ? BST_CHECKED : BST_UNCHECKED);
    }
}

std::wstring FieldText(HWND dialog, int control)
{
    auto field = GetDlgItem(dialog, control);
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(field)) + 1, L'\0');
    text.resize(GetWindowTextW(field, text.data(), static_cast<int>(text.size())));
    return text;
}

int ReadNumber(HWND dialog, int control, int maximum)
{
    auto text = Trim(FieldText(dialog, control));
    if (text.empty() || !std::ranges::all_of(text, [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
        return 0;
    }
    int value = 0;
    for (const auto digit : text) {
        value = value * 10 + (digit - L'0');
        if (value > maximum) {
            return 0;
        }
    }
    return value;
}

bool InvalidField(HWND dialog, int control, const wchar_t* message)
{
    SetDlgItemTextW(dialog, IDC_SETTINGS_STATUS, message);
    auto field = GetDlgItem(dialog, control);
    SetFocus(field);
    SendMessageW(field, EM_SETSEL, 0, -1);
    return false;
}

bool ReadForm(HWND dialog, Config& edited)
{
    edited.monitorIp = Trim(FieldText(dialog, IDC_SETTINGS_HOST));
    if (edited.monitorIp.empty() || edited.monitorIp.find_first_of(L"/\\?#=@\"'") != std::wstring::npos
        || std::ranges::any_of(edited.monitorIp, [](wchar_t c) { return std::iswspace(c) || std::iswcntrl(c); })) {
        return InvalidField(dialog, IDC_SETTINGS_HOST, L"Enter the monitor's IP address or hostname, such as 192.168.1.50.");
    }
    if (std::ranges::count(edited.monitorIp, L':') == 1) {
        return InvalidField(dialog, IDC_SETTINGS_HOST, L"Enter the monitor address and port in separate fields.");
    }
    edited.monitorPort = ReadNumber(dialog, IDC_SETTINGS_PORT, 65535);
    if (!edited.monitorPort) {
        return InvalidField(dialog, IDC_SETTINGS_PORT, L"Enter a port from 1 to 65535. The default is 1516.");
    }
    edited.accessToken = FieldText(dialog, IDC_SETTINGS_TOKEN);
    if (edited.accessToken.find_first_of(L"\r\n") != std::wstring::npos) {
        return InvalidField(dialog, IDC_SETTINGS_TOKEN, L"Enter the access token on one line.");
    }
    edited.maxBrightness = ReadNumber(dialog, IDC_SETTINGS_MAX, 100);
    if (!edited.maxBrightness) {
        return InvalidField(dialog, IDC_SETTINGS_MAX, L"Enter a maximum brightness from 1 to 100.");
    }
    edited.step = ReadNumber(dialog, IDC_SETTINGS_STEP, edited.maxBrightness);
    if (!edited.step) {
        return InvalidField(dialog, IDC_SETTINGS_STEP, L"Enter a step from 1 up to the maximum brightness.");
    }
    for (const auto& [control, member] : kToggles) {
        edited.*member = IsDlgButtonChecked(dialog, control) == BST_CHECKED;
    }
    return true;
}

bool SaveForm(HWND dialog, const Config& original)
{
    Config edited;
    if (!ReadForm(dialog, edited)) {
        return false;
    }
    switch (config::Save(paths::EnvFile(), original, edited)) {
    case config::SaveResult::Saved:
        return true;
    case config::SaveResult::ChangedOnDisk:
        SetDlgItemTextW(dialog, IDC_SETTINGS_STATUS, L"The file changed while this window was open. Reopen Settings to edit the latest values.");
        break;
    case config::SaveResult::WriteFailed:
        SetDlgItemTextW(dialog, IDC_SETTINGS_STATUS, L"Could not save .env. Check that the file is writable and not in use.");
        break;
    }
    return false;
}

}

INT_PTR CALLBACK App::SettingsDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INITDIALOG) {
        auto& app = *reinterpret_cast<App*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        app.settingsWindow_ = dialog;
        app.InitializeSettingsPages();
        return TRUE;
    }

    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (!app) {
        return FALSE;
    }
    if (message == WM_NOTIFY && wParam == IDC_SETTINGS_TABS
        && reinterpret_cast<const NMHDR*>(lParam)->code == TCN_SELCHANGE) {
        app->SelectSettingsPage(TabCtrl_GetCurSel(GetDlgItem(dialog, IDC_SETTINGS_TABS)));
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL)) {
        if (app->settingsPages_[Appearance] && app->osd_.SizePercent() != app->savedOsdSize_) {
            app->ChangeOsdSize(TB_ENDTRACK);
        }
        DestroyWindow(dialog);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK && app->settingsTab_ == Monitor) {
        SendMessageW(app->settingsPages_[Monitor], WM_COMMAND, IDOK, 0);
        return TRUE;
    }
    if (message == WM_DESTROY) {
        app->settingsWindow_ = nullptr;
        app->settingsPages_.fill(nullptr);
        app->settingsSnapshot_ = {};
    }
    return FALSE;
}

INT_PTR CALLBACK App::SettingsPageProc(HWND page, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(GetParent(page), DWLP_USER));
    if (!app) {
        return FALSE;
    }
    if (message == WM_HSCROLL && page == app->settingsPages_[Appearance]
        && reinterpret_cast<HWND>(lParam) == GetDlgItem(page, IDC_OSD_SIZE_SLIDER)) {
        app->ChangeOsdSize(LOWORD(wParam));
        return TRUE;
    }
    if (message != WM_COMMAND) {
        return FALSE;
    }

    switch (LOWORD(wParam)) {
    case IDOK:
        if (page == app->settingsPages_[Monitor] && SaveForm(page, app->settingsSnapshot_)) {
            app->LoadSettings(/*showConnectionError=*/true);
            app->settingsSnapshot_ = app->config_;
            Populate(page, app->settingsSnapshot_);
            SetDlgItemTextW(page, IDC_SETTINGS_STATUS, L"Settings saved and applied.");
            app->CheckSettings();
        }
        return TRUE;
    case IDC_SETTINGS_RELOAD:
        app->LoadSettings();
        app->settingsSnapshot_ = app->config_;
        Populate(page, app->settingsSnapshot_);
        SetDlgItemTextW(page, IDC_SETTINGS_STATUS, L"Settings reloaded from .env.");
        app->CheckSettings();
        return TRUE;
    default:
        return app->HandleSettingsControl(page, LOWORD(wParam), HIWORD(wParam));
    }
}

void App::ShowSettings()
{
    if (settingsWindow_) {
        SetForegroundWindow(settingsWindow_);
        return;
    }
    const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_BAR_CLASSES};
    if (!InitCommonControlsEx(&controls)) {
        ShowMessageBox(L"Could not initialize the settings controls.", MB_OK | MB_ICONWARNING);
        return;
    }
    const auto envFile = paths::EnvFile();
    config::CreateIfMissing(envFile);
    settingsSnapshot_ = config::TryLoad(envFile).value_or(config_);
    const auto dialog = CreateDialogParamW(instance_, MAKEINTRESOURCEW(IDD_SETTINGS), window_, SettingsDialogProc,
                                           reinterpret_cast<LPARAM>(this));
    if (!dialog || std::ranges::find(settingsPages_, nullptr) != settingsPages_.end()) {
        if (dialog) {
            DestroyWindow(dialog);
        }
        ShowMessageBox(L"Could not open the settings window.", MB_OK | MB_ICONWARNING);
        return;
    }
    ShowWindow(dialog, SW_SHOW);
    SetForegroundWindow(dialog);
}

void App::InitializeSettingsPages()
{
    static_assert(std::size(kTabs) == PageCount);
    const auto tabs = GetDlgItem(settingsWindow_, IDC_SETTINGS_TABS);
    TabCtrl_SetPadding(tabs, 12, 5);
    for (int i = 0; i < PageCount; ++i) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(kTabs[i].label);
        if (SendMessageW(tabs, TCM_INSERTITEMW, i, reinterpret_cast<LPARAM>(&item)) == -1) {
            return;
        }
        settingsPages_[i] = CreateDialogParamW(instance_, MAKEINTRESOURCEW(kTabs[i].resource), settingsWindow_,
                                               SettingsPageProc, 0);
        if (!settingsPages_[i]) {
            return;
        }
    }

    RECT area{};
    GetClientRect(tabs, &area);
    TabCtrl_AdjustRect(tabs, FALSE, &area);
    MapWindowPoints(tabs, settingsWindow_, reinterpret_cast<POINT*>(&area), 2);
    for (const auto page : settingsPages_) {
        SetWindowPos(page, nullptr, area.left, area.top, area.right - area.left, area.bottom - area.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    Populate(settingsPages_[Monitor], settingsSnapshot_);
    UpdateBrightnessPresets();
    UpdateMonitorStatus();

    const auto appearance = settingsPages_[Appearance];
    const auto list = GetDlgItem(appearance, IDC_SETTINGS_THEME);
    for (const auto& option : kOsdThemes) {
        const auto row = SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option.name));
        if (row >= 0) {
            SendMessageW(list, LB_SETITEMDATA, row, static_cast<LPARAM>(option.theme));
        }
    }
    const auto slider = GetDlgItem(appearance, IDC_OSD_SIZE_SLIDER);
    SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(kMinOsdSize, kMaxOsdSize));
    SendMessageW(slider, TBM_SETPAGESIZE, 0, 5);
    for (const auto stop : kSizeStops) {
        if (stop > kMinOsdSize && stop < kMaxOsdSize) {
            SendMessageW(slider, TBM_SETTIC, 0, stop);
        }
    }
    savedOsdSize_ = osd_.SizePercent();
    SendMessageW(slider, TBM_SETPOS, TRUE, savedOsdSize_);
    UpdateAppearanceControls();
    UpdateKeyboardControls();
    CheckDlgButton(settingsPages_[Application], IDC_SETTINGS_AUTOSTART,
                   autostart::IsEnabled() ? BST_CHECKED : BST_UNCHECKED);
    const auto version = std::wstring(kAppName) + L" " APP_VERSION_TEXT;
    SetDlgItemTextW(settingsPages_[About], IDC_SETTINGS_VERSION, version.c_str());
    SelectSettingsPage(settingsTab_);
}

void App::SelectSettingsPage(int index)
{
    if (index < 0 || index >= PageCount || !settingsPages_[index]) {
        return;
    }
    if (settingsTab_ == Appearance && index != Appearance && osd_.SizePercent() != savedOsdSize_) {
        ChangeOsdSize(TB_ENDTRACK);
    }
    settingsTab_ = index;
    TabCtrl_SetCurSel(GetDlgItem(settingsWindow_, IDC_SETTINGS_TABS), index);
    for (int i = 0; i < PageCount; ++i) {
        if (i != index) {
            ShowWindow(settingsPages_[i], SW_HIDE);
        }
    }
    SetWindowPos(settingsPages_[index], HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void App::SyncSettingsToken()
{
    const auto monitor = settingsPages_[Monitor];
    if (monitor && FieldText(monitor, IDC_SETTINGS_TOKEN) == settingsSnapshot_.accessToken) {
        settingsSnapshot_.accessToken = config_.accessToken;
        SetDlgItemTextW(monitor, IDC_SETTINGS_TOKEN, config_.accessToken.c_str());
    }
}

void App::UpdateBrightnessPresets()
{
    const auto page = settingsPages_[Monitor];
    if (!page) {
        return;
    }
    const auto combo = GetDlgItem(page, IDC_SETTINGS_PRESETS);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto value : brightness_.Presets()) {
        const auto text = std::to_wstring(value);
        const auto row = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        if (row >= 0) {
            SendMessageW(combo, CB_SETITEMDATA, row, value);
        }
    }
    EnableWindow(GetDlgItem(page, IDC_SETTINGS_SET_LEVEL), FALSE);
}

void App::UpdateMonitorStatus()
{
    const auto page = settingsPages_[Monitor];
    if (!page) {
        return;
    }
    auto status = brightness_.StatusText();
    const auto error = brightness_.LastError();
    if (!error.empty()) {
        status = error;
    }
    SetDlgItemTextW(page, IDC_SETTINGS_LEVEL, status.c_str());
}
