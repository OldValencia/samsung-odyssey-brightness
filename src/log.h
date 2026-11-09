#pragma once

#include <filesystem>

// A tiny diagnostic log. Messages always go to OutputDebugString;
// they are also written to a file when DEBUG_LOG=1 is set in .env.
namespace logging {

// Pass an empty path to stop writing to a file.
void SetLogFile(const std::filesystem::path& file);

// printf-style formatting, e.g. logging::Write(L"Brightness %d", value).
void Write(const wchar_t* format, ...);

}
