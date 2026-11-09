#include "json_rpc.h"

#include <cctype>
#include <charconv>

namespace json_rpc {

namespace {

constexpr auto npos = std::string_view::npos;

size_t SkipWhitespace(std::string_view json, size_t position)
{
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) {
        ++position;
    }
    return position;
}

// Returns the position of the value that follows "key":, or npos when the key is missing.
size_t FindValueStart(std::string_view json, std::string_view key)
{
    auto quotedKey = "\"" + std::string(key) + "\"";

    auto position = json.find(quotedKey);
    if (position == npos) {
        return npos;
    }

    position = SkipWhitespace(json, position + quotedKey.size());
    if (position >= json.size() || json[position] != ':') {
        return npos;
    }

    position = SkipWhitespace(json, position + 1);
    return position < json.size() ? position : npos;
}

}

std::string BuildRequest(std::string_view id, std::string_view method, std::string_view paramsObject)
{
    std::string request = "{\"jsonrpc\":\"2.0\"";
    request += ",\"id\":\"" + EscapeString(id) + "\"";
    request += ",\"method\":\"" + EscapeString(method) + "\"";

    if (!paramsObject.empty()) {
        request += ",\"params\":";
        request += paramsObject;
    }

    request += "}";
    return request;
}

std::string EscapeString(std::string_view text)
{
    std::string escaped;
    escaped.reserve(text.size());

    for (char character : text) {
        switch (character) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            // Other control characters are not valid in JSON strings and never appear in tokens.
            if (static_cast<unsigned char>(character) >= 0x20) {
                escaped += character;
            }
            break;
        }
    }
    return escaped;
}

std::optional<int> FindInt(std::string_view json, std::string_view key)
{
    auto position = FindValueStart(json, key);
    if (position == npos) {
        return std::nullopt;
    }

    // Accept numbers sent as strings too, e.g. "backlight":"25".
    if (json[position] == '"') {
        ++position;
    }

    int value = 0;
    auto [end, error] = std::from_chars(json.data() + position, json.data() + json.size(), value);
    if (error != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::string> FindString(std::string_view json, std::string_view key)
{
    auto position = FindValueStart(json, key);
    if (position == npos || json[position] != '"') {
        return std::nullopt;
    }

    std::string value;
    for (auto index = position + 1; index < json.size(); ++index) {
        auto character = json[index];

        if (character == '"') {
            return value;
        }

        // Keep the escaped character itself; \uXXXX sequences are not needed for this protocol.
        if (character == '\\' && index + 1 < json.size()) {
            character = json[++index];
        }
        value += character;
    }

    return std::nullopt;
}

}
