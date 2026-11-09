#pragma once

#include <filesystem>

// All files of the app live next to the executable, so it stays portable.
namespace paths {

std::filesystem::path ExecutablePath();
std::filesystem::path EnvFile();
std::filesystem::path LogFile();

}
