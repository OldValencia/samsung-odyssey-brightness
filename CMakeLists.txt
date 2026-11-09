cmake_minimum_required(VERSION 3.25)

project(SamsungBrightness VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the C++ runtime statically so the exe runs without the Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(SamsungBrightness WIN32
    src/main.cpp
    src/app.cpp
    src/app_menu.cpp
    src/autostart.cpp
    src/brightness_controller.cpp
    src/brightness_worker.cpp
    src/command_line.cpp
    src/config.cpp
    src/consumer_key_listener.cpp
    src/json_rpc.cpp
    src/log.cpp
    src/logitech_key_listener.cpp
    src/osd_window.cpp
    src/pairing.cpp
    src/paths.cpp
    src/samsung_client.cpp
    src/text.cpp
    src/tray_icon.cpp
    src/tray_menu.cpp
    resources/app.rc
    resources/app.manifest
)

target_include_directories(SamsungBrightness PRIVATE src resources)

target_compile_definitions(SamsungBrightness PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_options(SamsungBrightness PRIVATE /W4 /permissive- /utf-8)

target_link_libraries(SamsungBrightness PRIVATE
    winhttp   # HTTPS requests to the monitor
    hid       # parsing keyboard HID reports
    shcore    # per-monitor DPI
)
