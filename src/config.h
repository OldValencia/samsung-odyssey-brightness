#pragma once

#include <filesystem>
#include <optional>
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

// Writes a commented template when the file does not exist yet. Returns true if it was created.
bool CreateIfMissing(const std::filesystem::path& envFile);

// Missing or invalid values fall back to the defaults in Config.
Config Load(const std::filesystem::path& envFile);
std::optional<Config> TryLoad(const std::filesystem::path& envFile);

enum class SaveResult { Saved, ChangedOnDisk, WriteFailed };

// Saves the fields edited in the dialog against the latest file contents.
SaveResult Save(const std::filesystem::path& envFile, const Config& original, const Config& edited);

enum class SaveTokenResult { Saved, EndpointChanged, WriteFailed };

// Checks the endpoint in the same settings snapshot used to save the token.
SaveTokenResult SaveAccessToken(const std::filesystem::path& envFile, std::wstring_view monitorIp,
                                int monitorPort, std::wstring_view token);

}
