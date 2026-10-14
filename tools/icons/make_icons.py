#!/usr/bin/env python3
"""Wallpaper Switcher icon set: 6 designs x (dark, light), 256x256 .ico each.
All art is drawn at 4x and downsampled for clean edges."""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageChops, ImageFont

K = 4                      # supersampling
S = 256 * K


def k(v):
    return int(round(v * K))


def hx(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


# ------------------------------------------------------------------ helpers
def gradient(w, h, stops, angle=90):
    """stops: [(pos, (r,g,b))]. angle 0 = left->right, 90 = top->bottom."""
    th = math.radians(angle)
    xs = (np.linspace(-0.5, 0.5, w)[None, :]) * w
    ys = (np.linspace(-0.5, 0.5, h)[:, None]) * h
    proj = xs * math.cos(th) + ys * math.sin(th)
    L = abs(w * math.cos(th)) + abs(h * math.sin(th))
    t = np.clip(proj / L + 0.5, 0, 1)
    pos = [p for p, _ in stops]
    ch = [np.interp(t, pos, [c[i] for _, c in stops]) for i in range(3)]
    arr = np.stack(ch + [np.full_like(t, 255.0)], -1).astype(np.uint8)
    return Image.fromarray(arr, 'RGBA')


def rr_mask(size, box, r):
    m = Image.new('L', size, 0)
    ImageDraw.Draw(m).rounded_rectangle(box, r, fill=255)
    return m


def composite(cv, layer, mask=None, pos=(0, 0), alpha=1.0):
    lay = layer.copy()
    a = lay.getchannel('A')
    if mask is not None:
        a = ImageChops.multiply(a, mask)
    if alpha != 1.0:
        a = a.point(lambda v: int(v * alpha))
    lay.putalpha(a)
    full = Image.new('RGBA', cv.size, (0, 0, 0, 0))
    full.paste(lay, pos)
    cv.alpha_composite(full)


def drop_shadow(cv, mask, dy=3, blur=5, alpha=0.35, color=(0, 0, 0)):
    m = Image.new('L', cv.size, 0)
    m.paste(mask, (0, k(dy)))
    m = m.filter(ImageFilter.GaussianBlur(k(blur)))
    solid = Image.new('RGBA', cv.size, color + (255,))
    composite(cv, solid, m, alpha=alpha)


def full_mask_from_box(cv, box, r):
    b = tuple(k(v) for v in box)
    return rr_mask(cv.size, b, k(r))


# ------------------------------------------------------------------ themes
DARK = dict(
    name='dark',
    tile=[(0, hx('#2A3042')), (1, hx('#0E1118'))], tile_angle=60,
    tile_shadow=0.45, border=(255, 255, 255, 34), glow_alpha=0.34,
    bezel=[(0, hx('#F4F6FC')), (1, hx('#BCC3D8'))],
    stand=hx('#A9B1C8'), shadow=0.45,
    ink=hx('#F3F5FF'), gap=hx('#171B26'),
)
LIGHT = dict(
    name='light',
    tile=[(0, hx('#FFFFFF')), (1, hx('#DDE3F1'))], tile_angle=60,
    tile_shadow=0.22, border=(20, 24, 40, 40), glow_alpha=0.26,
    bezel=[(0, hx('#343B52')), (1, hx('#171B2A'))],
    stand=hx('#4A526B'), shadow=0.30,
    ink=hx('#1B2033'), gap=hx('#F1F4FB'),
)

ACC_CORAL = hx('#FF6A55')
ACC_LIME = hx('#B8F13A')
ACC_AMBER = hx('#FFC247')
ACC_AQUA = hx('#25D9C4')


# -------------------------------------------------------------- wallpapers
def _hills(img, layers):
    w, h = img.size
    d = ImageDraw.Draw(img, 'RGBA')
    for col, base, amp, ph, freq in layers:
        pts = []
        n = max(40, w // 6)
        for i in range(n + 1):
            x = w * i / n
            y = h * base + amp * h * math.sin(x / w * 2 * math.pi * freq + ph)
            pts.append((x, y))
        pts += [(w, h), (0, h)]
        d.polygon(pts, fill=col)


def _sun(img, cx, cy, r, color=(255, 244, 214), halo=(255, 226, 170)):
    w, h = img.size
    halo_l = Image.new('RGBA', img.size, (0, 0, 0, 0))
    ImageDraw.Draw(halo_l).ellipse((cx - r * 2, cy - r * 2, cx + r * 2, cy + r * 2), fill=halo + (110,))
    halo_l = halo_l.filter(ImageFilter.GaussianBlur(r * 0.7))
    img.alpha_composite(halo_l)
    ImageDraw.Draw(img, 'RGBA').ellipse((cx - r, cy - r, cx + r, cy + r), fill=color + (255,))


def wp_sunset(sunx=0.68, suny=0.40, sunr=0.15, tone=(55, 18, 105)):
    stops = [(0, hx('#4B3FE0')), (0.55, hx('#E4519E')), (1, hx('#FF9F5A'))]

    def paint(w, h):
        img = gradient(w, h, stops, 90)
        m = min(w, h)
        _sun(img, sunx * w, suny * h, sunr * m)
        _hills(img, [
            (tone + (120,), 0.62, 0.05, 0.4, 1.1),
            (tone + (185,), 0.74, 0.045, 2.1, 1.3),
            (tuple(int(c * 0.6) for c in tone) + (240,), 0.87, 0.035, 3.6, 1.0),
        ])
        return img
    return paint


def wp_aqua(sunx=0.5, suny=0.30, sunr=0.20):
    stops = [(0, hx('#27D9C6')), (1, hx('#2F5BFF'))]

    def paint(w, h):
        img = gradient(w, h, stops, 90)
        m = min(w, h)
        _sun(img, sunx * w, suny * h, sunr * m, color=(236, 255, 252), halo=(190, 255, 245))
        _hills(img, [
            ((10, 40, 140, 120), 0.64, 0.045, 0.2, 1.2),
            ((8, 30, 120, 190), 0.76, 0.04, 2.5, 1.5),
            ((5, 20, 90, 240), 0.88, 0.03, 4.0, 1.1),
        ])
        return img
    return paint


def wp_aurora(seed=0):
    stops = [(0, hx('#0A1A3A')), (0.7, hx('#0F5C74')), (1, hx('#1E9E8E'))]

    def paint(w, h):
        img = gradient(w, h, stops, 90)
        rib = Image.new('RGBA', img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(rib)
        cols = [(60, 255, 170, 200), (110, 120, 255, 190), (255, 90, 200, 150)]
        for i, c in enumerate(cols):
            pts = []
            for j in range(61):
                x = w * j / 60
                y = h * (0.30 + 0.17 * i) + h * 0.10 * math.sin(x / w * 5.0 + i * 1.7 + seed)
                pts.append((x, y))
            d.line(pts, fill=c, width=int(min(w, h) * 0.16), joint='curve')
        rib = rib.filter(ImageFilter.GaussianBlur(min(w, h) * 0.07))
        img.alpha_composite(rib)
        dd = ImageDraw.Draw(img, 'RGBA')
        rnd = np.random.RandomState(7 + int(seed * 10))
        for _ in range(14):
            x, y = rnd.rand() * w, rnd.rand() * h * 0.5
            r = min(w, h) * 0.012 * (0.6 + rnd.rand())
            dd.ellipse((x - r, y - r, x + r, y + r), fill=(255, 255, 255, 200))
        _hills(img, [((4, 18, 30, 230), 0.86, 0.03, 1.0 + seed, 1.2)])
        return img
    return paint


def wp_ocean():
    stops = [(0, hx('#2056FF')), (1, hx('#14D3E8'))]

    def paint(w, h):
        img = gradient(w, h, stops, 90)
        m = min(w, h)
        _sun(img, 0.74 * w, 0.30 * h, 0.13 * m, color=(255, 222, 130), halo=(255, 200, 90))
        _hills(img, [
            ((255, 255, 255, 60), 0.58, 0.04, 0.0, 1.6),
            ((255, 255, 255, 70), 0.70, 0.04, 1.6, 1.8),
            ((255, 255, 255, 85), 0.82, 0.035, 3.1, 1.5),
        ])
        return img
    return paint


def wp_candy(peak=(95, 35, 140)):
    stops = [(0, hx('#FF4F9A')), (0.55, hx('#FF8A5B')), (1, hx('#FFD36B'))]

    def paint(w, h):
        img = gradient(w, h, stops, 90)
        m = min(w, h)
        _sun(img, 0.30 * w, 0.34 * h, 0.14 * m, color=(255, 250, 230), halo=(255, 235, 190))
        d = ImageDraw.Draw(img, 'RGBA')
        base = h
        d.polygon([(-0.05 * w, base), (0.30 * w, 0.52 * h), (0.62 * w, base)], fill=peak + (150,))
        d.polygon([(0.28 * w, base), (0.66 * w, 0.44 * h), (1.05 * w, base)], fill=peak + (205,))
        d.polygon([(0.66 * w, 0.44 * h), (1.05 * w, base), (0.78 * w, base)], fill=(0, 0, 0, 38))
        return img
    return paint


def wp_violet():
    stops = [(0, hx('#7A4DFF')), (1, hx('#2B9DFF'))]

    def paint(w, h):
        img = gradient(w, h, stops, 90)
        m = min(w, h)
        _sun(img, 0.34 * w, 0.32 * h, 0.14 * m, color=(240, 255, 200), halo=(200, 255, 120))
        _hills(img, [
            ((20, 10, 90, 110), 0.62, 0.05, 0.5, 1.2),
            ((15, 8, 80, 185), 0.75, 0.04, 2.0, 1.5),
            ((10, 5, 60, 240), 0.88, 0.03, 3.5, 1.0),
        ])
        return img
    return paint


# --------------------------------------------------------------- primitives
def make_tile(th, glow):
    cv = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    box = (12, 12, 244, 244)
    m = full_mask_from_box(cv, box, 56)
    drop_shadow(cv, m, dy=4, blur=6, alpha=th['tile_shadow'])
    composite(cv, gradient(S, S, th['tile'], th['tile_angle']), m)
    g = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(g).ellipse((k(30), k(150), k(226), k(262)), fill=glow + (255,))
    g = g.filter(ImageFilter.GaussianBlur(k(24)))
    composite(cv, g, m, alpha=th['glow_alpha'])
    bl = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(bl).rounded_rectangle(tuple(k(v) for v in box), k(56), outline=th['border'], width=k(1.4))
    cv.alpha_composite(bl)
    return cv


def monitor(cv, th, cx, base_y, sw, sh, paint, b=6, stand=True, neck_h=10, base_h=4.5,
            neck_w=None, base_w=None, ro=None, screen_img=None, shadow=None):
    """Draw a monitor; (cx, base_y) = bottom centre of its footprint (256-space)."""
    ow, oh = sw + 2 * b, sh + 2 * b
    total_h = oh + (neck_h + base_h if stand else 0)
    top = base_y - total_h
    x0 = cx - ow / 2
    ro = ro if ro is not None else max(5, min(ow, oh) * 0.11)
    box = (x0, top, x0 + ow, top + oh)

    if stand:
        nw = neck_w if neck_w else max(8, ow * 0.13)
        bw = base_w if base_w else max(18, ow * 0.42)
        by = top + oh
        d = ImageDraw.Draw(cv, 'RGBA')
        c = th['stand'] + (255,)
        d.rectangle((k(cx - nw / 2), k(by - 3), k(cx + nw / 2), k(by + neck_h + 0.5)), fill=c)
        d.rounded_rectangle((k(cx - bw / 2), k(by + neck_h - 0.5), k(cx + bw / 2), k(by + neck_h + base_h)),
                            k(base_h / 2), fill=c)

    m = full_mask_from_box(cv, box, ro)
    drop_shadow(cv, m, dy=2.5, blur=3.5, alpha=th['shadow'] if shadow is None else shadow)
    bez = gradient(k(ow), k(oh), th['bezel'], 90)
    bm = rr_mask((k(ow), k(oh)), (0, 0, k(ow) - 1, k(oh) - 1), k(ro))
    composite(cv, bez, bm, pos=(k(x0), k(top)))

    sx0, sy0 = x0 + b, top + b
    ri = max(2.0, ro - b * 0.55)
    if screen_img is None:
        scr = paint(k(sw), k(sh))
    else:
        scr = screen_img
    # soft glass sheen
    sheen = Image.new('RGBA', scr.size, (0, 0, 0, 0))
    w_, h_ = scr.size
    ImageDraw.Draw(sheen).polygon([(0, 0), (w_ * 0.62, 0), (0, h_ * 0.62)], fill=(255, 255, 255, 34))
    scr.alpha_composite(sheen)
    sm = rr_mask((k(sw), k(sh)), (0, 0, k(sw) - 1, k(sh) - 1), k(ri))
    composite(cv, scr, sm, pos=(k(sx0), k(sy0)))
    return box


def arc_arrow(cv, cx, cy, R, a0, a1, width, color, head):
    """Circular arrow along an arc (angles in degrees, y down)."""
    d = ImageDraw.Draw(cv, 'RGBA')
    pts = []
    n = 80
    for i in range(n + 1):
        a = math.radians(a0 + (a1 - a0) * i / n)
        pts.append((k(cx + R * math.cos(a)), k(cy + R * math.sin(a))))
    d.line(pts, fill=color + (255,), width=k(width), joint='curve')
    r = width / 2
    p = pts[0]
    d.ellipse((p[0] - k(r), p[1] - k(r), p[0] + k(r), p[1] + k(r)), fill=color + (255,))
    a = math.radians(a1)
    tx, ty = -math.sin(a), math.cos(a)           # tangent (direction of travel)
    nx, ny = math.cos(a), math.sin(a)            # outward normal
    ex, ey = cx + R * math.cos(a), cy + R * math.sin(a)
    tip = (ex + tx * head, ey + ty * head)
    b1 = (ex + nx * head * 0.9, ey + ny * head * 0.9)
    b2 = (ex - nx * head * 0.9, ey - ny * head * 0.9)
    d.polygon([(k(tip[0]), k(tip[1])), (k(b1[0]), k(b1[1])), (k(b2[0]), k(b2[1]))], fill=color + (255,))


# ------------------------------------------------------------------ designs
def d_duo(th):
    cv = make_tile(th, (130, 90, 255))
    monitor(cv, th, 101, 190, 122, 76, wp_sunset(), b=6, neck_h=11)            # landscape
    monitor(cv, th, 189, 190, 54, 96, wp_aqua(), b=6, neck_h=10)               # portrait, in front
    return cv


def d_triple(th):
    cv = make_tile(th, (60, 230, 190))
    # continuous panorama across portrait | landscape | portrait
    b = 5
    sides_w, sides_h = 36, 66
    mid_w, mid_h = 90, 56
    gap = 5
    ow_side, ow_mid = sides_w + 2 * b, mid_w + 2 * b
    total = ow_side * 2 + ow_mid + gap * 2
    x_left = 128 - total / 2
    base = 180
    # screen rectangles in 256-space
    nh = 9
    bh = 4.2
    def top_of(sh):
        return base - (sh + 2 * b + nh + bh)
    sc = []
    x = x_left
    for (sw, sh) in [(sides_w, sides_h), (mid_w, mid_h), (sides_w, sides_h)]:
        sc.append((x + b, top_of(sh) + b, sw, sh, x + (sw + 2 * b) / 2))
        x += sw + 2 * b + gap
    gx0 = sc[0][0]
    gx1 = sc[2][0] + sc[2][2]
    gy0 = min(s[1] for s in sc)
    gy1 = max(s[1] + s[3] for s in sc)
    pano = wp_aurora(0.6)(k(gx1 - gx0), k(gy1 - gy0))
    for (sx, sy, sw, sh, cx) in sc:
        crop = pano.crop((k(sx - gx0), k(sy - gy0), k(sx - gx0) + k(sw), k(sy - gy0) + k(sh))).copy()
        monitor(cv, th, cx, base, sw, sh, None, b=b, neck_h=nh, base_h=bh, screen_img=crop,
                base_w=sw * 0.6 + 8, neck_w=7)
    return cv


def d_panels(th):
    cv = make_tile(th, (80, 140, 255))
    monitor(cv, th, 104, 144, 116, 72, wp_ocean(), b=6, stand=False, ro=11)
    monitor(cv, th, 180, 204, 60, 107, wp_candy(), b=6, stand=False, ro=11)
    # swap badge
    cx, cy, r = 212, 205, 25
    m = Image.new('L', cv.size, 0)
    ImageDraw.Draw(m).ellipse((k(cx - r), k(cy - r), k(cx + r), k(cy + r)), fill=255)
    drop_shadow(cv, m, dy=2.5, blur=3.5, alpha=0.4)
    badge = gradient(k(2 * r), k(2 * r), [(0, ACC_AMBER), (1, hx('#FF7A45'))], 60)
    bm = Image.new('L', badge.size, 0)
    ImageDraw.Draw(bm).ellipse((0, 0, badge.size[0] - 1, badge.size[1] - 1), fill=255)
    composite(cv, badge, bm, pos=(k(cx - r), k(cy - r)))
    ring = Image.new('RGBA', cv.size, (0, 0, 0, 0))
    ImageDraw.Draw(ring).ellipse((k(cx - r), k(cy - r), k(cx + r), k(cy + r)),
                                 outline=th['gap'] + (255,), width=k(2.6))
    cv.alpha_composite(ring)
    ink = (36, 24, 20)
    arc_arrow(cv, cx, cy, 11.5, 200, 330, 4.2, ink, 6.5)
    arc_arrow(cv, cx, cy, 11.5, 20, 150, 4.2, ink, 6.5)
    return cv


def d_mosaic(th):
    cv = make_tile(th, (255, 70, 160))
    b = 3
    gap = 6
    A = (100, 62)
    Bd = (56, 112)
    C = (100, 38)
    W = (A[0] + 2 * b) + gap + (Bd[0] + 2 * b)
    H = max((A[1] + 2 * b) + gap + (C[1] + 2 * b), Bd[1] + 2 * b)
    x0, y0 = 128 - W / 2, 128 - H / 2
    ax = x0 + (A[0] + 2 * b) / 2
    monitor(cv, th, ax, y0 + A[1] + 2 * b, A[0], A[1], wp_sunset(sunx=0.7), b=b, stand=False, ro=9)
    monitor(cv, th, ax, y0 + A[1] + 2 * b + gap + C[1] + 2 * b, C[0], C[1], wp_candy(), b=b, stand=False, ro=9)
    bx = x0 + (A[0] + 2 * b) + gap + (Bd[0] + 2 * b) / 2
    monitor(cv, th, bx, y0 + Bd[1] + 2 * b, Bd[0], Bd[1], wp_aqua(), b=b, stand=False, ro=9)
    # "primary display" dot
    d = ImageDraw.Draw(cv, 'RGBA')
    px, py = ax - 0, y0 + A[1] + 2 * b + gap + (C[1] + 2 * b) / 2
    d.ellipse((k(px - 4.5), k(py - 4.5), k(px + 4.5), k(py + 4.5)), fill=(255, 255, 255, 235))
    return cv


def d_glyph(th):
    cv = make_tile(th, (30, 200, 225))
    ink = th['ink']
    art = Image.new('RGBA', cv.size, (0, 0, 0, 0))     # drawn separately so the portrait can knock out the outline
    d = ImageDraw.Draw(art, 'RGBA')
    sw = 9
    lx0, ly0, lx1, ly1 = 34, 68, 158, 148
    d.rounded_rectangle((k(lx0), k(ly0), k(lx1), k(ly1)), k(14), outline=ink + (255,), width=k(sw))
    mt = gradient(k(62), k(32), [(0, ACC_AQUA), (1, hx('#2F78FF'))], 45)
    mm = Image.new('L', mt.size, 0)
    ImageDraw.Draw(mm).polygon([(0, k(32)), (k(20), k(8)), (k(32), k(21)), (k(43), k(2)), (k(62), k(32))], fill=255)
    composite(art, mt, mm, pos=(k(65), k(102)))
    d.rectangle((k(96 - 4.5), k(ly1 - 2), k(96 + 4.5), k(ly1 + 12)), fill=ink + (255,))
    d.rounded_rectangle((k(66), k(ly1 + 9), k(126), k(ly1 + 17)), k(4), fill=ink + (255,))

    px0, py0, px1, py1 = 140, 54, 210, 176
    ring = rr_mask(art.size, (k(px0 - 8), k(py0 - 8), k(px1 + 8), k(py1 + 8)), k(24))
    art.putalpha(ImageChops.multiply(art.getchannel('A'), ring.point(lambda v: 255 - v)))

    sh_m = full_mask_from_box(art, (px0, py0, px1, py1), 16)
    drop_shadow(art, sh_m, dy=2.5, blur=3.5, alpha=th['shadow'] * 0.8)
    pw, ph_ = px1 - px0, py1 - py0
    fill = gradient(k(pw), k(ph_), [(0, hx('#FF4F9A')), (0.55, hx('#FF8A5B')), (1, hx('#FFD36B'))], 70)
    dd = ImageDraw.Draw(fill, 'RGBA')
    cx, cy = pw / 2, 46
    dd.ellipse((k(cx - 13), k(cy - 13), k(cx + 13), k(cy + 13)), fill=(255, 250, 235, 255))
    dd.polygon([(0, k(ph_)), (0, k(ph_ - 36)), (k(cx - 8), k(ph_ - 58)), (k(pw), k(ph_ - 22)), (k(pw), k(ph_))],
               fill=(120, 40, 140, 150))
    fm = rr_mask(fill.size, (0, 0, fill.size[0] - 1, fill.size[1] - 1), k(16))
    composite(art, fill, fm, pos=(k(px0), k(py0)))
    cv.alpha_composite(art)
    return cv


def d_orbit(th):
    cv = make_tile(th, (140, 255, 90) if th['name'] == 'dark' else (120, 200, 255))
    acc = ACC_LIME if th['name'] == 'dark' else hx('#12B886')
    # ring of switching arrows
    arc_arrow(cv, 128, 128, 96, 205, 335, 9, acc, 15)
    arc_arrow(cv, 128, 128, 96, 25, 155, 9, acc, 15)
    # landscape + small portrait
    monitor(cv, th, 114, 174, 100, 62, wp_violet(), b=5, neck_h=10, base_h=4.4)
    monitor(cv, th, 167, 172, 38, 67, wp_sunset(sunx=0.5, suny=0.36, sunr=0.2), b=5, neck_h=8, base_h=4.2, shadow=th['shadow'])
    return cv


DESIGNS = [
    ('01-duo', 'Duo', d_duo),
    ('02-triple-panorama', 'Panorama', d_triple),
    ('03-floating-panels', 'Panels', d_panels),
    ('04-layout-mosaic', 'Layout', d_mosaic),
    ('05-line-glyph', 'Glyph', d_glyph),
    ('06-orbit', 'Orbit', d_orbit),
]


def finish(cv):
    p = cv.convert('RGBa').resize((256, 256), Image.LANCZOS).convert('RGBA')
    return p


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    made = {}
    for slug, label, fn in DESIGNS:
        for th in (DARK, LIGHT):
            img = finish(fn(th))
            path = os.path.join(outdir, f'{slug}-{th["name"]}.ico')
            img.save(path, format='ICO', sizes=[(256, 256)])
            img.save(path.replace('.ico', '.png'))
            made[(slug, th['name'])] = img
            print('wrote', path)
    return made


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'out')
