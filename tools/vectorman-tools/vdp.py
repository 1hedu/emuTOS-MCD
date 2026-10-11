"""One frame of a running Mega Drive, as tools/gpgx-vdpdump.patch writes it.

The patch drops a file per frame: VRAM 64K, CRAM 128, VSRAM 128, the VDP's
32 registers and 64K of work RAM, each exactly as Genesis Plus GX holds it.
That last part matters. gpgx keeps every 16-bit word in the host's byte
order, so on a little-endian PC the byte the 68000 sees at address a is
stored at a ^ 1 -- in VRAM and in work RAM alike. CRAM is not the VDP's
format either: gpgx packs each colour into nine bits, BBBGGGRRR.

Everything here undoes those, so the rest of the extractor deals in the
machine's own terms: big-endian tiles, VDP colour words, 68000 addresses.
"""


class Frame:
    def __init__(self, path):
        d = open(path, 'rb').read()
        if len(d) != 0x10000 + 0x80 + 0x80 + 0x20 + 0x10000:
            raise SystemExit('%s: not a VDP dump (%d bytes)' % (path, len(d)))
        self.vram = d[:0x10000]
        self.cram = d[0x10000:0x10080]
        self.reg = d[0x10100:0x10120]
        self.wram = d[0x10120:0x20120]

    def vword(self, a):
        """The VRAM word at a, as the VDP reads it."""
        return self.vram[a] | (self.vram[a + 1] << 8)

    def ram8(self, a):
        """Work RAM byte at 68000 address 0xFF0000 + a."""
        return self.wram[a ^ 1]

    def ram16(self, a):
        return self.wram[a] | (self.wram[a + 1] << 8)

    def sram16(self, a):
        v = self.ram16(a)
        return v - 0x10000 if v & 0x8000 else v

    def colour(self, n):
        """CRAM entry n as the VDP's own word: 0000 BBB0 GGG0 RRR0."""
        c = self.cram[2 * n] | (self.cram[2 * n + 1] << 8)
        return (((c >> 6) & 7) << 9) | (((c >> 3) & 7) << 5) | ((c & 7) << 1)

    def tiles(self, first, count):
        """count tiles from tile number first, big-endian, 32 bytes each."""
        a = first * 32
        raw = self.vram[a:a + count * 32]
        return bytes(raw[i ^ 1] for i in range(len(raw)))

    def sprites(self):
        """The sprite table, walked down its link chain from entry 0 -- the
        order the VDP draws in, earliest on top."""
        base = (self.reg[5] & 0x7F) << 9
        out, seen, i = [], set(), 0
        while i not in seen and len(out) < 80:
            seen.add(i)
            a = base + 8 * i
            w0, w1, w2, w3 = (self.vword(a + 2 * k) for k in range(4))
            out.append(dict(y=(w0 & 0x3FF) - 128, x=(w3 & 0x1FF) - 128,
                            w=((w1 >> 10) & 3) + 1, h=((w1 >> 8) & 3) + 1,
                            tile=w2 & 0x7FF, pal=(w2 >> 13) & 3,
                            hf=(w2 >> 11) & 1, vf=(w2 >> 12) & 1))
            i = w1 & 0x7F
            if i == 0:
                break
        return out


def pixels(gfx, w, h):
    """A sprite's pixels from its tiles, as rows of colour indices. The
    VDP orders a sprite's tiles down each column first."""
    rows = [[0] * (8 * w) for _ in range(8 * h)]
    for c in range(w):
        for r in range(h):
            t = gfx[(c * h + r) * 32:(c * h + r + 1) * 32]
            for y in range(8):
                for x in range(8):
                    b = t[y * 4 + (x >> 1)]
                    rows[r * 8 + y][c * 8 + x] = (b >> 4) if not x & 1 else b & 15
    return rows
