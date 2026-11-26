#include "shortcuts.h"

#include "log.h"

#include <algorithm>
#include <bit>

namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\SamsungBrightness";
constexpr wchar_t kBrightnessKeysValue[] = L"UseBrightnessKeys";
constexpr UINT kAllowedModifiers = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
constexpr USHORT kGenericDesktop = 1;
constexpr USHORT kKeyboard = 6;

UINT ModifierFor(UINT key)
{
    switch (key) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return MOD_SHIFT;
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return MOD_CONTROL;
    case VK_MENU: case VK_LMENU: case VK_RMENU: return MOD_ALT;
    case VK_LWIN: case VK_RWIN: return MOD_WIN;
    default: return 0;
    }
}

Shortcut MakeShortcut(const std::bitset<256>& pressed)
{
    if (pressed.count() > 5) {
        return {};
    }
    Shortcut result;
    size_t extras = 0;
    for (UINT key = 1; key < pressed.size(); ++key) {
        if (!pressed.test(key)) {
            continue;
        }
        if (const auto modifier = ModifierFor(key)) {
            result.modifiers |= modifier;
        } else if (!result.key) {
            result.key = key;
        } else if (extras < result.extraKeys.size()) {
            result.extraKeys[extras++] = static_cast<BYTE>(key);
        } else {
            return {};
        }
    }
    return result.key && extras + std::popcount(result.modifiers) < 5 ? result : Shortcut{};
}

bool Includes(const Shortcut& chord, const Shortcut& part)
{
    const auto hasKey = [&](UINT key) {
        return key == 0 || key == chord.key || std::ranges::find(chord.extraKeys, key) != chord.extraKeys.end();
    };
    return part.key && (chord.modifiers & part.modifiers) == part.modifiers && hasKey(part.key)
        && std::ranges::all_of(part.extraKeys, hasKey);
}

UINT PhysicalKey(const RAWKEYBOARD& keyboard)
{
    const bool extended = (keyboard.Flags & RI_KEY_E0) != 0;
    switch (keyboard.VKey) {
    case VK_SHIFT: {
        const auto key = MapVirtualKeyW(keyboard.MakeCode, MAPVK_VSC_TO_VK_EX);
        return key ? key : VK_SHIFT;
    }
    case VK_CONTROL: return extended ? VK_RCONTROL : VK_LCONTROL;
    case VK_MENU: return extended ? VK_RMENU : VK_LMENU;
    default: return keyboard.VKey;
    }
}

const wchar_t* ValueName(BrightnessKey direction)
{
    return direction == BrightnessKey::Up ? L"IncreaseShortcut" : L"DecreaseShortcut";
}

DWORD ReadValue(const wchar_t* name, DWORD fallback = 0)
{
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD,
                        nullptr, &value, &bytes) == ERROR_SUCCESS ? value : fallback;
}

bool WriteValue(const wchar_t* name, DWORD value)
{
    return RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD,
                           &value, sizeof(value)) == ERROR_SUCCESS;
}

Shortcut ReadShortcut(BrightnessKey direction)
{
    ULONGLONG encoded = 0;
    DWORD bytes = sizeof(encoded);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, ValueName(direction), RRF_RT_REG_QWORD,
                     nullptr, &encoded, &bytes) != ERROR_SUCCESS) {
        encoded = ReadValue(ValueName(direction));
    }
    const auto first = static_cast<DWORD>(encoded);
    Shortcut shortcut{HIWORD(first), LOWORD(first)};
    if (shortcut.key == 0 || shortcut.key >= 0xff || ModifierFor(shortcut.key) || (shortcut.modifiers & ~kAllowedModifiers)) {
        return {};
    }
    auto last = shortcut.key;
    size_t extras = 0;
    for (auto& key : shortcut.extraKeys) {
        key = static_cast<BYTE>(encoded >> (32 + 8 * extras));
        if (key != 0 && (key <= last || ModifierFor(key) || key == 0xff)) {
            return {};
        }
        last = key ? key : 0xff;
        ++extras;
    }
    const auto keys = std::popcount(shortcut.modifiers) + 1;
    for (size_t i = 5 - keys; i < shortcut.extraKeys.size(); ++i) {
        if (shortcut.extraKeys[i]) {
            return {};
        }
    }
    return shortcut.key == VK_F12 && !shortcut.IsChord() ? Shortcut{} : shortcut;
}

bool WriteShortcut(BrightnessKey direction, const Shortcut& shortcut)
{
    const auto first = static_cast<DWORD>(MAKELONG(shortcut.modifiers, shortcut.key));
    if (!shortcut.IsChord()) {
        return WriteValue(ValueName(direction), first);
    }
    ULONGLONG encoded = first;
    for (size_t i = 0; i < shortcut.extraKeys.size(); ++i) {
        encoded |= static_cast<ULONGLONG>(shortcut.extraKeys[i]) << (32 + 8 * i);
    }
    return RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, ValueName(direction), REG_QWORD,
                           &encoded, sizeof(encoded)) == ERROR_SUCCESS;
}

int BaseId(BrightnessKey direction)
{
    return direction == BrightnessKey::Up ? 1 : 3;
}

}

void Shortcuts::Load(HWND window)
{
    window_ = window;
    useBrightnessKeys_ = ReadValue(kBrightnessKeysValue, 1) != 0;
    for (const auto direction : {BrightnessKey::Up, BrightnessKey::Down}) {
        auto& binding = For(direction);
        binding.shortcut = ReadShortcut(direction);
        if (binding.shortcut.IsChord()) {
            if (EnableKeyboardInput()) {
                binding.registration = -1;
            }
        } else if (binding.shortcut.key) {
            const auto id = BaseId(direction);
            if (RegisterHotKey(window_, id, binding.shortcut.modifiers | MOD_NOREPEAT, binding.shortcut.key)) {
                binding.registration = id;
            } else {
                logging::Write(L"Cannot register %ls (error %lu)", ValueName(direction), GetLastError());
            }
        }
    }
}

void Shortcuts::Unregister()
{
    for (auto* binding : {&increase_, &decrease_}) {
        if (binding->registration > 0) {
            UnregisterHotKey(window_, binding->registration);
        }
        binding->registration = 0;
    }
    recordingField_ = nullptr;
    ReleaseUnusedKeyboardInput();
}

Shortcuts::SaveResult Shortcuts::Set(BrightnessKey direction, Shortcut shortcut)
{
    auto& binding = For(direction);
    if (binding.shortcut == shortcut && (binding.registration || shortcut.key == 0)) {
        return SaveResult::Saved;
    }

    auto id = 0;
    if (shortcut.key) {
        if (shortcut.key == VK_F12 && !shortcut.IsChord()) {
            return SaveResult::Unavailable;
        }
        const auto otherDirection = direction == BrightnessKey::Up ? BrightnessKey::Down : BrightnessKey::Up;
        const auto& other = For(otherDirection).shortcut;
        if (shortcut == other || (shortcut.IsChord() && Includes(shortcut, other))
            || (other.IsChord() && Includes(other, shortcut))) {
            return SaveResult::Unavailable;
        }
        if (shortcut.IsChord()) {
            if (!EnableKeyboardInput()) {
                return SaveResult::Unavailable;
            }
            id = -1;
        } else {
            const auto base = BaseId(direction);
            id = binding.registration == base ? base + 1 : base;
            // Reserve the replacement first so a conflict leaves the old binding active.
            if (!RegisterHotKey(window_, id, shortcut.modifiers | MOD_NOREPEAT, shortcut.key)) {
                return SaveResult::Unavailable;
            }
        }
    }

    if (!WriteShortcut(direction, shortcut)) {
        if (id > 0) {
            UnregisterHotKey(window_, id);
        }
        ReleaseUnusedKeyboardInput();
        return SaveResult::WriteFailed;
    }

    if (binding.registration > 0) {
        UnregisterHotKey(window_, binding.registration);
    }
    binding = {shortcut, id};
    ReleaseUnusedKeyboardInput();
    return SaveResult::Saved;
}

std::optional<BrightnessKey> Shortcuts::HandleHotkey(WPARAM id, LPARAM hotkey) const
{
    if (recordingField_) {
        return std::nullopt;
    }
    for (const auto direction : {BrightnessKey::Up, BrightnessKey::Down}) {
        const auto& binding = For(direction);
        if (binding.registration > 0 && id == static_cast<WPARAM>(binding.registration)
            && HIWORD(hotkey) == binding.shortcut.key
            && (LOWORD(hotkey) & kAllowedModifiers) == binding.shortcut.modifiers) {
            return direction;
        }
    }
    return std::nullopt;
}

bool Shortcuts::EnableKeyboardInput()
{
    if (!keyboardInputEnabled_) {
        const RAWINPUTDEVICE device{kGenericDesktop, kKeyboard, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window_};
        keyboardInputEnabled_ = RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
    }
    return keyboardInputEnabled_;
}

void Shortcuts::ReleaseUnusedKeyboardInput()
{
    if (keyboardInputEnabled_ && !recordingField_ && increase_.registration != -1 && decrease_.registration != -1) {
        const RAWINPUTDEVICE device{kGenericDesktop, kKeyboard, RIDEV_REMOVE, nullptr};
        if (RegisterRawInputDevices(&device, 1, sizeof(device))) {
            keyboardInputEnabled_ = false;
            ResetKeyboard();
        }
    }
}

bool Shortcuts::BeginRecording(HWND field, Shortcut current)
{
    ResetKeyboard();
    recorded_ = current;
    recordingField_ = field;
    return EnableKeyboardInput();
}

void Shortcuts::EndRecording()
{
    recordingField_ = nullptr;
    ResetKeyboard();
    ReleaseUnusedKeyboardInput();
}

void Shortcuts::ClearRecording()
{
    recorded_ = {};
    ResetKeyboard();
    SetWindowTextW(recordingField_, L"None");
}

std::optional<BrightnessKey> Shortcuts::KeyboardInput(const RAWKEYBOARD& keyboard)
{
    const auto key = PhysicalKey(keyboard);
    if (!key || key >= 0xff) {
        return std::nullopt;
    }
    const bool down = (keyboard.Flags & RI_KEY_BREAK) == 0;
    if (pressed_.test(key) == down) {
        return std::nullopt;
    }
    pressed_.set(key, down);
    if (recordingField_) {
        if (GetFocus() != recordingField_ || GetForegroundWindow() != GetAncestor(recordingField_, GA_ROOT)) {
            ResetKeyboard();
        } else if (!down) {
            recordingReleased_ = pressed_.any();
        } else if (!recordingReleased_) {
            if (auto candidate = MakeShortcut(pressed_); candidate.key && (candidate.key != VK_F12 || candidate.IsChord())) {
                recorded_ = candidate;
                SetWindowTextW(recordingField_, ShortcutLabel(recorded_).c_str());
                EnableWindow(GetDlgItem(GetParent(recordingField_), IDOK), TRUE);
            } else if (pressed_.count() > 5 || key == VK_F12) {
                SetWindowTextW(recordingField_, key == VK_F12 ? L"F12 is reserved by Windows" : L"Up to five keys");
                EnableWindow(GetDlgItem(GetParent(recordingField_), IDOK), FALSE);
            }
        }
        return std::nullopt;
    }
    if (down && pressed_.count() >= 2) {
        const auto chord = MakeShortcut(pressed_);
        for (const auto direction : {BrightnessKey::Up, BrightnessKey::Down}) {
            const auto& binding = For(direction);
            if (binding.registration == -1 && chord == binding.shortcut) {
                return direction;
            }
        }
    }
    return std::nullopt;
}

Shortcut Shortcuts::Get(BrightnessKey direction) const
{
    return For(direction).shortcut;
}

std::wstring Shortcuts::Label(BrightnessKey direction) const
{
    const auto& binding = For(direction);
    auto label = ShortcutLabel(binding.shortcut);
    if (binding.shortcut.key && !binding.registration) {
        label += L" (unavailable)";
    }
    return label;
}

bool Shortcuts::SetUseBrightnessKeys(bool enabled)
{
    if (!WriteValue(kBrightnessKeysValue, enabled ? 1 : 0)) {
        return false;
    }
    useBrightnessKeys_ = enabled;
    return true;
}

Shortcuts::Binding& Shortcuts::For(BrightnessKey direction)
{
    return direction == BrightnessKey::Up ? increase_ : decrease_;
}

const Shortcuts::Binding& Shortcuts::For(BrightnessKey direction) const
{
    return direction == BrightnessKey::Up ? increase_ : decrease_;
}
