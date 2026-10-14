# Icon tools

Generates the application icons (Glyph design, dark and light).

```bash
pip install -r requirements.txt
python build_multi.py        # writes multi/WallpaperSwitcher-dark.ico, -light.ico (+ per-size PNGs)
python zoom.py dark zoom.png # magnified preview of the small sizes
```

`make_icons.py` holds the artwork (all six explored designs); `build_multi.py` renders the 256 / 128 px images from the
master artwork and the 64 / 48 / 32 / 24 / 16 px images from pixel-aligned, hand-tuned geometry, then writes the `.ico`
container (PNG for 256 px, 32-bit DIB for smaller sizes). Copy the two `.ico` files to `resources/icons/` to update the app.
