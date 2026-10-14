// User settings, stored under HKCU\Software\WallpaperSwitcher.
#pragma once

#include <string>

namespace ws {

enum class MonitorMode : int { Same = 0, Different = 1 };
enum class IconChoice : int { Dark = 0, Light = 1, Auto = 2 };   // Auto follows the Windows theme

constexpr int kIntervalCount = 7;       // entries of the "Change every" menu
constexpr int kStyleCount = 6;          // Fill, Fit, Stretch, Center, Tile, Span
constexpr int kDefaultIntervalIdx = 3;  // 15 minutes

struct Config {
    std::wstring folder;
    int         intervalIdx = kDefaultIntervalIdx;
    int         styleIdx    = 0;                      // Fill
    MonitorMode monitorMode = MonitorMode::Same;
    bool        shuffle     = true;
    bool        smartCrop   = true;                   // subject-aware crop on portrait monitors
    bool        fade        = true;                   // cross-fade between wallpapers
    IconChoice  icon        = IconChoice::Dark;
};

// Missing or out-of-range values fall back to the defaults.
Config LoadConfig();
bool SaveConfig(const Config& cfg);

// "Run at startup" (HKCU\...\Run). IsRunAtStartup() is true only when the entry points at the
// currently running executable.
bool IsRunAtStartup();
bool SetRunAtStartup(bool enable);

}  // namespace ws
