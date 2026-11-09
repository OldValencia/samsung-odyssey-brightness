#include "pairing.h"

#include "app_constants.h"
#include "samsung_client.h"

#include <memory>
#include <thread>

void StartPairing(HWND resultWindow, const std::wstring& monitorIp, int port)
{
    // Detached: if the app exits meanwhile, the process simply ends together with the thread.
    std::thread([resultWindow, monitorIp, port] {
        SamsungClient client(monitorIp, port);
        auto token = client.CreateAccessToken();

        auto result = std::make_unique<PairingResult>();
        result->success = token.has_value();
        result->text = token ? *token : client.LastError();

        auto posted = PostMessageW(resultWindow, WM_APP_PAIRING_RESULT, 0, reinterpret_cast<LPARAM>(result.get()));
        if (posted) {
            result.release();  // now owned by the receiver
        }
    }).detach();
}
