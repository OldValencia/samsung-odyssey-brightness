#include "samsung_client.h"

#include "json_rpc.h"
#include "text.h"

#include <format>

namespace {

constexpr wchar_t kUserAgent[] = L"SamsungBrightness/1.0";
constexpr wchar_t kRequestHeaders[] = L"Content-Type: application/json\r\nAccept: application/json\r\n";

constexpr int kResolveTimeoutMs = 2000;
constexpr int kConnectTimeoutMs = 3000;
constexpr int kSendTimeoutMs = 3000;
constexpr int kReceiveTimeoutMs = 5000;
// Pairing waits until the user accepts the prompt on the monitor.
constexpr int kPairingTimeoutMs = 60000;

// The monitor uses a self-signed certificate that Windows cannot verify (see README, "Security").
constexpr DWORD kIgnoreCertificateErrors = SECURITY_FLAG_IGNORE_UNKNOWN_CA
    | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
    | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
    | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;

std::wstring DescribeWinHttpError(DWORD code)
{
    switch (code) {
    case ERROR_WINHTTP_TIMEOUT:
        return L"Monitor timed out";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return L"Address not found";
    case ERROR_WINHTTP_CANNOT_CONNECT:
        return L"Cannot connect";
    case ERROR_WINHTTP_CONNECTION_ERROR:
        return L"Connection reset";
    case ERROR_WINHTTP_SECURE_FAILURE:
        return L"TLS connection failed";
    case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
        return L"Invalid HTTP response";
    default:
        return L"Network error";
    }
}

std::optional<std::string> ReadResponseBody(HINTERNET request)
{
    std::string body;
    char buffer[4096];

    while (true) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer, sizeof(buffer), &read)) {
            return std::nullopt;
        }
        if (read == 0) {
            return body;
        }
        body.append(buffer, read);
    }
}

DWORD QueryStatusCode(HINTERNET request)
{
    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);

    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
    return statusCode;
}

}

SamsungClient::SamsungClient(const std::wstring& host, int port)
    : host_(host), port_(port)
{
    // The monitor is in the local network, so system proxy settings are ignored on purpose.
    session_.reset(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_NO_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));

    if (!session_) {
        SetWinHttpError(L"open a session");
    } else if (!host_.empty()) {
        connection_.reset(WinHttpConnect(session_.get(), host_.c_str(), static_cast<INTERNET_PORT>(port_), 0));
        if (!connection_) {
            SetWinHttpError(L"start a connection");
        }
    }
}

std::optional<int> SamsungClient::GetBrightness(const std::wstring& accessToken)
{
    return CallBacklightControl(accessToken, std::nullopt);
}

std::optional<int> SamsungClient::SetBrightness(const std::wstring& accessToken, int value)
{
    return CallBacklightControl(accessToken, value);
}

std::optional<std::wstring> SamsungClient::CreateAccessToken()
{
    auto response = Post(json_rpc::BuildRequest("pair", "createAccessToken", ""), kPairingTimeoutMs);
    if (!response) {
        return std::nullopt;
    }

    auto token = json_rpc::FindString(*response, "AccessToken");
    if (!token || token->empty()) {
        SetUnexpectedResponseError(*response);
        return std::nullopt;
    }
    return FromUtf8(*token);
}

// "backlightControl" reads the brightness, or changes it when "backlight" is passed.
std::optional<int> SamsungClient::CallBacklightControl(const std::wstring& accessToken, std::optional<int> newValue)
{
    if (accessToken.empty()) {
        errorSummary_ = L"Pair with monitor";
        lastError_ = L"SAMSUNG_ACCESS_TOKEN is empty. Use \"Pair with monitor...\" in the tray menu.";
        return std::nullopt;
    }

    auto params = "{\"AccessToken\":\"" + json_rpc::EscapeString(ToUtf8(accessToken)) + "\"";
    if (newValue) {
        params += ",\"backlight\":" + std::to_string(*newValue);
    }
    params += "}";

    auto id = newValue ? "set-brightness" : "get-brightness";
    auto response = Post(json_rpc::BuildRequest(id, "backlightControl", params), kReceiveTimeoutMs);
    if (!response) {
        return std::nullopt;
    }

    auto brightness = json_rpc::FindInt(*response, "backlight");
    if (!brightness || *brightness < 0) {
        SetUnexpectedResponseError(*response);
        return std::nullopt;
    }
    return brightness;
}

std::optional<std::string> SamsungClient::Post(const std::string& body, int receiveTimeoutMs)
{
    if (host_.empty()) {
        errorSummary_ = L"Monitor IP missing";
        lastError_ = L"SAMSUNG_MONITOR_IP is empty in .env.";
        return std::nullopt;
    }
    if (!connection_) {
        return std::nullopt;
    }

    InternetHandle request(WinHttpOpenRequest(connection_.get(), L"POST", L"/", nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) {
        SetWinHttpError(L"create a request");
        return std::nullopt;
    }

    auto securityFlags = kIgnoreCertificateErrors;
    WinHttpSetOption(request.get(), WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
    WinHttpSetTimeouts(request.get(), kResolveTimeoutMs, kConnectTimeoutMs, kSendTimeoutMs, receiveTimeoutMs);

    auto* data = const_cast<char*>(body.data());
    auto size = static_cast<DWORD>(body.size());

    if (!WinHttpSendRequest(request.get(), kRequestHeaders, static_cast<DWORD>(-1L), data, size, size, 0)) {
        SetWinHttpError(L"send the request");
        return std::nullopt;
    }
    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        SetWinHttpError(L"receive the answer");
        return std::nullopt;
    }

    auto statusCode = QueryStatusCode(request.get());
    if (statusCode == 0) {
        SetWinHttpError(L"read the HTTP status");
        return std::nullopt;
    }

    if (statusCode != 200) {
        lastError_ = std::format(L"The monitor answered with HTTP {}.", statusCode);
        errorSummary_ = statusCode == 401 || statusCode == 403
            ? L"Access denied" : std::format(L"Monitor HTTP {}", statusCode);
        return std::nullopt;
    }
    auto response = ReadResponseBody(request.get());
    if (!response) {
        SetWinHttpError(L"read the answer");
    }
    return response;
}

void SamsungClient::SetWinHttpError(const wchar_t* action)
{
    auto code = GetLastError();
    auto reason = DescribeWinHttpError(code);
    auto endpoint = std::format(L"{}:{}", host_, port_);
    errorSummary_ = std::format(L"{}\n{}", reason, endpoint);
    lastError_ = std::format(L"{} at {} ({}. WinHTTP error {}).", reason, endpoint, action, code);
}

void SamsungClient::SetUnexpectedResponseError(const std::string& response)
{
    auto code = json_rpc::FindInt(response, "code");
    if (code) {
        lastError_ = std::format(L"The monitor reported JSON-RPC error {}.", *code);
        errorSummary_ = std::format(L"Monitor error {}", *code);
    } else {
        lastError_ = L"Unexpected answer from the monitor.";
        errorSummary_ = L"Invalid monitor reply";
    }
}
