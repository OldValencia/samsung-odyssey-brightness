# CLAUDE.md

Guidance for AI coding sessions in this repository. Read it before changing anything.

## What this is

A tiny Windows tray app (C++20, pure Win32) that controls the hardware brightness of Samsung
Tizen-based monitors (tested: Odyssey OLED G8 / G85SB, LS34BG850SUXEN, firmware 1720) through the
local **Samsung IP Control** JSON-RPC API. These monitors have no DDC/CI. Brightness keys come from
keyboards via Raw Input. See `README.md` for the user-facing description.

## Project principles

- **Minimal resource usage is a core feature.** ~3.8 MB RAM (private working set), 0 CPU when idle,
  no polling, no idle timers. Do not add frameworks, runtimes, third-party libraries, or background
  polling. Only Windows SDK libraries (currently `winhttp`, `hid`, `shcore`).
- **Readable code over clever code.** People read this project to learn from it.
- **The repository is public and fully AI-generated**, and the README says so. Keep that note.

## Build

```bat
build.cmd [version]        rem e.g. build.cmd 1.2.0 -> build\SamsungBrightness.exe
```

- Needs Visual Studio 2022+ with "Desktop development with C++" (CMake and Ninja come with it).
- From PowerShell run it as `& "$env:SystemRoot\System32\cmd.exe" /c "D:\Projects\samsung-brightness\build.cmd"`
  (a plain `cmd` is not on PATH in this environment).
- On the owner's machine PATH contains unexpanded `%SystemRoot%` entries; `build.cmd` prepends
  `System32` and the VS Installer folder itself. Keep that.
- The build must stay **warning-free at `/W4`**. The C++ runtime is linked statically (`/MT`), so the
  exe has no dependencies. Version: `APP_VERSION` CMake cache variable -> `resources/version.h.in` ->
  exe properties. The CMake cache remembers the last version; pass it explicitly when it matters.
- There are no automated tests. Verify changes on real hardware (see "Testing").

## Code conventions

- C++20, `UNICODE`, `WIN32_LEAN_AND_MEAN`, `NOMINMAX`. Wide-char Win32 APIs (`...W`).
- English for code, comments, UI text, docs and commit messages.
- Always use braces for `if`/`else`/loops. One statement per line. Blank lines between logical steps.
- Prefer `auto` when the type is obvious from the right side. Descriptive names, short functions.
- Small focused files/classes (roughly 150 lines or less), split by responsibility. A class may
  span several `.cpp` files by topic (e.g. `app.cpp` + `app_menu.cpp`).
- RAII for handles (`std::unique_ptr` with custom deleters, see `samsung_client.h`, `osd_window.cpp`).
- Window classes use `WindowObject<T>()` from `win_util.h` to map `HWND` to the C++ object.
- Private window messages live in `app_constants.h` (`WM_APP + n`).
- When adding a source file, add it to `CMakeLists.txt` and to the "Project layout" table in `README.md`.

## Architecture

| File | Responsibility |
|---|---|
| `main.cpp` | Entry point, command-line parsing, single instance (named mutex). |
| `command_line.*` | `up`, `down`, `set N`, `show`, `exit`; forwarded to the running instance via `WM_COPYDATA`. |
| `app.*`, `app_menu.cpp` | Hidden main window: Raw Input, key auto-repeat, tray, settings, pairing. |
| `brightness_controller.*` | UI-thread logic: optimistic OSD updates; re-reads the real value after 10 s idle. |
| `brightness_worker.*` | Background thread for all network calls; merges queued requests (latest wins). Results return as `WM_APP_BRIGHTNESS_RESULT` with request ids, so stale results are ignored. |
| `brightness_transition.cpp` | Worker part that talks to the monitor: smooth transitions in up to 6 requests (~100 ms each, mostly monitor latency); a newer request interrupts and continues from the current value. |
| `brightness_scale.*` | Linear steps or a quadratic curve with the same number of steps (`0,1,2,5,8,13,18,25,32,41,50` for 50/5). Integer math on purpose: floating point once turned 25 into 24. Values between levels move to the nearest level in the key's direction. |
| `samsung_client.*`, `json_rpc.*` | WinHTTP JSON-RPC client with keep-alive. |
| `consumer_key_listener.*` | Standard HID brightness keys (Consumer page `0x0C`, usages `0x6F` / `0x70`). |
| `logitech_key_listener.*` | Logitech HID++ keys diverted by Logi Options+ (see below). |
| `osd_window.*` | Windows 11 style overlay (layered, no-activate, click-through), fades out. |
| `tray_icon.*`, `tray_menu.*`, `autostart.*` | Tray icon, context menu, `HKCU\...\Run` entry. |
| `config.*`, `paths.*` | `.env` next to the exe (created from a template on first start). |
| `pairing.*` | `createAccessToken` on a detached thread. |

## Samsung IP Control facts

- `POST https://<ip>:1516/`, JSON-RPC 2.0. Headers `Content-Type: application/json` **and**
  `Accept: application/json` are both required (missing `Accept` gives HTTP 400).
- Self-signed certificate: certificate errors are ignored on purpose (documented in README).
- `backlightControl` with `{"AccessToken": "..."}` reads; adding `"backlight": N` sets. Range 0–50 on
  the G85SB (`BRIGHTNESS_MAX`). `createAccessToken` (no params) needs confirmation on the monitor screen.
- Works without internet access. Response time is ~30–130 ms in the LAN.

## Logitech HID++ facts (Logi Options+)

- While `logioptionsplus_agent` runs, Options+ diverts special keys even on their default action. The
  keyboard then sends HID++ 2.0 long reports `11 <dev> <featIdx> 00 <CIDs...>` of feature
  `REPROG_CONTROLS_V4` (`0x1B04`) instead of standard codes. Brightness Down = CID `0x00C7`,
  Up = `0x00C8`; all zeros = released.
- The feature index differs per device (MX Keys S on Bolt: `0x09`; a mouse on Unifying: `0x0A`), so the
  listener asks each device with `IRoot.getFeature` (software id `0x0E`) and keeps state **per channel
  and device index**. Collections: receivers `0xFF00/0x0002`, Bluetooth `0xFF43/0x0202`, VID `0x046D`.
- Closing the Options+ window does not stop the agent. Keys then still arrive via HID++.

## Testing on real hardware

- The owner's install lives in `%LOCALAPPDATA%\Programs\SamsungBrightness\` (installed with the
  installer; `.env` there holds the IP and the access token).
- To update it: `SamsungBrightness.exe exit`, copy the new exe, start it again.
- Set `DEBUG_LOG=1` in that `.env` (then restart or "Reload settings") to get
  `samsung-brightness.log` next to the exe: settings, requests, key events (consumer usages and
  Logitech CIDs). Turn it back off afterwards.
- Test without a visible change: `SamsungBrightness.exe set <current value>`. Tests that change the
  brightness (e.g. `up` then `down`) should restore the original value at the end.
- Running `Remove-Item` in the same PowerShell call as `cmd.exe /c ...` gets blocked by the sandbox
  ("system path '/c'"); run the build and file cleanup as separate calls.
- Key presses need the owner. Ask them to press keys, then read the log.
- Re-measure resource usage after notable changes (private working set, private bytes, idle CPU
  over 60 s) and update the "Resource usage" table in `README.md`.

## Secrets

- **Never print, log, commit or copy the access token.** `.env` and `*.log` are git-ignored; secret
  scanning with push protection is enabled on GitHub. Do not put real IPs or tokens into tracked files
  (`.env.example` uses an example IP and an empty token).

## Git and GitHub

- Repository: https://github.com/OldValencia/samsung-odyssey-brightness (public), branch `main`.
- Commit or push only when the owner asks. Commits are authored as
  `--author="Claude <noreply@anthropic.com>"`; do not add `Co-Authored-By` trailers.
- Rulesets: "Protect main" (no deletion, no force push, applies to everyone); "Require pull requests"
  (PR + passing `build` check; the repository admin may bypass and push to `main` directly);
  "Protect release tags" (`v*` tags cannot be moved or deleted).
- CI: `.github/workflows/build.yml` builds the exe, the Inno Setup installer (`installer/`) and a
  portable zip on every push/PR. Pushing a tag `vX.Y.Z` publishes a GitHub Release with both files.
  Releases are made only when the owner asks.
- `gh` is not installed and the owner prefers not to install one-off tools locally. For GitHub API
  calls the token from Git Credential Manager (`git credential fill`) may be used **only after the
  owner agrees in the current session**, and must never be printed or saved.
