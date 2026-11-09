// Tray icon, menu, settings and pairing parts of App.

#include "app.h"

#include "app_constants.h"
#include "autostart.h"
#include "log.h"
#include "pairing.h"
#include "paths.h"

#include <shellapi.h>

#include <memory>

void App::OnTrayEvent(UINT event)
{
    switch (event) {
    case NIN_SELECT:
    case NIN_KEYSELECT:
        brightness_.ShowCurrent();
        break;
    case WM_CONTEXTMENU:
        ShowMenu();
        break;
    default:
        break;
    }
}

void App::ShowMenu()
{
    TrayMenuState state;
    state.status = brightness_.StatusText();
    state.currentValue = brightness_.Value();
    state.presets = brightness_.Presets();
    state.autostartEnabled = autostart::IsEnabled();

    if (brightness_.Value() < 0) {
        state.error = brightness_.LastError();
    }

    HandleMenuChoice(ShowTrayMenu(window_, state));
}

void App::HandleMenuChoice(const MenuChoice& choice)
{
    switch (choice.command) {
    case MenuCommand::None:
        break;
    case MenuCommand::Increase:
        brightness_.Step(+1);
        break;
    case MenuCommand::Decrease:
        brightness_.Step(-1);
        break;
    case MenuCommand::SetValue:
        brightness_.Set(choice.value);
        break;
    case MenuCommand::Refresh:
        brightness_.Refresh(true);
        break;
    case MenuCommand::ToggleAutostart:
        autostart::SetEnabled(!autostart::IsEnabled());
        break;
    case MenuCommand::EditSettings:
        OpenSettingsFile();
        break;
    case MenuCommand::ReloadSettings:
        LoadSettings();
        CheckSettings();
        break;
    case MenuCommand::Pair:
        StartPairingFlow();
        break;
    case MenuCommand::Exit:
        DestroyWindow(window_);
        break;
    }
}

void App::UpdateTooltip()
{
    tray_.SetTooltip(std::wstring(kAppName) + L"\n" + brightness_.StatusText());
}

void App::LoadSettings()
{
    auto envFile = paths::EnvFile();
    config::CreateIfMissing(envFile);
    config_ = config::Load(envFile);

    logging::SetLogFile(config_.debugLog ? paths::LogFile() : std::filesystem::path());
    logging::Write(L"Settings loaded: monitor %ls:%d, step %d, max %d, nonlinear %d, smooth %d",
                   config_.monitorIp.c_str(), config_.monitorPort, config_.step, config_.maxBrightness,
                   config_.nonlinearSteps, config_.smoothTransitions);

    brightness_.Configure(config_, window_);
    brightness_.Refresh(false);
    UpdateTooltip();
}

// Guides the user through the first setup: the IP address first, then pairing.
void App::CheckSettings()
{
    if (config_.monitorIp.empty()) {
        ShowMessageBox(L"Please enter the IP address of your monitor (SAMSUNG_MONITOR_IP) in the settings file.\n\n"
                       L"The file opens now. After saving it, choose \"Reload settings\" in the tray menu.",
                       MB_OK | MB_ICONINFORMATION);
        OpenSettingsFile();
        return;
    }

    if (config_.accessToken.empty()) {
        auto answer = ShowMessageBox(L"The app is not paired with the monitor yet. Pair now?",
                                     MB_YESNO | MB_ICONQUESTION);
        if (answer == IDYES) {
            StartPairingFlow();
        }
    }
}

void App::OpenSettingsFile()
{
    auto envFile = paths::EnvFile();
    config::CreateIfMissing(envFile);

    auto argument = L"\"" + envFile.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"notepad.exe", argument.c_str(), nullptr, SW_SHOWNORMAL);
}

void App::StartPairingFlow()
{
    if (pairingInProgress_) {
        return;
    }

    if (config_.monitorIp.empty()) {
        CheckSettings();
        return;
    }

    auto answer = ShowMessageBox(L"Make sure the monitor is on and IP Remote is enabled in its network settings.\n\n"
                                 L"After you click OK, the monitor asks for permission. "
                                 L"Accept it on the monitor within 60 seconds.",
                                 MB_OKCANCEL | MB_ICONINFORMATION);
    if (answer != IDOK) {
        return;
    }

    pairingInProgress_ = true;
    StartPairing(window_, config_.monitorIp, config_.monitorPort);
}

void App::OnPairingResult(PairingResult* rawResult)
{
    std::unique_ptr<PairingResult> result(rawResult);
    pairingInProgress_ = false;

    if (!result->success) {
        ShowMessageBox(L"Pairing failed.\n\n" + result->text, MB_OK | MB_ICONWARNING);
        return;
    }

    if (!config::SaveValue(paths::EnvFile(), config::kAccessTokenKey, result->text)) {
        ShowMessageBox(L"Pairing worked, but the token could not be saved to .env.", MB_OK | MB_ICONWARNING);
        return;
    }

    LoadSettings();
    ShowMessageBox(L"Paired! The access token was saved to .env.", MB_OK | MB_ICONINFORMATION);
}

int App::ShowMessageBox(const std::wstring& text, UINT flags)
{
    return MessageBoxW(window_, text.c_str(), kAppName, flags | MB_SETFOREGROUND);
}
