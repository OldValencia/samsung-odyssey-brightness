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

constexpr size_t kMaxErrorSnippet = 200;

// The monitor uses a self-signed certificate that Windows cannot verify (see README, "Security").
constexpr DWORD kIgnoreCertificateErrors = SECURITY_FLAG_IGNORE_UNKNOWN_CA
    | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
    | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
    | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;

std::wstring DescribeWinHttpError(DWORD code)
{
    switch (code) {
    case ERROR_WINHTTP_TIMEOUT:
        return L"the monitor did not answer in time";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return L"the address could not be resolved";
    case ERROR_WINHTTP_CANNOT_CONNECT:
        return L"cannot connect: is the monitor on and IP Remote enabled?";
    case ERROR_WINHTTP_CONNECTION_ERROR:
        return L"the connection was reset";
    case ERROR_WINHTTP_SECURE_FAILURE:
        return L"TLS handshake failed";
    case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
        return L"invalid response";
    default:
        return L"network error";
    }
}

std::string ReadResponseBody(HINTERNET request)
{
    std::string body;
    DWORD available = 0;

    while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
        auto offset = body.size();
        body.resize(offset + available);

        DWORD read = 0;
        if (!WinHttpReadData(request, body.data() + offset, available, &read)) {
            body.resize(offset);
            break;
        }
        body.resize(offset + read);
    }

    return body;
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
    : host_(host)
{
    // The monitor is in the local network, so system proxy settings are ignored on purpose.
    session_.reset(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_NO_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));

    if (session_ && !host_.empty()) {
        connection_.reset(WinHttpConnect(session_.get(), host_.c_str(), static_cast<INTERNET_PORT>(port), 0));
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
    if (!brightness) {
        SetUnexpectedResponseError(*response);
    }
    return brightness;
}

std::optional<std::string> SamsungClient::Post(const std::string& body, int receiveTimeoutMs)
{
    if (host_.empty()) {
        lastError_ = L"SAMSUNG_MONITOR_IP is empty in .env.";
        return std::nullopt;
    }
    if (!connection_) {
        SetWinHttpError(L"start a connection");
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
    auto response = ReadResponseBody(request.get());

    if (statusCode != 200) {
        lastError_ = std::format(L"The monitor answered with HTTP {}.", statusCode);
        return std::nullopt;
    }
    return response;
}

void SamsungClient::SetWinHttpError(const wchar_t* action)
{
    auto code = GetLastError();
    lastError_ = std::format(L"Cannot {}: {} (WinHTTP error {}).", action, DescribeWinHttpError(code), code);
}

void SamsungClient::SetUnexpectedResponseError(const std::string& response)
{
    auto message = json_rpc::FindString(response, "message");

    if (message) {
        lastError_ = L"The monitor reported an error: " + FromUtf8(*message);
    } else {
        lastError_ = L"Unexpected answer from the monitor: " + FromUtf8(response.substr(0, kMaxErrorSnippet));
    }
}
