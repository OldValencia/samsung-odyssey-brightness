# Samsung Brightness

A tiny Windows tray app that controls the **hardware brightness** of Samsung Tizen-based monitors
(such as the Odyssey OLED G8) **over the local network**. Use it with your keyboard's brightness keys
and see a Windows 11 style on-screen indicator.

> [!NOTE]
> **This project is fully AI-generated.** All code, documentation and commits in this repository
> were written by an AI assistant (Claude by Anthropic) under the direction of the repository owner.
> It was tested on real hardware, but review it as you would any third-party code.

## Why

Some Samsung smart/gaming monitors, such as the Odyssey OLED G8 (G85SB), **do not support DDC/CI**.
Tools such as Twinkle Tray, Monitorian or PowerToys cannot change their brightness, and Windows has
no brightness slider for external monitors anyway.

These monitors run Tizen and include the **Samsung IP Control** server: a JSON-RPC API over HTTPS
in your local network. Its `backlightControl` method reads and changes the real panel brightness.
This app wraps that API in a lightweight tray utility.

The monitor does **not** need internet access; LAN access is enough.

## Features

- **Brightness keys**: works with the standard HID *Brightness Up / Down* keys of any keyboard.
  Holding a key repeats it.
- **On-screen display**: a Windows 11 style indicator shows the level after every change.
- **Tray icon**: the tooltip shows the current brightness. Left-click shows the indicator.
  Right-click opens a menu with presets, autostart and settings.
- **Command line**: `SamsungBrightness.exe up`, `down`, `set 25`, `show`, `exit`.
  Bind these to any key or macro tool.
- **Start with Windows**: one checkbox in the tray menu.
- **Light on resources**: a single native exe (C++ / Win32, no frameworks or runtimes).
  It uses about 4 MB of RAM and no CPU while idle (see [Resource usage](#resource-usage)).
- **Responsive**: keeps the HTTPS connection alive, merges fast key presses, never blocks the UI.

## Tested with

| Monitor | Model code | Firmware |
|---|---|---|
| Samsung Odyssey OLED G8 (G85SB), 34" | LS34BG850SUXEN | 1720 |

| Keyboard | Connection | Result |
|---|---|---|
| Logitech MX Keys S, Logi Options+ running | Bolt receiver | Works (HID++ diverted keys) |

Other Tizen-based Samsung monitors with "IP Remote" will probably work too. The brightness range
may differ; set `BRIGHTNESS_MAX` accordingly. Reports are welcome.

## Setup

1. **On the monitor**: connect it to your network and enable **IP Remote** in its settings.
2. **In your router** *(recommended)*: reserve a fixed IP address for the monitor (DHCP reservation),
   so the address does not change.
3. **Install**: put `SamsungBrightness.exe` into any folder, for example
   `%LOCALAPPDATA%\Programs\SamsungBrightness`, and start it.
4. **First start**: the app creates a `.env` settings file next to the exe and opens it.
   Enter the monitor IP address in `SAMSUNG_MONITOR_IP`, save the file, and choose
   **Reload settings** in the tray menu.
5. **Pairing**: when the app asks, choose **Yes**, or use **Pair with monitor...** in the tray menu.
   Accept the request **on the monitor screen**. The access token is saved to `.env` automatically.
6. **Autostart**: tick **Start with Windows** in the tray menu.

## Keyboard

The app reads keyboard input with Windows Raw Input. It installs no keyboard hooks and needs no special
rights. It recognizes brightness keys in two ways:

1. **Standard HID keys** (*Consumer* page `0x0C`, usages `0x6F` Brightness Increment and
   `0x70` Brightness Decrement). Most keyboards send these.
2. **Logitech keyboards with Logi Options+** (tested: MX Keys S on a Bolt receiver). Options+
   *diverts* the special keys: the keyboard then sends Logitech HID++ notifications
   (feature `REPROG_CONTROLS_V4`, control IDs `0x00C7` Brightness Down and `0x00C8` Brightness Up)
   instead of standard codes. The app asks each Logitech keyboard once where this feature is and
   then listens for those notifications. Receivers (Bolt, Unifying) and Bluetooth are supported.

What to do:

- **Most keyboards**: nothing to configure. Press the brightness keys.
- **Logitech keyboards with Logi Options+**: keep the brightness keys on their default action
  (*Brightness up / down*). If a key gets a custom action, the app does not see it.
- **Keyboards without brightness keys**, or keys that send something else: bind any key in your
  keyboard software (Logi Options+, a macro tool, AutoHotkey...) to run
  `SamsungBrightness.exe up` and `SamsungBrightness.exe down`. Each call passes the command to the
  running instance and exits within milliseconds.

To check what your keyboard sends, set `DEBUG_LOG=1` and look at `samsung-brightness.log`
after pressing a key.

## Settings (`.env`)

| Key | Default | Description |
|---|---|---|
| `SAMSUNG_MONITOR_IP` | — | IP address of the monitor |
| `SAMSUNG_ACCESS_TOKEN` | — | Filled in by pairing |
| `SAMSUNG_MONITOR_PORT` | `1516` | Port of the IP Control server |
| `BRIGHTNESS_STEP` | `5` | Change per key press |
| `BRIGHTNESS_MAX` | `50` | Highest brightness the monitor accepts (50 on the Odyssey OLED G8) |
| `DEBUG_LOG` | `0` | `1` writes `samsung-brightness.log` next to the exe |

See [`.env.example`](.env.example). After editing the file, choose **Reload settings** in the tray menu.

## Command line

```text
SamsungBrightness.exe            start in the tray (or show the level if already running)
SamsungBrightness.exe up         increase brightness by one step
SamsungBrightness.exe down       decrease brightness by one step
SamsungBrightness.exe set 25     set brightness to 25
SamsungBrightness.exe show       show the current brightness
SamsungBrightness.exe exit       close the running instance
```

## How it works

```text
Keyboard brightness key ──(HID 0x0C/0x6F, 0x70)──► Raw Input ─┐
Logitech + Options+ ──(HID++ CID 0x00C7, 0x00C8)──► Raw Input ─┤
SamsungBrightness.exe up/down ──(WM_COPYDATA)─────────────────┤
Tray menu ────────────────────────────────────────────────────┤
                                                               ▼
                                           BrightnessController (UI thread)
                                           shows the OSD, decides what to send
                                                               │
                                                               ▼
                                           BrightnessWorker (background thread)
                                           merges requests, keeps the connection alive
                                                               │  HTTPS, JSON-RPC, LAN
                                                               ▼
                                           Monitor: backlightControl (0–50)
```

Protocol example (`POST https://<monitor-ip>:1516/`, headers `Content-Type` and `Accept: application/json`):

```json
{"jsonrpc":"2.0","id":"1","method":"backlightControl","params":{"AccessToken":"<token>","backlight":25}}
```

Without `"backlight"` the method only reads the current value. `createAccessToken` (no params) starts pairing.

## Resource usage

Measured on Windows 11 (x64) after 5 minutes of use, including key presses and tray menu use:

| Metric | Value |
|---|---|
| RAM, as shown in Task Manager (private working set) | **3.8 MB** |
| Committed memory (private bytes) | 4.5 MB |
| CPU while idle (measured over 60 s) | **0 ms** |
| CPU in total for 5 minutes of use | 0.34 s |
| Threads | 7 (2 own, the rest are Windows networking thread pool) |
| Executable size | 835 KB, no dependencies |

While idle, the app does no polling and has no timers. It sleeps until a key press, a tray click or
a command arrives. Most of the remaining memory belongs to Windows components every tray app with
HTTPS needs (the notification area API, WinHTTP and the TLS stack). The app's own data is a few
kilobytes.

## Security notes

- The monitor uses a **self-signed TLS certificate**, so the app **does not verify the certificate**.
  The traffic is encrypted but not authenticated. Use it only in a trusted home network.
- The access token is stored as plain text in `.env` next to the exe. Anyone who can read the file
  can control the monitor's IP Remote functions. Do not share or commit it (`.gitignore` excludes it).
- To revoke access, disable IP Remote or reset the network settings on the monitor.

## Limitations

- Windows itself still does not know the monitor's brightness: there is no slider in Quick Settings.
  Windows offers no supported way to add one for an external monitor without the graphics driver's help.
- The on-screen display appears on the **primary** monitor. It cannot be shown over games running in
  exclusive fullscreen mode (borderless windowed works).

## Building from source

Requirements: Windows 10/11 x64, Visual Studio 2022 or newer with the **Desktop development with C++**
workload, which includes CMake and Ninja.

```bat
build.cmd
```

The result is `build\SamsungBrightness.exe`. The C++ runtime is linked statically, so the exe
needs no redistributable.

Project layout:

| File | Responsibility |
|---|---|
| `src/main.cpp` | Entry point, single instance, forwarding commands to the running instance |
| `src/app.cpp`, `src/app_menu.cpp` | Hidden main window: message handling, keys, tray menu, settings, pairing |
| `src/brightness_controller.*` | Brightness logic on the UI thread (optimistic updates, OSD) |
| `src/brightness_worker.*` | Background thread for network calls, request merging |
| `src/samsung_client.*` | Samsung IP Control client (WinHTTP) |
| `src/json_rpc.*` | Minimal JSON-RPC helpers |
| `src/consumer_key_listener.*` | Standard HID brightness keys (Raw Input) |
| `src/logitech_key_listener.*` | Logitech HID++ brightness keys diverted by Logi Options+ |
| `src/osd_window.*` | On-screen display |
| `src/tray_icon.*`, `src/tray_menu.*` | Notification area icon and menu |
| `src/config.*`, `src/paths.*` | `.env` settings |
| `src/autostart.*` | "Start with Windows" |
| `src/pairing.*` | Getting an access token from the monitor |
| `tools/make-icon.ps1` | Regenerates `resources/app.ico` with ImageMagick |

## Credits

The Samsung IP Control API was documented by the community, for example in the
[BetterDisplay discussion](https://github.com/waydabber/BetterDisplay/issues/5861).

## Disclaimer

This is an unofficial project and is not affiliated with or endorsed by Samsung.
"Samsung" and "Odyssey" are trademarks of their respective owners.

## License

[MIT](LICENSE)
