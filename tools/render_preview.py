#!/usr/bin/env python3
"""
Approximate PNG preview of an FT81x co-processor command stream.

Understands only what this firmware draws: POINTS, LINES, LINE_STRIP,
RECTS, EDGE_STRIP_B, colours and COLOR_A, scissor, COLOR_MASK, the stencil
test and operations, SAVE/RESTORE_CONTEXT, CMD_GRADIENT (vertical),
CMD_TEXT/CMD_NUMBER and CMD_ROMFONT. Text uses a TrueType stand-in for the
EVE ROM fonts, so glyph shapes and widths are only roughly right, and it
ignores the scissor and stencil. Check the real panel before trusting fine
details.

Usage:
    tools/render_preview.py build-tests/preview/*.bin

Needs Pillow (pip install pillow). Writes <name>.png next to each input.
"""
import struct
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

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
POINTS, LINES, LINE_STRIP, EDGE_STRIP_B, RECTS = 2, 3, 4, 8, 9
NEVER, LESS, LEQUAL, GREATER, GEQUAL, EQUAL, NOTEQUAL, ALWAYS = range(8)
ZERO, KEEP, REPLACE, INCR, DECR, INVERT = range(6)
STENCIL_TESTS = [
    lambda r, s: False, lambda r, s: r < s, lambda r, s: r <= s, lambda r, s: r > s,
    lambda r, s: r >= s, lambda r, s: r == s, lambda r, s: r != s, lambda r, s: True,
]
DEFAULT_SCISSOR = (0, 0, 2048, 2048)


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
        self.alpha = 255
        self.clear_color = (0, 0, 0)
        self.point_size = 16
        self.line_width = 16
        self.prim = None
        self.verts = []
        self.handles = {}
        self.stencil = Image.new("L", self.img.size, 0)
        self.clear_stencil = 0
        self.scissor = DEFAULT_SCISSOR  # x, y, w, h
        self.color_mask = (1, 1, 1, 1)
        self.stencil_func = (ALWAYS, 0, 255)  # func, ref, mask
        self.stencil_op = (KEEP, KEEP)  # sfail, spass
        self.contexts = []

    CONTEXT = ("color", "alpha", "point_size", "line_width", "scissor", "color_mask", "stencil_func",
               "stencil_op")

    def save_context(self):
        self.contexts.append({k: getattr(self, k) for k in self.CONTEXT})

    def restore_context(self):
        for k, v in self.contexts.pop().items():
            setattr(self, k, v)

    def masked(self):
        """True when drawing has to go through the scissor/stencil path."""
        return (self.scissor != DEFAULT_SCISSOR or self.color_mask != (1, 1, 1, 1)
                or self.stencil_func[0] != ALWAYS or self.stencil_op != (KEEP, KEEP))

    def shapes(self):
        """The primitive's shapes as (draw(d, fill), colour, alpha) tuples.
        Like the hardware, colour, alpha and sizes are latched per vertex."""
        v, out = self.verts, []
        if self.prim == POINTS:
            for x, y, c, a, ps, _ in v:
                r = ps / 16 * SS
                out.append((lambda d, f, x=x, y=y, r=r: d.ellipse([x * SS - r, y * SS - r, x * SS + r, y * SS + r],
                                                                  fill=f), c, a))
        elif self.prim in (LINES, LINE_STRIP):
            step = 2 if self.prim == LINES else 1
            for k in range(0, len(v) - 1, step):
                p, q = v[k], v[k + 1]
                w = q[5] / 16 * SS

                def seg(d, f, p=p, q=q, w=w):
                    d.line([p[0] * SS, p[1] * SS, q[0] * SS, q[1] * SS], fill=f, width=max(1, round(2 * w)))
                    for e in (p, q):  # round caps
                        d.ellipse([e[0] * SS - w, e[1] * SS - w, e[0] * SS + w, e[1] * SS + w], fill=f)
                out.append((seg, q[2], q[3]))
        elif self.prim == RECTS:
            for k in range(0, len(v) - 1, 2):
                p, q = v[k], v[k + 1]
                r = q[5] / 16 * SS
                out.append((lambda d, f, p=p, q=q, r=r: d.rounded_rectangle(
                    [p[0] * SS - r, p[1] * SS - r, q[0] * SS + r, q[1] * SS + r], radius=r, fill=f), q[2], q[3]))
        elif self.prim == EDGE_STRIP_B and len(v) > 1:
            # The area below each segment, combined even-odd (exact for the
            # stencil INVERT fills this firmware does). Closing the strip
            # straight down to below the screen adds nothing on screen.
            pts = [(x * SS, y * SS) for x, y, *_ in v]
            if pts[0] != pts[-1]:
                pts += [(pts[-1][0], 4 * HEIGHT * SS), (pts[0][0], 4 * HEIGHT * SS)]
            out.append((lambda d, f, pts=pts: d.polygon(pts, fill=f), v[-1][2], v[-1][3]))
        return out

    def paint_masked(self, draw_shape, color, alpha):
        """Draw one shape through the scissor, colour mask and stencil."""
        size = self.img.size
        cov = Image.new("L", size, 0)
        draw_shape(ImageDraw.Draw(cov), 255)
        x, y, w, h = self.scissor
        clip = Image.new("L", size, 0)
        ImageDraw.Draw(clip).rectangle([x * SS, y * SS, (x + w) * SS - 1, (y + h) * SS - 1], fill=255)
        cov = ImageChops.multiply(cov, clip)

        func, ref, mask = self.stencil_func
        test = STENCIL_TESTS[func]
        passed = ImageChops.multiply(cov, self.stencil.point(lambda s: 255 if test(ref & mask, s & mask) else 0))
        failed = ImageChops.subtract(cov, passed)

        if any(self.color_mask[:3]):
            self.img.paste(color, (0, 0, *size), passed.point(lambda m: m * alpha // 255))
        for where, op in ((failed, self.stencil_op[0]), (passed, self.stencil_op[1])):
            if op == KEEP:
                continue
            new = {
                ZERO: lambda: Image.new("L", size, 0),
                REPLACE: lambda: Image.new("L", size, ref),
                INCR: lambda: self.stencil.point(lambda s: min(s + 1, 255)),
                DECR: lambda: self.stencil.point(lambda s: max(s - 1, 0)),
                INVERT: lambda: ImageChops.invert(self.stencil),
            }[op]()
            self.stencil = Image.composite(new, self.stencil, where)

    def end_primitive(self):
        shapes = self.shapes()
        self.verts = []
        self.prim = None
        if self.masked():
            for draw_shape, color, alpha in shapes:
                self.paint_masked(draw_shape, color, alpha)
            return

        # Opaque shapes go straight to the image; a run of translucent ones
        # is drawn on a layer and blended in before the next shape.
        layer = None
        layer_alpha = 255

        def flush():
            nonlocal layer
            if layer is not None:
                mask = layer.getchannel("A").point(lambda px: px * layer_alpha // 255)
                self.img.paste(layer.convert("RGB"), (0, 0), mask)
                layer = None

        def canvas(alpha):
            nonlocal layer, layer_alpha
            if alpha != layer_alpha or alpha == 255:
                flush()
            layer_alpha = alpha
            if alpha == 255:
                return self.draw
            if layer is None:
                layer = Image.new("RGBA", self.img.size, (0, 0, 0, 0))
            return ImageDraw.Draw(layer)

        for draw_shape, color, alpha in shapes:
            draw_shape(canvas(alpha), color)
        flush()

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
                self.verts.append((x / 16, y / 16, self.color, self.alpha, self.point_size, self.line_width))
            elif op == 0x02:
                self.clear_color = rgb(w)
            elif op == 0x11:
                self.clear_stencil = w & 0xFF
            elif op == 0x26:  # CLEAR, ignoring the scissor
                if w & 4:
                    self.draw.rectangle([0, 0, WIDTH * SS, HEIGHT * SS], fill=self.clear_color)
                if w & 2:
                    self.stencil = Image.new("L", self.img.size, self.clear_stencil)
            elif op == 0x04:
                self.color = rgb(w)
            elif op == 0x10:
                self.alpha = w & 0xFF
            elif op == 0x0D:
                self.point_size = w & 0x1FFF
            elif op == 0x0E:
                self.line_width = w & 0xFFF
            elif op == 0x0A:
                self.stencil_func = ((w >> 16) & 0xF, (w >> 8) & 0xFF, w & 0xFF)
            elif op == 0x0C:
                self.stencil_op = ((w >> 3) & 7, w & 7)
            elif op == 0x1B:
                self.scissor = ((w >> 11) & 0x7FF, w & 0x7FF, *self.scissor[2:])
            elif op == 0x1C:
                self.scissor = (*self.scissor[:2], (w >> 12) & 0xFFF, w & 0xFFF)
            elif op == 0x20:
                self.color_mask = ((w >> 3) & 1, (w >> 2) & 1, (w >> 1) & 1, w & 1)
            elif op == 0x22:
                self.save_context()
            elif op == 0x23:
                self.restore_context()
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
