#!/usr/bin/env python3
"""Draw docs/spellbar-mockup.png: the spell bar as src/bar.cpp draws it (same layout, colours, glyphs and states), at 2x.

Usage: python3 docs/mockup.py FA_SOLID_TTF [OUT.png]
FA_SOLID_TTF = Font Awesome 6 Free Solid (fa-solid-900.ttf, the font SKSE Menu Framework uses for the bar's icons). Needs Pillow.

Shown state: Ctrl is held (its row lit, the others dimmed); Shift+1 is a spell you can't afford (red wash); Ctrl+4 is a
shout cooling down (23 s); 1-3 is the equipped sword (gold frame); 1-4 is a health potion you carry 12 of (shown "x12").
"""
import math
import sys

from PIL import Image, ImageDraw, ImageFont

SCALE = 2.0                      # the bar at 2x its 1080p size
ICON, GAP, LABEL_W = 46, 5, 44   # px at 1080p (bar.cpp defaults)
FONT_SIZE = 16
OPACITY, DIM = 0.9, 0.45         # Opacity, DimInactive
TEXT_FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

# rows bottom to top: (label, active, [slot, ...]); slot = (glyph or "sword", rgb, state dict) or None for an empty slot
ROWS = [
    ("1-4", False, [(0xF06D, (255, 122, 42), {}),                    # Flames (fire)
                    (0xF21E, (255, 214, 94), {}),                    # Healing (heart-pulse)
                    ("sword", (208, 212, 220), {"equipped": True}),  # Iron Sword, equipped
                    (0xF0C3, (255, 77, 77), {"count": 12})]),        # Potion of Health x12
    ("Ctrl", True, [(0xF2DC, (143, 211, 255), {}),                   # Frostbite (snowflake)
                    (0xF3ED, (111, 224, 176), {}),                   # Oakflesh (shield-halved)
                    (0xF6E2, (122, 168, 255), {}),                   # Conjure Familiar (ghost)
                    (0xF72E, (191, 232, 255), {"cooldown": 23})]),   # Unrelenting Force (wind), cooling down
    ("Shift", False, [(0xF0E7, (180, 156, 255), {"cant_afford": True}),  # Lightning Bolt (bolt), not enough magicka
                      (0xF06E, (201, 139, 255), {}),                     # Fury (eye)
                      None,
                      (0xF6E3, (208, 212, 220), {})]),                   # Warhammer (hammer)
]


def main():
    fa_path = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "spellbar-mockup.png"
    s = SCALE
    sz, gap, label_w = ICON * s, GAP * s, LABEL_W * s
    row_h = sz + gap
    bar_w = label_w + 4 * sz + 3 * gap
    pad = 40 * s
    w, h = int(bar_w + 2 * pad + 20 * s), int(len(ROWS) * row_h + 2 * pad)

    # backdrop: a dim dusk gradient, like an evening sky over dark ground
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for y in range(h):
        t = y / (h - 1)
        top, bot = (38, 46, 62), (22, 28, 24)
        c = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = c + (255,)

    text = lambda size: ImageFont.truetype(TEXT_FONT, int(size))
    fa = lambda size: ImageFont.truetype(fa_path, int(size))

    def layer():
        return Image.new("RGBA", img.size, (0, 0, 0, 0))

    def put(lay):
        img.alpha_composite(lay)

    def rgba(rgb, a):
        return tuple(rgb) + (max(0, min(255, int(a))),)

    x0 = pad + 10 * s
    y_bottom = h - pad
    fs = FONT_SIZE * s
    for i, (label, active, slots) in enumerate(ROWS):
        y = y_bottom - (i + 1) * row_h
        a = OPACITY * (1.0 if active else DIM)
        lay = layer()
        d = ImageDraw.Draw(lay)
        f = text(fs)
        tw = d.textlength(label, font=f)
        d.text((x0 + label_w - tw - 8 * s, y + (sz - fs) / 2 - 2 * s), label, font=f,
               fill=rgba((255, 230, 160) if active else (220, 220, 220), 255 * a))
        put(lay)
        for n in range(4):
            slot = slots[n]
            p0 = (x0 + label_w + n * (sz + gap), y)
            p1 = (p0[0] + sz, p0[1] + sz)
            rnd = 6 * s
            lay = layer()
            d = ImageDraw.Draw(lay)
            d.rounded_rectangle([p0, p1], rnd, fill=rgba((12, 12, 14), 200 * a * (1.0 if slot else 0.5)))
            put(lay)
            if slot:
                glyph, color, st = slot
                lay = layer()
                d = ImageDraw.Draw(lay)
                if glyph == "sword":
                    draw_sword(d, p0, sz, rgba(color, 255 * a))
                else:
                    g = chr(glyph)
                    gf = fa(sz * 0.56)
                    bb = d.textbbox((0, 0), g, font=gf)
                    gw, gh = bb[2] - bb[0], bb[3] - bb[1]
                    d.text((p0[0] + (sz - gw) / 2 - bb[0], p0[1] + (sz - gh) / 2 - bb[1]), g, font=gf, fill=rgba(color, 255 * a))
                put(lay)
                lay = layer()
                d = ImageDraw.Draw(lay)
                if st.get("cant_afford"):
                    d.rounded_rectangle([p0, p1], rnd, fill=rgba((160, 20, 20), 110 * a))
                if "cooldown" in st:
                    d.rounded_rectangle([p0, p1], rnd, fill=rgba((0, 0, 0), 150 * a))
                    t = str(st["cooldown"])
                    tf = text(fs * 1.3)
                    bb = d.textbbox((0, 0), t, font=tf)
                    d.text((p0[0] + (sz - (bb[2] - bb[0])) / 2 - bb[0], p0[1] + (sz - (bb[3] - bb[1])) / 2 - bb[1]), t,
                           font=tf, fill=rgba((255, 255, 255), 255 * a))
                if "count" in st:
                    t = "x" + str(st["count"])
                    tf = text(fs * 0.85)
                    tw = d.textlength(t, font=tf)
                    d.text((p1[0] - tw - 3 * s, p1[1] - fs * 0.85 - 4 * s), t, font=tf, fill=rgba((255, 255, 255), 255 * a))
                put(lay)
            lay = layer()
            d = ImageDraw.Draw(lay)
            eq = bool(slot and slot[2].get("equipped"))
            frame = rgba((255, 205, 90), 255 * a) if eq else rgba((255, 255, 255), (70 if slot else 30) * a)
            d.rounded_rectangle([p0, p1], rnd, outline=frame, width=max(1, round((2.2 if eq else 1.0) * s)))
            d.text((p0[0] + 3 * s, p0[1] + 1 * s), str(n + 1), font=text(fs * 0.8), fill=rgba((255, 255, 255), 200 * a))
            put(lay)
    img.convert("RGB").save(out, optimize=True)
    print(out, img.size)


def draw_sword(d, p0, sz, fill):
    """icons/sword.svg (64x64 viewBox, rotated 45 degrees) drawn inside the slot with the bar's 18 % padding."""
    pad = sz * 0.18
    k = (sz - 2 * pad) / 64.0
    cx = cy = 32.0
    ang = math.radians(45)

    def tf(x, y):
        dx, dy = x - cx, y - cy
        rx = cx + dx * math.cos(ang) - dy * math.sin(ang)
        ry = cy + dx * math.sin(ang) + dy * math.cos(ang)
        return (p0[0] + pad + rx * k, p0[1] + pad + ry * k)

    def rect(x, y, w, h):
        d.polygon([tf(x, y), tf(x + w, y), tf(x + w, y + h), tf(x, y + h)], fill=fill)

    d.polygon([tf(32, 2), tf(35.5, 9), tf(35.5, 41), tf(28.5, 41), tf(28.5, 9)], fill=fill)
    rect(20, 41, 24, 4.5)
    rect(29.8, 45.5, 4.4, 10)
    c = tf(32, 58.5)
    r = 3.4 * k
    d.ellipse([c[0] - r, c[1] - r, c[0] + r, c[1] + r], fill=fill)


if __name__ == "__main__":
    main()
