// Tray icon, menu, settings and pairing parts of App.

#include "app.h"

#include "app_constants.h"
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
    HandleTrayCommand(ShowTrayMenu(window_, brightness_.StatusText()));
}

void App::HandleTrayCommand(TrayCommand command)
{
    switch (command) {
    case TrayCommand::None:
        break;
    case TrayCommand::ShowCurrent:
        brightness_.ShowCurrent();
        break;
    case TrayCommand::Increase:
        brightness_.Step(+1);
        break;
    case TrayCommand::Decrease:
        brightness_.Step(-1);
        break;
    case TrayCommand::Settings:
        ShowSettings();
        break;
    case TrayCommand::Exit:
        DestroyWindow(window_);
        break;
    }
}

void App::ChangeOsdTheme(OsdTheme theme)
{
    if (theme != osd_.Theme() && !SaveOsdTheme(theme)) {
        ShowMessageBox(L"The OSD theme could not be saved.", MB_OK | MB_ICONWARNING);
        return;
    }

    osd_.SetTheme(theme);
    brightness_.Preview();
}

void App::ConfigureShortcut(BrightnessKey direction)
{
    auto shortcut = shortcuts_.Get(direction);
    while (auto selection = EditShortcut(instance_, DialogOwner(), direction, shortcut, shortcuts_)) {
        shortcut = *selection;
        switch (shortcuts_.Set(direction, shortcut)) {
        case Shortcuts::SaveResult::Saved:
            return;
        case Shortcuts::SaveResult::Unavailable:
            ShowMessageBox(ShortcutLabel(shortcut) + L" overlaps another shortcut or is unavailable. Choose a different one.",
                           MB_OK | MB_ICONWARNING);
            break;
        case Shortcuts::SaveResult::WriteFailed:
            ShowMessageBox(L"The shortcut could not be saved.", MB_OK | MB_ICONWARNING);
            return;
        }
    }
}

void App::ToggleBrightnessKeys()
{
    if (shortcuts_.SetUseBrightnessKeys(!shortcuts_.UseBrightnessKeys())) {
        StopRepeat();
    } else {
        ShowMessageBox(L"The keyboard setting could not be saved.", MB_OK | MB_ICONWARNING);
    }
}

void App::OpenAboutLink(const wchar_t* url)
{
    auto result = ShellExecuteW(DialogOwner(), L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        ShowMessageBox(std::wstring(L"Could not open this link in your browser.\n\n") + url,
                       MB_OK | MB_ICONWARNING);
    }
}

void App::UpdateTooltip()
{
    tray_.SetTooltip(std::wstring(kAppName) + L"\n" + brightness_.StatusText());
    UpdateMonitorStatus();
}

void App::LoadSettings(bool showConnectionError)
{
    auto envFile = paths::EnvFile();
    config::CreateIfMissing(envFile);
    config_ = config::Load(envFile);

    logging::SetLogFile(config_.debugLog ? paths::LogFile() : std::filesystem::path());
    logging::Write(L"Settings loaded: monitor %ls:%d, step %d, max %d, nonlinear %d, smooth %d",
                   config_.monitorIp.c_str(), config_.monitorPort, config_.step, config_.maxBrightness,
                   config_.nonlinearSteps, config_.smoothTransitions);

    brightness_.Configure(config_, window_, showConnectionError);
    UpdateBrightnessPresets();
    UpdateTooltip();
}

// Guides the user through the first setup: the monitor address first, then pairing.
void App::CheckSettings()
{
    if (config_.monitorIp.empty()) {
        ShowSettings();
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
    ShellExecuteW(DialogOwner(), L"open", L"notepad.exe", argument.c_str(), nullptr, SW_SHOWNORMAL);
}

void App::StartPairingFlow()
{
    if (pairingInProgress_) {
        return;
    }

    if (config_.monitorIp.empty()) {
        CheckSettings();
        ShowMessageBox(L"Save the monitor address in Settings before pairing.", MB_OK | MB_ICONINFORMATION);
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

    auto saved = config::SaveAccessToken(paths::EnvFile(), result->monitorIp, result->monitorPort, result->text);
    if (saved == config::SaveTokenResult::EndpointChanged) {
        ShowMessageBox(L"The monitor address changed during pairing. Reload settings and pair again.",
                       MB_OK | MB_ICONWARNING);
        return;
    }

    if (saved == config::SaveTokenResult::WriteFailed) {
        ShowMessageBox(L"Pairing worked, but the token could not be saved to .env.", MB_OK | MB_ICONWARNING);
        return;
    }

    LoadSettings();
    SyncSettingsToken();
    ShowMessageBox(L"Paired! The access token was saved to .env.", MB_OK | MB_ICONINFORMATION);
}

int App::ShowMessageBox(const std::wstring& text, UINT flags)
{
    return MessageBoxW(DialogOwner(), text.c_str(), kAppName, flags | MB_SETFOREGROUND);
}
