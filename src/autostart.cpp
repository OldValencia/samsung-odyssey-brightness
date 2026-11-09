#include "autostart.h"

#include "paths.h"

#include <windows.h>

#include <cwchar>
#include <string>

namespace autostart {

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"SamsungBrightness";

std::wstring StartupCommand()
{
    return L"\"" + paths::ExecutablePath().wstring() + L"\"";
}

}

bool IsEnabled()
{
    wchar_t command[2048] = {};
    DWORD size = sizeof(command);

    auto status = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ, nullptr, command, &size);
    return status == ERROR_SUCCESS && _wcsicmp(command, StartupCommand().c_str()) == 0;
}

bool SetEnabled(bool enabled)
{
    if (!enabled) {
        auto status = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }

    auto command = StartupCommand();
    auto bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    auto status = RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName, REG_SZ, command.c_str(), bytes);
    return status == ERROR_SUCCESS;
}

}
