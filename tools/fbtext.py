#!/usr/bin/env python3
"""Read the text off a dumped ST framebuffer.

GPGX_FBAT writes 32000 bytes of EmuTOS's own screen memory -- 320x200,
four planes, interleaved 16-bit words, 160 bytes a line. The console
font is 8x8, so the screen is 40 columns by 25 rows of character cells.

The font itself is not extracted from anywhere: it is learned. Several
of the lines on screen are string literals in the program that printed
them, so their cells and their characters can be paired up directly,
and every glyph learned that way decodes every later appearance. That
beats squinting at ASCII art, which is how a "-15" and a "-11" got
confused once already.
"""
import sys

W, H, BPL = 320, 200, 160


def cells(path):
    d = open(path, "rb").read()
    grid = []
    for row in range(H // 8):
        line = []
        for col in range(W // 8):
            bits = []
            for y in range(8):
                base = (row * 8 + y) * BPL
                word = (d[base + (col // 2) * 8] << 8) | d[base + (col // 2) * 8 + 1]
                byte = (word >> 8) if (col % 2 == 0) else (word & 0xFF)
                bits.append(byte)
            line.append(bytes(bits))
        grid.append(line)
    return grid


def learn(grid, known):
    """known: {row_index: exact text}. Returns glyph->char."""
    font = {}
    for r, text in known.items():
        for c, ch in enumerate(text):
            if c < len(grid[r]):
                g = grid[r][c]
                if g not in font:
                    font[g] = ch
    return font


def decode(grid, font):
    out = []
    blank = bytes(8)
    for row in grid:
        s = "".join(font.get(g, ("·" if g != blank else " ")) for g in row)
        out.append(s.rstrip())
    return out


FONTFILE = None


def load_font(path):
    font = {}
    try:
        with open(path, "rb") as f:
            blob = f.read()
    except OSError:
        return font
    for i in range(0, len(blob), 9):
        font[blob[i:i + 8]] = chr(blob[i + 8])
    return font


def save_font(path, font):
    with open(path, "wb") as f:
        for g, ch in font.items():
            f.write(g + bytes([ord(ch)]))


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = [a for a in sys.argv[1:] if a.startswith("--")]
    fontfile = None
    for o in opts:
        if o.startswith("--font="):
            fontfile = o.split("=", 1)[1]

    grid = cells(args[0])
    known = {}
    for spec in args[1:]:
        r, _, t = spec.partition("=")
        known[int(r)] = t

    # A learned glyph is worth keeping: the row a string lands on moves
    # whenever the program prints a line more or fewer, and re-teaching
    # from stale row numbers pairs every glyph with the wrong character
    # and produces confident nonsense. So the table is built once and
    # carried forward; later runs only add glyphs it has not seen.
    font = load_font(fontfile) if fontfile else {}
    if known:
        fresh = learn(grid, known)
        for g, ch in fresh.items():
            font.setdefault(g, ch)
        if fontfile:
            save_font(fontfile, font)

    for i, line in enumerate(decode(grid, font)):
        if line:
            print("%2d| %s" % (i, line))
