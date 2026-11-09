#include "command_line.h"

#include "app_constants.h"

#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <vector>

namespace {

// Marks our WM_COPYDATA messages ("SBCM").
constexpr ULONG_PTR kCopyDataId = 0x5342434D;

// How long a new process waits for a just-started instance to create its window.
constexpr int kFindWindowAttempts = 20;
constexpr DWORD kFindWindowDelayMs = 100;
constexpr UINT kSendTimeoutMs = 2000;

struct CommandPayload {
    int32_t kind;
    int32_t value;
};

std::vector<std::wstring> ProgramArguments()
{
    auto count = 0;
    auto* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == nullptr) {
        return {};
    }

    // Skip the program path.
    std::vector<std::wstring> result(arguments + std::min(count, 1), arguments + count);
    LocalFree(arguments);
    return result;
}

std::wstring ToLower(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return std::towlower(c); });
    return text;
}

std::optional<int> ParseNonNegativeInt(const std::wstring& text)
{
    auto isDigit = [](wchar_t c) { return std::iswdigit(c) != 0; };

    if (text.empty() || text.size() > 3 || !std::all_of(text.begin(), text.end(), isDigit)) {
        return std::nullopt;
    }
    return std::stoi(text);
}

HWND FindRunningInstance()
{
    for (int attempt = 0; attempt < kFindWindowAttempts; ++attempt) {
        auto window = FindWindowW(kMainWindowClass, nullptr);
        if (window != nullptr) {
            return window;
        }
        Sleep(kFindWindowDelayMs);
    }
    return nullptr;
}

}

std::optional<Command> ParseCommandLine()
{
    auto arguments = ProgramArguments();
    if (arguments.empty()) {
        return Command{CommandKind::Start};
    }

    auto name = ToLower(arguments[0]);
    auto hasSingleArgument = arguments.size() == 1;

    if (name == L"up" && hasSingleArgument) {
        return Command{CommandKind::Up};
    }
    if (name == L"down" && hasSingleArgument) {
        return Command{CommandKind::Down};
    }
    if (name == L"show" && hasSingleArgument) {
        return Command{CommandKind::Show};
    }
    if (name == L"exit" && hasSingleArgument) {
        return Command{CommandKind::Exit};
    }
    if (name == L"set" && arguments.size() == 2) {
        auto value = ParseNonNegativeInt(arguments[1]);
        if (value) {
            return Command{CommandKind::Set, *value};
        }
    }

    return std::nullopt;
}

std::wstring UsageText()
{
    return L"Usage: SamsungBrightness.exe [command]\n"
           L"\n"
           L"  (no command)\tstart in the tray\n"
           L"  up\t\tincrease brightness by one step\n"
           L"  down\t\tdecrease brightness by one step\n"
           L"  set <value>\tset brightness, e.g. set 25\n"
           L"  show\t\tshow the current brightness\n"
           L"  exit\t\tclose the running instance";
}

bool SendToRunningInstance(Command command)
{
    // Launching the app again without arguments shows the current brightness.
    if (command.kind == CommandKind::Start) {
        command.kind = CommandKind::Show;
    }

    auto window = FindRunningInstance();
    if (window == nullptr) {
        return false;
    }

    CommandPayload payload{static_cast<int32_t>(command.kind), command.value};

    COPYDATASTRUCT data{};
    data.dwData = kCopyDataId;
    data.cbData = sizeof(payload);
    data.lpData = &payload;

    auto sent = SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                                    SMTO_ABORTIFHUNG, kSendTimeoutMs, nullptr);
    return sent != 0;
}

std::optional<Command> CommandFromCopyData(const COPYDATASTRUCT& data)
{
    if (data.dwData != kCopyDataId || data.cbData != sizeof(CommandPayload) || data.lpData == nullptr) {
        return std::nullopt;
    }

    const auto* payload = static_cast<const CommandPayload*>(data.lpData);
    auto isKnownKind = payload->kind >= static_cast<int32_t>(CommandKind::Start)
        && payload->kind <= static_cast<int32_t>(CommandKind::Exit);
    if (!isKnownKind) {
        return std::nullopt;
    }

    return Command{static_cast<CommandKind>(payload->kind), payload->value};
}
