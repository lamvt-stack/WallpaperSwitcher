#!/usr/bin/env python3
"""Glyph Light, multi-size .ico: 256/128 come from the master artwork, 64/48/32/24/16 are
redrawn by hand-tuned, pixel-aligned geometry (thicker strokes, fewer details)."""
import io, struct, sys
from PIL import Image, ImageDraw, ImageChops
sys.path.insert(0, '.')
import make_icons as mi

THEMES = {
    'light': dict(th=mi.LIGHT, ink=mi.hx('#1B2033'), border_rgb=(20, 24, 40), border_k=1.0, shadow_k=1.0),
    'dark':  dict(th=mi.DARK,  ink=mi.hx('#F3F5FF'), border_rgb=(255, 255, 255), border_k=0.95, shadow_k=1.7),
}
PINK_STOPS = [(0, mi.hx('#FF4F9A')), (0.55, mi.hx('#FF8A5B')), (1, mi.hx('#FFD36B'))]
AQUA_STOPS = [(0, mi.ACC_AQUA), (1, mi.hx('#2F78FF'))]


# ---- per-size geometry, all in TARGET PIXELS ---------------------------------
# tile: (x0, y0, x1, y1, radius); mon: outer box of the outline monitor + stroke + radius;
# neck / base: stand rectangles; mount: mountain polygon (None = omitted);
# port: portrait panel box + radius; pad/pad_r: knockout ring around it;
# sun: (cx, cy, r); hill: polygon relative to the portrait's top-left.
SPECS = {
    64: dict(ss=8, tile=(3, 3, 61, 61, 13), tile_shadow=(1.0, 1.2, 0.20), border=(0.8, 40),
             mon=(9, 17, 39, 37, 3, 4.5), neck=(22, 36, 26, 40), base=(16, 40, 32, 42.5, 1.2),
             mount=[(15.5, 32), (20.5, 24), (23.5, 27.5), (27, 22), (31.5, 32)],
             port=(35, 13, 53, 45, 4.5), pad=2.2, pad_r=7,
             sun=(44, 24.5, 3.6), hill=[(0, 32), (0, 22.7), (7, 17.1), (18, 26.3), (18, 32)],
             hill_a=170),
    48: dict(ss=8, tile=(2, 2, 46, 46, 10), tile_shadow=(0.8, 0.9, 0.18), border=(0.7, 45),
             mon=(7, 15, 29, 30, 3, 3.5), neck=(16.5, 29, 19.5, 32), base=(12.5, 32, 22.5, 34, 1.0),
             mount=[(11.5, 28), (15, 22.5), (17, 25), (19.5, 21), (22.5, 28)],
             port=(26, 12, 39, 35, 3.6), pad=2, pad_r=5.5,
             sun=(32.5, 20.7, 2.6), hill=[(0, 23), (0, 16.3), (5, 12.2), (13, 18.9), (13, 23)],
             hill_a=185),
    32: dict(ss=16, tile=(1, 1, 31, 31, 7), tile_shadow=None, border=(1, 46),
             mon=(3, 8, 20, 20, 2, 2.5), neck=(9, 19, 12, 22.5), base=(6, 22, 14.5, 23.5, 0.7),
             mount=[(6.5, 17.5), (9.5, 13), (11, 15), (13.3, 11.8), (14.8, 17.5)],
             port=(17, 5, 29, 27, 3), pad=1.5, pad_r=4.2,
             sun=(23, 10.5, 2), hill=[(0, 22), (0, 17), (4, 14), (12, 18), (12, 22)],
             hill_a=210),
    24: dict(ss=16, tile=(1, 1, 23, 23, 5.5), tile_shadow=None, border=(1, 46),
             mon=(2, 6, 15, 15, 2, 1.5), neck=(6, 14.5, 8, 17), base=(4, 17, 10, 18.5, 0.5),
             mount=[(5.2, 13), (7.5, 9.5), (8.8, 11), (10.6, 8.8), (11.6, 13)],
             port=(13, 3, 22, 20, 2.2), pad=1, pad_r=3,
             sun=(17.5, 7.5, 1.5), hill=[(0, 17), (0, 13.5), (3, 11.2), (9, 14), (9, 17)],
             hill_a=220),
    16: dict(ss=16, tile=(0, 0, 16, 16, 4), tile_shadow=None, border=(1, 52),
             mon=(2, 4, 9, 11, 1, 0.6), neck=(5, 10.5, 6, 12), base=(3, 12, 8, 13, 0),
             mount=None,
             port=(9, 2, 14, 13, 1.2), pad=1, pad_r=1.6,
             sun=(11.5, 4.5, 0.75), hill=[(0, 11), (0, 9), (2, 7), (5, 8.5), (5, 11)],
             hill_a=230),
}


def render_small(N, sp, theme='light'):
    T = THEMES[theme]
    INK = T['ink']
    ss = sp['ss']
    SZ = N * ss
    q = lambda v: v * ss
    cv = Image.new('RGBA', (SZ, SZ), (0, 0, 0, 0))

    def rrect(d, box, r, **kw):
        x0, y0, x1, y1 = box
        d.rounded_rectangle((q(x0), q(y0), q(x1) - 1, q(y1) - 1), max(0, int(q(r))), **kw)

    def rmask(box, r):
        m = Image.new('L', cv.size, 0)
        rrect(ImageDraw.Draw(m), box, r, fill=255)
        return m

    # tile (+ optional soft shadow)
    tx0, ty0, tx1, ty1, tr = sp['tile']
    tile_m = rmask((tx0, ty0, tx1, ty1), tr)
    if sp['tile_shadow']:
        dy, blur, a = sp['tile_shadow']
        sm = Image.new('L', cv.size, 0)
        sm.paste(tile_m, (0, int(q(dy))))
        from PIL import ImageFilter
        sm = sm.filter(ImageFilter.GaussianBlur(q(blur)))
        mi.composite(cv, Image.new('RGBA', cv.size, (0, 0, 0, 255)), sm, alpha=min(1.0, a * T['shadow_k']))
    mi.composite(cv, mi.gradient(SZ, SZ, T['th']['tile'], 60), tile_m)
    bw, ba = sp['border']
    bl = Image.new('RGBA', cv.size, (0, 0, 0, 0))
    rrect(ImageDraw.Draw(bl), (tx0, ty0, tx1, ty1), tr, outline=T['border_rgb'] + (min(255, int(ba * T['border_k'])),), width=max(1, int(q(bw))))
    cv.alpha_composite(bl)

    # art layer: outline monitor, mountain, stand
    art = Image.new('RGBA', cv.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(art, 'RGBA')
    mx0, my0, mx1, my1, ms, mr = sp['mon']
    rrect(d, (mx0, my0, mx1, my1), mr, outline=INK + (255,), width=int(q(ms)))
    nx0, ny0, nx1, ny1 = sp['neck']
    d.rectangle((q(nx0), q(ny0), q(nx1) - 1, q(ny1) - 1), fill=INK + (255,))
    bx0, by0, bx1, by1, br = sp['base']
    rrect(d, (bx0, by0, bx1, by1), br, fill=INK + (255,))
    if sp['mount']:
        pts = sp['mount']
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        ox, oy = min(xs), min(ys)
        w_, h_ = int(q(max(xs) - ox)) + 1, int(q(max(ys) - oy)) + 1
        fill = mi.gradient(w_, h_, AQUA_STOPS, 45)
        mm = Image.new('L', (w_, h_), 0)
        ImageDraw.Draw(mm).polygon([(q(x - ox), q(y - oy)) for x, y in pts], fill=255)
        mi.composite(art, fill, mm, pos=(int(q(ox)), int(q(oy))))

    # knockout ring around the portrait panel
    px0, py0, px1, py1, pr = sp['port']
    pad = sp['pad']
    ring = rmask((px0 - pad, py0 - pad, px1 + pad, py1 + pad), sp['pad_r'])
    art.putalpha(ImageChops.multiply(art.getchannel('A'), ring.point(lambda v: 255 - v)))

    # portrait panel: gradient + sun + hill
    pw, ph = px1 - px0, py1 - py0
    gw, gh = int(q(pw)), int(q(ph))
    pan = mi.gradient(gw, gh, PINK_STOPS, 70)
    pd = ImageDraw.Draw(pan, 'RGBA')
    sx, sy, sr = sp['sun']
    sx -= px0; sy -= py0
    pd.ellipse((q(sx - sr), q(sy - sr), q(sx + sr), q(sy + sr)), fill=(255, 250, 235, 255))
    pd.polygon([(q(x), q(y)) for x, y in sp['hill']], fill=(110, 40, 140, sp['hill_a']))
    pm = Image.new('L', (gw, gh), 0)
    ImageDraw.Draw(pm).rounded_rectangle((0, 0, gw - 1, gh - 1), int(q(pr)), fill=255)
    mi.composite(art, pan, pm, pos=(int(q(px0)), int(q(py0))))
    cv.alpha_composite(art)

    flt = Image.LANCZOS if N >= 48 else Image.BOX
    return cv.convert('RGBa').resize((N, N), flt).convert('RGBA')


def master_sizes(theme='light'):
    cv = mi.d_glyph(THEMES[theme]['th'])                       # 1024 px master
    pre = cv.convert('RGBa')
    return {256: pre.resize((256, 256), Image.LANCZOS).convert('RGBA'),
            128: pre.resize((128, 128), Image.LANCZOS).convert('RGBA')}


# ---- ICO container (PNG for 256, 32-bit DIB for smaller sizes: widest compatibility) -------
def dib_bytes(img):
    w, h = img.size
    px = img.tobytes('raw', 'BGRA')
    rows = [px[y * w * 4:(y + 1) * w * 4] for y in range(h - 1, -1, -1)]    # bottom-up
    mask_stride = ((w + 31) // 32) * 4
    andmask = b'\x00' * (mask_stride * h)
    hdr = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0, w * h * 4 + len(andmask), 0, 0, 0, 0)
    return hdr + b''.join(rows) + andmask


def write_ico(path, images):
    entries = []
    for size in sorted(images):
        img = images[size]
        if size >= 256:
            b = io.BytesIO(); img.save(b, 'PNG', optimize=True); data = b.getvalue()
        else:
            data = dib_bytes(img)
        entries.append((size, data))
    out = struct.pack('<HHH', 0, 1, len(entries))
    off = 6 + 16 * len(entries)
    blob = b''
    for size, data in entries:
        out += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), off + len(blob))
        blob += data
    open(path, 'wb').write(out + blob)


def build(outdir, theme):
    imgs = master_sizes(theme)
    for n, sp in SPECS.items():
        imgs[n] = render_small(n, sp, theme)
    write_ico(f'{outdir}/WallpaperSwitcher-{theme}.ico', imgs)
    for n, im in imgs.items():
        im.save(f'{outdir}/glyph-{theme}-{n}.png')
    return imgs


if __name__ == '__main__':
    import os
    os.makedirs('multi', exist_ok=True)
    for t in ('dark', 'light'):
        build('multi', t)
    print('ok')
