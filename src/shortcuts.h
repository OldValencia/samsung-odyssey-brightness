#pragma once

#include "brightness_key.h"

#include <windows.h>

#include <array>
#include <bitset>
#include <optional>
#include <string>

struct Shortcut {
    UINT key = 0;
    UINT modifiers = 0;
    std::array<BYTE, 4> extraKeys{};
    bool operator==(const Shortcut&) const = default;
    bool IsChord() const { return extraKeys[0] != 0; }
};

class Shortcuts;
std::wstring ShortcutLabel(Shortcut shortcut);
std::optional<Shortcut> EditShortcut(HINSTANCE instance, HWND owner, BrightnessKey direction, Shortcut current,
                                     Shortcuts& shortcuts);

class Shortcuts {
public:
    enum class SaveResult { Saved, Unavailable, WriteFailed };

    void Load(HWND window);
    void Unregister();
    SaveResult Set(BrightnessKey direction, Shortcut shortcut);
    Shortcut Get(BrightnessKey direction) const;
    std::optional<BrightnessKey> HandleHotkey(WPARAM id, LPARAM hotkey) const;
    std::wstring Label(BrightnessKey direction) const;
    bool BeginRecording(HWND field, Shortcut current);
    void EndRecording();
    void ClearRecording();
    Shortcut Recorded() const { return recorded_; }
    std::optional<BrightnessKey> KeyboardInput(const RAWKEYBOARD& keyboard);
    void ResetKeyboard() { pressed_.reset(); recordingReleased_ = false; }

    bool UseBrightnessKeys() const { return useBrightnessKeys_; }
    bool SetUseBrightnessKeys(bool enabled);

private:
    struct Binding {
        Shortcut shortcut;
        int registration = 0;
    };

    Binding& For(BrightnessKey direction);
    const Binding& For(BrightnessKey direction) const;
    bool EnableKeyboardInput();
    void ReleaseUnusedKeyboardInput();

    HWND window_ = nullptr;
    Binding increase_;
    Binding decrease_;
    bool useBrightnessKeys_ = true;
    bool keyboardInputEnabled_ = false;
    HWND recordingField_ = nullptr;
    std::bitset<256> pressed_;
    Shortcut recorded_;
    bool recordingReleased_ = false;
};
