#include "app.h"
#include "app_constants.h"
#include "command_line.h"

#include <windows.h>

namespace {

constexpr wchar_t kSingleInstanceMutex[] = L"Local\\SamsungBrightness.SingleInstance";

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    auto command = ParseCommandLine();
    if (!command) {
        MessageBoxW(nullptr, UsageText().c_str(), kAppName, MB_OK | MB_ICONINFORMATION);
        return 1;
    }

    // Only one instance runs in the tray. Later launches (e.g. "SamsungBrightness.exe up" from a
    // keyboard tool) pass their command to it and exit. The mutex stays open until the process ends.
    CreateMutexW(nullptr, FALSE, kSingleInstanceMutex);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return SendToRunningInstance(*command) ? 0 : 1;
    }

    if (command->kind == CommandKind::Exit) {
        return 0;  // nothing is running
    }

    App app(instance);
    if (!app.Initialize()) {
        return 1;
    }

    app.Execute(*command);
    return app.Run();
}
