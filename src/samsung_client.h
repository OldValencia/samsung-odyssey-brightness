#pragma once

#include <windows.h>
#include <winhttp.h>

#include <memory>
#include <optional>
#include <string>

// Talks to the "Samsung IP Control" server built into Tizen-based Samsung monitors and TVs:
// JSON-RPC 2.0 over HTTPS (port 1516 by default) in the local network.
//
// Not thread-safe: use one instance per thread. WinHTTP keeps the connection alive between
// calls, so repeated requests skip the TLS handshake and answer quickly.
class SamsungClient {
public:
    SamsungClient(const std::wstring& host, int port);

    std::optional<int> GetBrightness(const std::wstring& accessToken);
    // Returns the brightness the monitor reports after the change.
    std::optional<int> SetBrightness(const std::wstring& accessToken, int value);

    // Asks the monitor for a new access token. The user has to accept the prompt on the monitor screen.
    std::optional<std::wstring> CreateAccessToken();

    // Human-readable reason of the last failure.
    const std::wstring& LastError() const
    {
        return lastError_;
    }

private:
    struct HandleCloser {
        void operator()(HINTERNET handle) const
        {
            WinHttpCloseHandle(handle);
        }
    };

    using InternetHandle = std::unique_ptr<void, HandleCloser>;

    std::optional<int> CallBacklightControl(const std::wstring& accessToken, std::optional<int> newValue);
    std::optional<std::string> Post(const std::string& body, int receiveTimeoutMs);
    void SetWinHttpError(const wchar_t* action);
    void SetUnexpectedResponseError(const std::string& response);

    std::wstring host_;
    InternetHandle session_;
    InternetHandle connection_;
    std::wstring lastError_;
};
