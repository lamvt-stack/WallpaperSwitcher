// The tray application: hidden window, tray icon, menu, hotkeys, timer and the glue that
// turns "next wallpaper" into Playlist + desktop + fade calls.
#pragma once

#include "win_util.h"   // first: defines the Windows environment the SDK headers below rely on

#include <shellapi.h>

#include <string>

#include "config.h"
#include "image_library.h"

namespace ws {

class App {
public:
    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Creates the hidden window, tray icon and hotkeys.
    bool Create(HINSTANCE instance);
    // First scan + first wallpaper change (or the folder prompt on first run) and the auto-change timer.
    void Start();
    // Message loop; returns the process exit code.
    int Run();

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    // tray / icon
    void AddTrayIcon();
    void ApplyIcon(bool force);
    int  IconResourceId() const;
    void SetTooltip(const std::wstring& text);
    void OnTrayEvent(WPARAM wp, LPARAM lp);

    // menu / commands
    void ShowMenu(POINT anchor);
    void OnCommand(UINT id);
    bool PickFolder(std::wstring& out);
    void ChooseFolder();

    // wallpaper
    void Rescan();
    void Step(int dir, int only = -1);
    void StepImpl(int dir, int only);
    void RefreshCurrent();
    void ApplyTimer();
    bool PerMonitorMode() const;

    HINSTANCE     instance_ = nullptr;
    HWND          hwnd_ = nullptr;
    Config        cfg_;
    ImageLibrary  library_;
    NOTIFYICONDATAW nid_ = {};
    UINT          taskbarCreated_ = 0;
    int           iconResId_ = 0;
    bool          trayV4_ = false;
    bool          hotkeysOk_ = true;
    bool          haveShown_ = false;   // some wallpaper has been set by this session
    bool          stepping_ = false;    // re-entrancy guard for Step()
    bool          modal_ = false;       // a menu / dialog is open: the timer must not change wallpapers
};

}  // namespace ws
