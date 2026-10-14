# Changelog

## 1.0.0 - initial release

- Tray wallpaper switcher: next / previous, hotkeys, timer, shuffle, six picture positions.
- Multi-monitor modes (same image, different image per monitor, span) with a distinct image on every display.
- Orientation matching using the EXIF Orientation tag, with pixel-size fallback.
- Smart crop for portrait monitors (detail / contrast / XMP face regions), cached as PNG.
- 500 ms cross-fade behind the desktop icons (smoothstep easing, vblank-paced).
- Lazy, bounded-cost image classification: fast start-up on very large folders.
- Dark / light / follow-Windows-theme tray icon with hand-tuned 16-256 px sizes.
- Hardening from the initial code review (see `docs/CODE_REVIEW.md`): safe DLL loading, RAII for every handle,
  exception barriers at OS callbacks, checked Win32/COM calls with diagnostics, bounded image dimensions.
