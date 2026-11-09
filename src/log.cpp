#include "log.h"

#include "text.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace logging {

namespace {

// The worker thread logs too, so access to the file is serialized.
std::mutex logMutex;
std::filesystem::path logFile;

void AppendToFile(const std::wstring& line)
{
    std::scoped_lock lock(logMutex);

    if (logFile.empty()) {
        return;
    }

    FILE* file = nullptr;
    if (_wfopen_s(&file, logFile.c_str(), L"ab") != 0 || file == nullptr) {
        return;
    }

    auto utf8 = ToUtf8(line);
    fwrite(utf8.data(), 1, utf8.size(), file);
    fclose(file);
}

}

void SetLogFile(const std::filesystem::path& file)
{
    std::scoped_lock lock(logMutex);
    logFile = file;
}

void Write(const wchar_t* format, ...)
{
    wchar_t message[1024];

    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message, _TRUNCATE, format, arguments);
    va_end(arguments);

    SYSTEMTIME now;
    GetLocalTime(&now);

    wchar_t line[1100];
    swprintf_s(line, L"%02u:%02u:%02u.%03u  %ls\r\n",
               now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, message);

    OutputDebugStringW(line);
    AppendToFile(line);
}

}
