#pragma once

#include <string>
#include <string_view>

// Conversions between UTF-8 (files, network) and UTF-16 (Windows API).
std::string ToUtf8(std::wstring_view text);
std::wstring FromUtf8(std::string_view text);

// Removes spaces, tabs and line breaks from both ends.
std::wstring Trim(std::wstring_view text);
