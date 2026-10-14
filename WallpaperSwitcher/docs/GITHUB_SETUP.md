# First check-in

```bash
git init -b main
git add -A
git commit -m "Initial commit: Wallpaper Switcher 1.0.0

Tray wallpaper switcher for Windows (C++17, Win32): multi-monitor modes, orientation
matching, smart crop for portrait displays, cross-fade, lazy image indexing,
dark/light icons. Portable core with unit tests, CMake build, CI."
git remote add origin https://github.com/<you>/<repo>.git
git push -u origin main
```

Suggested repository settings:

- **Actions**: enabled; the workflow in `.github/workflows/ci.yml` builds with MSVC and MinGW-w64 and runs the tests.
- **Branch protection** on `main`: require the three CI jobs to pass.
- **Topics**: `windows`, `win32`, `cpp17`, `wallpaper`, `multi-monitor`, `system-tray`.
- **Releases**: tag `v1.0.0`, attach `WallpaperSwitcher.exe` from the CI artifact (binaries are intentionally not committed;
  `.gitignore` excludes `*.exe`). Consider code-signing release binaries, because unsigned executables trigger SmartScreen.
- **License**: MIT is included as a default; change `LICENSE` (and the copyright holder) if you prefer another.
