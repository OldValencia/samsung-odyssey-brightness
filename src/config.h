#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Settings from the .env file next to the executable. See .env.example for descriptions.
struct Config {
    std::wstring monitorIp;
    int monitorPort = 1516;
    std::wstring accessToken;
    int step = 5;
    int maxBrightness = 50;
    bool nonlinearSteps = true;
    bool smoothTransitions = true;
    bool debugLog = false;
};

namespace config {

inline constexpr wchar_t kAccessTokenKey[] = L"SAMSUNG_ACCESS_TOKEN";

// Writes a commented template when the file does not exist yet. Returns true if it was created.
bool CreateIfMissing(const std::filesystem::path& envFile);

// Missing or invalid values fall back to the defaults in Config.
Config Load(const std::filesystem::path& envFile);

// Replaces the KEY=value line (or appends it) and keeps the rest of the file untouched.
bool SaveValue(const std::filesystem::path& envFile, std::wstring_view key, std::wstring_view value);

}
