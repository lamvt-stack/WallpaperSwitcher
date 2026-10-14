// Cross-fade between wallpapers.
//
// The old picture of each display that is about to change is drawn into a layered child
// window of Explorer's wallpaper layer (the "WorkerW" behind the desktop icons), the new
// wallpaper is set underneath it, and the overlay's opacity is then animated from 255 to 0.
// Nothing is ever shown above icons or other windows. If the wallpaper layer cannot be found,
// or anything else fails, the wallpaper simply changes instantly.
#pragma once

#include <string>
#include <vector>

#include "desktop.h"
#include "win_util.h"

namespace ws {

// Registers the overlay window class; call once at startup.
bool RegisterFadeWindowClass(HINSTANCE instance);

// Forget a recent "could not find the wallpaper layer" result (call when Explorer restarts).
void ResetFadeBackoff();

// Lives for one wallpaper change: Begin() is called right before a slot's new image is set,
// Run() once every slot has been updated. Destruction removes any cover still on screen.
class FadeSession {
public:
    FadeSession(const DesktopWallpaper& dw, std::vector<MonitorInfo> monitors, bool enabled);
    FadeSession(const FadeSession&) = delete;
    FadeSession& operator=(const FadeSession&) = delete;
    ~FadeSession();

    // Covers the displays `slot` drives with their current picture (skipping displays whose
    // picture is not actually changing to `newPath`).
    void Begin(const Slot& slot, const std::wstring& newPath);

    // Dissolves the covers away over kFadeDurationMs, then removes them.
    void Run();

private:
    struct Overlay {
        std::wstring id;
        UniqueBitmap bitmap;     // declared before hwnd: the window is destroyed first
        UniqueHwnd   hwnd;
    };

    bool EnsureHost();
    void Clear();
    bool Covered(const std::wstring& id) const;

    const DesktopWallpaper&  dw_;
    std::vector<MonitorInfo> monitors_;
    bool                     enabled_;
    HWND                     host_ = nullptr;
    bool                     hostTried_ = false;
    std::vector<Overlay>     overlays_;
};

}  // namespace ws
