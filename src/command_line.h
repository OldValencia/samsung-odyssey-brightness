#pragma once

#include <windows.h>

#include <optional>
#include <string>

enum class CommandKind {
    Start,  // no arguments: just run in the tray
    Up,
    Down,
    Set,
    Show,
    Exit,
};

struct Command {
    CommandKind kind = CommandKind::Start;
    // Used by CommandKind::Set.
    int value = 0;
};

// Returns nothing when the arguments are invalid.
std::optional<Command> ParseCommandLine();

std::wstring UsageText();

// Passes the command to the already running instance via WM_COPYDATA.
bool SendToRunningInstance(Command command);

// Reads a command sent by SendToRunningInstance.
std::optional<Command> CommandFromCopyData(const COPYDATASTRUCT& data);
