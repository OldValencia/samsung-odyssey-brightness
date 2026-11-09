#pragma once

#include <windows.h>

#include <vector>

// Copies the data of a WM_INPUT message into buffer. Returns nullptr on failure.
inline const RAWINPUT* ReadRawInput(HRAWINPUT input, std::vector<BYTE>& buffer)
{
    UINT size = 0;
    GetRawInputData(input, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    if (size == 0) {
        return nullptr;
    }

    buffer.resize(size);
    auto copied = GetRawInputData(input, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER));
    if (copied == static_cast<UINT>(-1)) {
        return nullptr;
    }

    return reinterpret_cast<const RAWINPUT*>(buffer.data());
}
