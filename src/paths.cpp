#include "paths.h"

#include <windows.h>

#include <string>

namespace paths {

std::filesystem::path ExecutablePath()
{
    std::wstring buffer(MAX_PATH, L'\0');

    while (true) {
        auto capacity = static_cast<DWORD>(buffer.size());
        auto length = GetModuleFileNameW(nullptr, buffer.data(), capacity);

        if (length < capacity) {
            buffer.resize(length);
            return buffer;
        }

        // The path was truncated: try again with a bigger buffer.
        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path EnvFile()
{
    return ExecutablePath().parent_path() / L".env";
}

std::filesystem::path LogFile()
{
    return ExecutablePath().parent_path() / L"samsung-brightness.log";
}

}
