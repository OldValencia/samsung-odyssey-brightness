#include "shortcuts.h"

#include "resource.h"

#include <commctrl.h>

namespace {

std::wstring KeyLabel(UINT key)
{
    if (key >= VK_F1 && key <= VK_F24) {
        return L"F" + std::to_wstring(key - VK_F1 + 1);
    }
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) {
        return std::wstring(1, static_cast<wchar_t>(key));
    }
    switch (key) {
    case VK_UP: return L"Up";
    case VK_DOWN: return L"Down";
    case VK_LEFT: return L"Left";
    case VK_RIGHT: return L"Right";
    case VK_ADD: return L"Numpad +";
    case VK_SUBTRACT: return L"Numpad -";
    default: break;
    }
    const auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
    auto extended = (scan & 0xff00) == 0xe000 ? 1u << 24 : 0;
    wchar_t name[64]{};
    if (GetKeyNameTextW(static_cast<LONG>(((scan & 0xff) << 16) | extended), name, 64) > 0) {
        return name;
    }
    return L"Key " + std::to_wstring(key);
}

struct DialogState {
    BrightnessKey direction;
    Shortcut shortcut;
    Shortcuts& shortcuts;
};

LRESULT CALLBACK CaptureField(HWND field, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
{
    switch (message) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
        return 0;
    case WM_KILLFOCUS:
        reinterpret_cast<Shortcuts*>(data)->ResetKeyboard();
        break;
    }
    return DefSubclassProc(field, message, wParam, lParam);
}

INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INITDIALOG) {
        auto* state = reinterpret_cast<DialogState*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        SetWindowTextW(dialog, state->direction == BrightnessKey::Up ? L"Increase brightness" : L"Decrease brightness");
        const auto field = GetDlgItem(dialog, IDC_SHORTCUT_KEY);
        SetWindowTextW(field, ShortcutLabel(state->shortcut).c_str());
        if (!SetWindowSubclass(field, CaptureField, 1, reinterpret_cast<DWORD_PTR>(&state->shortcuts))
            || !state->shortcuts.BeginRecording(field, state->shortcut)) {
            MessageBoxW(dialog, L"Windows could not start keyboard recording.", L"Keyboard shortcut", MB_OK | MB_ICONWARNING);
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        SetFocus(field);
        return FALSE;
    }

    if (message == WM_COMMAND) {
        auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
        switch (LOWORD(wParam)) {
        case IDC_SHORTCUT_CLEAR:
            state->shortcuts.ClearRecording();
            EnableWindow(GetDlgItem(dialog, IDOK), TRUE);
            SetFocus(GetDlgItem(dialog, IDC_SHORTCUT_KEY));
            return TRUE;
        case IDOK:
            state->shortcut = state->shortcuts.Recorded();
            EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}

}

std::wstring ShortcutLabel(Shortcut shortcut)
{
    if (!shortcut.key) {
        return L"None";
    }
    std::wstring label;
    constexpr std::pair<UINT, const wchar_t*> modifiers[]{
        {MOD_CONTROL, L"Ctrl+"}, {MOD_ALT, L"Alt+"}, {MOD_SHIFT, L"Shift+"}, {MOD_WIN, L"Win+"},
    };
    for (const auto& [flag, name] : modifiers) {
        if (shortcut.modifiers & flag) {
            label += name;
        }
    }
    label += KeyLabel(shortcut.key);
    for (const auto key : shortcut.extraKeys) {
        if (key) {
            label += L"+" + KeyLabel(key);
        }
    }
    return label;
}

std::optional<Shortcut> EditShortcut(HINSTANCE instance, HWND owner, BrightnessKey direction, Shortcut current,
                                     Shortcuts& shortcuts)
{
    DialogState state{direction, current, shortcuts};
    const auto result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SHORTCUT), owner, DialogProc,
                                        reinterpret_cast<LPARAM>(&state));
    shortcuts.EndRecording();
    return result == IDOK ? std::optional(state.shortcut) : std::nullopt;
}
