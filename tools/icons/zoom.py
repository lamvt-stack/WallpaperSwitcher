from PIL import Image, ImageDraw, ImageFont
import glob, sys
THEME = sys.argv[1] if len(sys.argv) > 1 else 'dark'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'zoom.png'
sizes = [16, 24, 32, 48, 64]
zoom = {16: 12, 24: 8, 32: 6, 48: 4, 64: 3}
font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 13)
W = sum(s * zoom[s] for s in sizes) + 20 * (len(sizes) + 1)
H = 64 * 3 + 2 * 70 + 40
img = Image.new('RGB', (W, 2 * (64 * 3 + 50) + 20), (238, 241, 248))
d = ImageDraw.Draw(img)
# row 2 dark background
d.rectangle((0, 64 * 3 + 50 + 10, W, img.size[1]), fill=(14, 17, 24))
for row, bg in enumerate([(238, 241, 248), (14, 17, 24)]):
    x = 20; y0 = row * (64 * 3 + 60) + 12
    for s in sizes:
        im = Image.open(f'multi/glyph-{THEME}-{s}.png').convert('RGBA')
        z = im.resize((s * zoom[s], s * zoom[s]), Image.NEAREST)
        img.paste(z, (x, y0), z)
        # actual size
        img.paste(im, (x, y0 + 64 * 3 + 6), im)
        d.text((x + s * zoom[s] // 2 - 10, y0 + 64 * 3 + 6 + s + 2), f'{s}', fill=(120, 126, 145), font=font)
        x += s * zoom[s] + 20
img.save(OUT); print(img.size)
