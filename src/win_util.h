#pragma once

#include <windows.h>

// Connects a window to the C++ object that handles its messages.
// The object is passed as the last argument of CreateWindowEx and stored in GWLP_USERDATA.
template <typename T>
T* WindowObject(HWND window, UINT message, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        auto* createInfo = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* object = static_cast<T*>(createInfo->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(object));
        return object;
    }

    return reinterpret_cast<T*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}
