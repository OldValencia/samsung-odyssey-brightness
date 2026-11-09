#pragma once

// "Start with Windows" via HKCU\Software\Microsoft\Windows\CurrentVersion\Run (no admin rights needed).
namespace autostart {

// True when the Run entry exists and points to this executable.
bool IsEnabled();

bool SetEnabled(bool enabled);

}
