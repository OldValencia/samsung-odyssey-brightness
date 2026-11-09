#pragma once

#include <optional>
#include <string>
#include <string_view>

// Minimal JSON-RPC 2.0 helpers for the Samsung IP Control protocol.
// The monitor's answers are tiny flat objects such as
//   {"jsonrpc":"2.0","id":"1","result":{"backlight":25}}
// so a full JSON parser would be overkill.
namespace json_rpc {

// paramsObject is a ready JSON object like {"a":1}, or empty when the method has no parameters.
std::string BuildRequest(std::string_view id, std::string_view method, std::string_view paramsObject);

// Escapes quotes, backslashes and control characters for use inside a JSON string.
std::string EscapeString(std::string_view text);

// Finds the first "key": <number> (or "key": "<number>") anywhere in the document.
std::optional<int> FindInt(std::string_view json, std::string_view key);

// Finds the first "key": "<text>" anywhere in the document.
std::optional<std::string> FindString(std::string_view json, std::string_view key);

}
