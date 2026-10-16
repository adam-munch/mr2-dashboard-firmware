#!/usr/bin/env python3
"""
Approximate PNG preview of an FT81x co-processor command stream.

Understands only what this firmware draws: POINTS, LINES, LINE_STRIP,
RECTS, colours, CMD_GRADIENT (vertical), CMD_TEXT/CMD_NUMBER and
CMD_ROMFONT. Text uses a TrueType stand-in for the EVE ROM fonts, so
glyph shapes and widths are only roughly right. Check the real panel
before trusting fine details.

Usage:
    tools/render_preview.py build-tests/preview/*.bin

Needs Pillow (pip install pillow). Writes <name>.png next to each input.
"""
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

WIDTH, HEIGHT = 800, 480
SS = 2  # supersampling factor for smoother edges

# Approximate cell heights of the FT81x ROM fonts, in pixels.
ROM_FONT_HEIGHT = {26: 16, 27: 20, 28: 25, 29: 28, 30: 36, 31: 49, 32: 63, 33: 83, 34: 108}
FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/Library/Fonts/Arial.ttf",
    "C:/Windows/Fonts/arial.ttf",
]

OPT_CENTERX, OPT_CENTERY, OPT_RIGHTX = 512, 1024, 2048
CMD_DLSTART, CMD_SWAP = 0xFFFFFF00, 0xFFFFFF01
CMD_GRADIENT, CMD_TEXT, CMD_NUMBER, CMD_ROMFONT = 0xFFFFFF0B, 0xFFFFFF0C, 0xFFFFFF2E, 0xFFFFFF3F
POINTS, LINES, LINE_STRIP, RECTS = 2, 3, 4, 9


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def rgb(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def load_font(px):
    for path in FONT_CANDIDATES:
        if Path(path).exists():
            return ImageFont.truetype(path, px)
    return ImageFont.load_default()


class Renderer:
    def __init__(self):
        self.img = Image.new("RGB", (WIDTH * SS, HEIGHT * SS))
        self.draw = ImageDraw.Draw(self.img)
        self.color = (255, 255, 255)
        self.clear_color = (0, 0, 0)
        self.point_size = 16
        self.line_width = 16
        self.prim = None
        self.verts = []
        self.handles = {}

    def end_primitive(self):
        d, v = self.draw, self.verts
        if self.prim == POINTS:
            for x, y, c, ps, _ in v:
                r = ps / 16 * SS
                d.ellipse([x * SS - r, y * SS - r, x * SS + r, y * SS + r], fill=c)
        elif self.prim in (LINES, LINE_STRIP):
            step = 2 if self.prim == LINES else 1
            for k in range(0, len(v) - 1, step):
                a, b = v[k], v[k + 1]
                w = b[4] / 16 * SS
                d.line([a[0] * SS, a[1] * SS, b[0] * SS, b[1] * SS], fill=b[2], width=max(1, round(2 * w)))
                for p in (a, b):  # round caps
                    d.ellipse([p[0] * SS - w, p[1] * SS - w, p[0] * SS + w, p[1] * SS + w], fill=b[2])
        elif self.prim == RECTS:
            for k in range(0, len(v) - 1, 2):
                a, b = v[k], v[k + 1]
                r = b[4] / 16 * SS
                d.rounded_rectangle([a[0] * SS - r, a[1] * SS - r, b[0] * SS + r, b[1] * SS + r],
                                    radius=r, fill=b[2])
        self.verts = []
        self.prim = None

    def text(self, x, y, font, opts, s):
        cell = ROM_FONT_HEIGHT.get(self.handles.get(font, font), 20) * SS
        f = load_font(int(cell * 0.72))
        left, top, right, bottom = self.draw.textbbox((0, 0), s, font=f)
        w = right - left
        X, Y = x * SS, y * SS
        if opts & OPT_CENTERX:
            X -= w / 2
        if opts & OPT_RIGHTX:
            X -= w
        if opts & OPT_CENTERY:
            Y -= cell / 2
        self.draw.text((X - left, Y + (cell - (bottom + top)) / 2), s, font=f, fill=self.color)

    def gradient(self, y0, c0, y1, c1):
        for yy in range(HEIGHT * SS):
            t = min(1.0, max(0.0, (yy / SS - y0) / max(1, y1 - y0)))
            c = tuple(round(a * (1 - t) + b * t) for a, b in zip(rgb(c0), rgb(c1)))
            self.draw.line([0, yy, WIDTH * SS, yy], fill=c)

    def run(self, words):
        def read_str(k):
            raw = b""
            while True:
                chunk = struct.pack("<I", words[k])
                k += 1
                if 0 in chunk:
                    return (raw + chunk[: chunk.index(0)]).decode("latin-1"), k
                raw += chunk

        i = 0
        while i < len(words):
            w = words[i]
            if (w & 0xFFFFFF00) == 0xFFFFFF00:
                if w in (CMD_DLSTART, CMD_SWAP):
                    i += 1
                elif w in (CMD_TEXT, CMD_NUMBER):
                    x, y = s16(words[i + 1]), s16(words[i + 1] >> 16)
                    font, opts = s16(words[i + 2]), words[i + 2] >> 16
                    if w == CMD_TEXT:
                        s, i = read_str(i + 3)
                    else:
                        s = str(struct.unpack("<i", struct.pack("<I", words[i + 3]))[0])
                        i += 4
                    self.text(x, y, font, opts, s)
                elif w == CMD_GRADIENT:
                    self.gradient(s16(words[i + 1] >> 16), words[i + 2], s16(words[i + 3] >> 16), words[i + 4])
                    i += 5
                elif w == CMD_ROMFONT:
                    self.handles[words[i + 1]] = words[i + 2]
                    i += 3
                else:
                    raise ValueError(f"unsupported co-processor command 0x{w:08X} at word {i}")
                continue

            op = w >> 24
            if (w >> 30) == 1:  # VERTEX2F
                x, y = (w >> 15) & 0x7FFF, w & 0x7FFF
                x = x - 0x8000 if x & 0x4000 else x
                y = y - 0x8000 if y & 0x4000 else y
                self.verts.append((x / 16, y / 16, self.color, self.point_size, self.line_width))
            elif op == 0x02:
                self.clear_color = rgb(w)
            elif op == 0x26:
                self.draw.rectangle([0, 0, WIDTH * SS, HEIGHT * SS], fill=self.clear_color)
            elif op == 0x04:
                self.color = rgb(w)
            elif op == 0x0D:
                self.point_size = w & 0x1FFF
            elif op == 0x0E:
                self.line_width = w & 0xFFF
            elif op == 0x1F:
                self.prim, self.verts = w & 0xF, []
            elif op == 0x21:
                self.end_primitive()
            i += 1
        return self.img.resize((WIDTH, HEIGHT), Image.LANCZOS)


def main(paths):
    if not paths:
        print(__doc__)
        return 1
    for p in map(Path, paths):
        data = p.read_bytes()
        words = struct.unpack(f"<{len(data) // 4}I", data)
        out = p.with_suffix(".png")
        Renderer().run(words).save(out)
        print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
