# Architecture

## Layers

```
 main.cpp ── wWinMain: hardening, single instance, COM apartment, exception barrier
    │
  app.cpp ── App: hidden window, tray icon (NOTIFYICON_VERSION_4), popup menu, hotkeys, timer
    │          Step(): one wallpaper change = the sequence below
    ├── image_library ── folder listing + session cache  ──►  core/Playlist
    ├── desktop        ── IDesktopWallpaper wrapper, monitors, slots, legacy fallback
    ├── smart_crop     ── crop to a display's shape (WIC) + PNG cache  ──►  core/crop_core
    ├── fade           ── cross-fade overlays behind the desktop icons   ──►  core/fade_math
    ├── wic            ── WIC factory, EXIF orientation, header-only probing
    ├── config         ── registry persistence (HKCU), run-at-startup
    └── win_util       ── RAII owners, checked calls, Format(), ModulePath(), LoadSystemLibrary()

 core/ (no Windows types, unit-tested on any OS)
    playlist.h/.cpp  ── Playlist: lazy classification, play order, per-display cursors, distinct-image selection
    crop_core.h/.cpp ── subject-aware crop window, XMP face-region parsing
    fade_math.h      ── fade curve, picture placement (Fill / Fit / ...)
    numeric.h        ── saturating rounding and clamps
```

## One wallpaper change (`App::StepImpl`)

1. `DesktopWallpaper::Open()` + `Monitors()`: the attached displays, left to right.
2. `BuildSlots()`: which displays share an image (a *slot*): the whole desktop for Span, one per monitor in
   "different image" mode, otherwise one per orientation group.
3. `Playlist::Run(slots, dir, only, apply)`:
   - displays that keep their image reserve it; the others pick the next image (in play order) that suits their
     orientation and is not already on another display - reading at most 16 unread images per display;
   - `apply(slot, file)` is called for every chosen image.
4. In `apply`: portrait display and smart crop on -> `MakeSmartCrop()` (cached PNG); `FadeSession::Begin()` covers
   the old picture; `ApplyToSlot()` sets the new wallpaper.
5. `FadeSession::Run()` dissolves the covers away (500 ms), then removes them.

## Lazy image index

Scanning a folder only lists names (+ size, timestamp). `Playlist` classifies a file (landscape / portrait,
via a callback) the first time the play order reaches it, or when the menu opens (3 random files). Results are
cached for the session by path + size + timestamp, so "Rescan folder" is cheap.

## Threading model

Single-threaded: everything runs on the UI thread in a single-threaded COM apartment, so there are no locks.
Re-entrancy is the hazard instead (modal menu / dialog loops dispatch timer and hotkey messages): `Step()` has a
re-entrancy guard and the timer is ignored while a menu or dialog is open. Heavy work (classification, cropping,
the fade loop) runs on this thread too - moving it to a worker is the main item on the roadmap.

## Error handling

- Every handle has an RAII owner; nothing is closed by hand.
- Win32 calls go through `Check()`, HRESULTs through `Succeeded()`; failures are logged with `OutputDebugStringW`.
- Exceptions never cross OS frames: the window procedure thunk, the overlay window procedure and `wWinMain`
  catch everything.
- Features degrade instead of failing: no `IDesktopWallpaper` -> legacy `SystemParametersInfo`; no wallpaper
  layer found -> instant switch (with a one-minute back-off); no WIC -> images are not filtered by orientation.

## Security notes

- DLLs load from System32 only (`SetDefaultDllDirectories`, `LoadSystemLibrary`).
- Image headers, metadata and XMP are treated as untrusted: sizes are saturated and capped, face counts are
  bounded, XMP is read from the first 256 KB only.
- The manifest requests `asInvoker`; the app never elevates.
