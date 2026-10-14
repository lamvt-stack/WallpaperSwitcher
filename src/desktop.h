// The Windows desktop wallpaper: IDesktopWallpaper wrapper, monitor list, wallpaper "slots"
// and the legacy single-monitor fallback.
#pragma once

#include <shobjidl.h>

#include <string>
#include <vector>

#include "config.h"
#include "core/playlist.h"
#include "win_util.h"

namespace ws {

struct MonitorInfo {
    std::wstring id;
    RECT         rc;
};

// One entry of the "Picture position" menu.
struct StyleDef {
    const wchar_t*             label;
    DESKTOP_WALLPAPER_POSITION pos;
    const wchar_t*             regStyle;   // legacy registry values (fallback path only)
    const wchar_t*             regTile;
};
const StyleDef& GetStyle(int index);       // index is clamped to the valid range

// Landscape (width >= height) or portrait, as an Orient value.
int OrientationOfRect(const RECT& rc);

// Thin wrapper over IDesktopWallpaper (Windows 8+). Every method is safe to call on a
// wrapper that failed to open: it just returns false / empty.
class DesktopWallpaper {
public:
    bool Open();                                        // creates the COM object; false if unavailable
    bool IsOpen() const { return static_cast<bool>(dw_); }

    // Attached monitors ordered left-to-right then top-to-bottom, at most kMaxMonitors.
    std::vector<MonitorInfo> Monitors() const;

    bool SetPosition(DESKTOP_WALLPAPER_POSITION pos);
    bool GetPosition(DESKTOP_WALLPAPER_POSITION& pos) const;
    bool SetWallpaper(const wchar_t* monitorIdOrNull, const std::wstring& path);   // nullptr = all monitors
    std::wstring GetWallpaper(const std::wstring& monitorId) const;
    COLORREF BackgroundColor() const;

private:
    ComPtr<IDesktopWallpaper> dw_;
};

// Decide which displays share an image:
//   Span position            -> one slot for the whole desktop
//   Different image/monitor  -> one slot per monitor
//   otherwise                -> one slot per orientation group (landscape / portrait)
std::vector<Slot> BuildSlots(bool haveDesktop, const std::vector<MonitorInfo>& mons, MonitorMode mode, int styleIdx);

// Puts `path` on the display(s) of `slot`. Uses the legacy API when the COM wrapper is closed.
bool ApplyToSlot(DesktopWallpaper& dw, const Slot& slot, const std::wstring& path, int styleIdx);

// SystemParametersInfo fallback (single wallpaper for all monitors).
bool SetWallpaperLegacy(const std::wstring& path, int styleIdx);

}  // namespace ws
