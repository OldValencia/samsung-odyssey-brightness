#include "config.h"

#include "text.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <fstream>
#include <optional>
#include <utility>
#include <vector>

namespace config {

namespace {

constexpr wchar_t kMonitorIpKey[] = L"SAMSUNG_MONITOR_IP";
constexpr wchar_t kMonitorPortKey[] = L"SAMSUNG_MONITOR_PORT";
constexpr wchar_t kAccessTokenKey[] = L"SAMSUNG_ACCESS_TOKEN";
constexpr wchar_t kStepKey[] = L"BRIGHTNESS_STEP";
constexpr wchar_t kMaxBrightnessKey[] = L"BRIGHTNESS_MAX";
constexpr wchar_t kNonlinearStepsKey[] = L"NONLINEAR_STEPS";
constexpr wchar_t kSmoothTransitionsKey[] = L"SMOOTH_TRANSITIONS";
constexpr wchar_t kDebugLogKey[] = L"DEBUG_LOG";

constexpr char kTemplate[] =
    "# Samsung Brightness settings. Lines starting with # are comments.\n"
    "\n"
    "# IP address of the monitor in your local network.\n"
    "# Tip: reserve this address for the monitor in your router's DHCP settings.\n"
    "SAMSUNG_MONITOR_IP=\n"
    "\n"
    "# Access token of the monitor. Use Settings > Monitor > Pair with monitor... to fill it in.\n"
    "# Keep this file private: the token allows controlling the monitor.\n"
    "SAMSUNG_ACCESS_TOKEN=\n"
    "\n"
    "# Port of the Samsung IP Control server.\n"
    "SAMSUNG_MONITOR_PORT=1516\n"
    "\n"
    "# How much one key press changes the brightness.\n"
    "# With NONLINEAR_STEPS=1 it sets the number of steps instead: BRIGHTNESS_MAX / BRIGHTNESS_STEP.\n"
    "BRIGHTNESS_STEP=5\n"
    "\n"
    "# The highest brightness value the monitor accepts (50 on the Odyssey OLED G8).\n"
    "BRIGHTNESS_MAX=50\n"
    "\n"
    "# 1 = small steps at low brightness, where the eye notices changes most, larger steps near the maximum.\n"
    "# 0 = every step changes the brightness by BRIGHTNESS_STEP.\n"
    "NONLINEAR_STEPS=1\n"
    "\n"
    "# 1 = fade smoothly to the new brightness, 0 = change it at once.\n"
    "SMOOTH_TRANSITIONS=1\n"
    "\n"
    "# Set to 1 to write samsung-brightness.log next to the exe.\n"
    "DEBUG_LOG=0\n";

struct Entry {
    std::wstring key;
    std::wstring value;
};

void RemoveUtf8Bom(std::string& line)
{
    constexpr std::string_view kBom = "\xEF\xBB\xBF";

    if (line.starts_with(kBom)) {
        line.erase(0, kBom.size());
    }
}

std::optional<std::vector<std::string>> ReadLines(const std::filesystem::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::vector<std::string> lines;
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    if (!stream.eof() || stream.bad()) {
        return std::nullopt;
    }

    if (!lines.empty()) {
        RemoveUtf8Bom(lines.front());
    }
    return lines;
}

bool WriteLines(const std::filesystem::path& file, const std::vector<std::string>& lines)
{
    std::string bytes;
    for (const auto& line : lines) {
        bytes += line;
        bytes += '\n';
    }
    if (bytes.size() > MAXDWORD) {
        return false;
    }

    auto temporary = file;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const auto size = static_cast<DWORD>(bytes.size());
    const bool ready = WriteFile(handle, bytes.data(), size, &written, nullptr)
        && written == size && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (ready && ReplaceFileW(file.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr)) {
        return true;
    }

    DeleteFileW(temporary.c_str());
    return false;
}

std::wstring StripQuotes(const std::wstring& value)
{
    bool quoted = value.size() >= 2
        && (value.front() == L'"' || value.front() == L'\'')
        && value.back() == value.front();

    return quoted ? value.substr(1, value.size() - 2) : value;
}

// Parses a "KEY=value" line. Comments and empty lines return nothing.
std::optional<Entry> ParseEntry(const std::string& line)
{
    auto text = Trim(FromUtf8(line));
    if (text.empty() || text.front() == L'#') {
        return std::nullopt;
    }

    auto separator = text.find(L'=');
    if (separator == std::wstring::npos) {
        return std::nullopt;
    }

    auto key = Trim(text.substr(0, separator));
    auto value = StripQuotes(Trim(text.substr(separator + 1)));
    return Entry{key, value};
}

int ParseInt(const std::wstring& text, int fallback, int minValue, int maxValue)
{
    wchar_t* end = nullptr;
    auto value = std::wcstol(text.c_str(), &end, 10);

    bool isNumber = end != text.c_str() && *end == L'\0';
    if (!isNumber) {
        return fallback;
    }
    return std::clamp(static_cast<int>(value), minValue, maxValue);
}

bool ParseBool(const std::wstring& text)
{
    return text == L"1" || _wcsicmp(text.c_str(), L"true") == 0 || _wcsicmp(text.c_str(), L"yes") == 0;
}

void Apply(Config& config, const Entry& entry)
{
    if (entry.key == kMonitorIpKey) {
        config.monitorIp = entry.value;
    } else if (entry.key == kAccessTokenKey) {
        config.accessToken = entry.value;
    } else if (entry.key == kMonitorPortKey) {
        config.monitorPort = ParseInt(entry.value, config.monitorPort, 1, 65535);
    } else if (entry.key == kStepKey) {
        config.step = ParseInt(entry.value, config.step, 1, 100);
    } else if (entry.key == kMaxBrightnessKey) {
        config.maxBrightness = ParseInt(entry.value, config.maxBrightness, 1, 100);
    } else if (entry.key == kNonlinearStepsKey) {
        config.nonlinearSteps = ParseBool(entry.value);
    } else if (entry.key == kSmoothTransitionsKey) {
        config.smoothTransitions = ParseBool(entry.value);
    } else if (entry.key == kDebugLogKey) {
        config.debugLog = ParseBool(entry.value);
    }
}

Config ParseConfig(const std::vector<std::string>& lines)
{
    Config config;

    for (const auto& line : lines) {
        auto entry = ParseEntry(line);
        if (entry) {
            Apply(config, *entry);
        }
    }

    config.step = std::min(config.step, config.maxBrightness);
    return config;
}

std::wstring SettingText(std::wstring_view value)
{
    if (value.starts_with(L'"') || value.starts_with(L'\'') || Trim(std::wstring(value)) != value) {
        return L"\"" + std::wstring(value) + L"\"";
    }
    return std::wstring(value);
}

std::wstring SettingText(int value)
{
    return std::to_wstring(value);
}

void SetEntry(std::vector<std::string>& lines, std::wstring_view key, std::wstring_view value)
{
    auto replacement = ToUtf8(std::wstring(key) + L"=" + std::wstring(value));
    auto existing = std::find_if(lines.rbegin(), lines.rend(), [key](const std::string& line) {
        auto entry = ParseEntry(line);
        return entry && entry->key == key;
    });
    if (existing == lines.rend()) {
        lines.push_back(std::move(replacement));
    } else {
        *existing = std::move(replacement);
    }
}

}

bool CreateIfMissing(const std::filesystem::path& envFile)
{
    std::error_code error;
    if (std::filesystem::exists(envFile, error) || error) {
        return false;
    }

    std::ofstream stream(envFile, std::ios::binary);
    stream << kTemplate;
    return stream.good();
}

Config Load(const std::filesystem::path& envFile)
{
    return TryLoad(envFile).value_or(Config{});
}

std::optional<Config> TryLoad(const std::filesystem::path& envFile)
{
    auto lines = ReadLines(envFile);
    return lines ? std::optional(ParseConfig(*lines)) : std::nullopt;
}

SaveResult Save(const std::filesystem::path& envFile, const Config& original, const Config& edited)
{
    auto lines = ReadLines(envFile);
    if (!lines) {
        return SaveResult::WriteFailed;
    }
    const auto current = ParseConfig(*lines);
    if (edited.accessToken != original.accessToken
        && (current.monitorIp != original.monitorIp || current.monitorPort != original.monitorPort)) {
        return SaveResult::ChangedOnDisk;
    }
    const auto step = edited.step != original.step ? edited.step : current.step;
    const auto maximum = edited.maxBrightness != original.maxBrightness ? edited.maxBrightness : current.maxBrightness;
    if (step > maximum) {
        return SaveResult::ChangedOnDisk;
    }

    bool modified = false;
    const auto apply = [&](std::wstring_view key, auto member) {
        const auto& value = edited.*member;
        if (value == original.*member || value == current.*member) {
            return true;
        }
        if (current.*member != original.*member) {
            return false;
        }
        SetEntry(*lines, key, SettingText(value));
        modified = true;
        return true;
    };
    if (!apply(kMonitorIpKey, &Config::monitorIp) || !apply(kMonitorPortKey, &Config::monitorPort)
        || !apply(kAccessTokenKey, &Config::accessToken) || !apply(kStepKey, &Config::step)
        || !apply(kMaxBrightnessKey, &Config::maxBrightness) || !apply(kNonlinearStepsKey, &Config::nonlinearSteps)
        || !apply(kSmoothTransitionsKey, &Config::smoothTransitions) || !apply(kDebugLogKey, &Config::debugLog)) {
        return SaveResult::ChangedOnDisk;
    }

    return !modified || WriteLines(envFile, *lines) ? SaveResult::Saved : SaveResult::WriteFailed;
}

SaveTokenResult SaveAccessToken(const std::filesystem::path& envFile, std::wstring_view monitorIp,
                                int monitorPort, std::wstring_view token)
{
    auto lines = ReadLines(envFile);
    if (!lines) {
        return SaveTokenResult::WriteFailed;
    }
    auto config = ParseConfig(*lines);
    if (config.monitorIp != monitorIp || config.monitorPort != monitorPort) {
        return SaveTokenResult::EndpointChanged;
    }

    SetEntry(*lines, kAccessTokenKey, SettingText(token));
    return WriteLines(envFile, *lines) ? SaveTokenResult::Saved : SaveTokenResult::WriteFailed;
}

}
