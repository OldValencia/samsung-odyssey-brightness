#include "text.h"

#include <windows.h>

std::string ToUtf8(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }

    auto length = static_cast<int>(text.size());
    auto size = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);

    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring FromUtf8(std::string_view text)
{
    if (text.empty()) {
        return {};
    }

    auto length = static_cast<int>(text.size());
    auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);

    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), length, result.data(), size);
    return result;
}

std::wstring Trim(std::wstring_view text)
{
    constexpr wchar_t kWhitespace[] = L" \t\r\n";

    auto first = text.find_first_not_of(kWhitespace);
    if (first == std::wstring_view::npos) {
        return {};
    }

    auto last = text.find_last_not_of(kWhitespace);
    return std::wstring(text.substr(first, last - first + 1));
}
