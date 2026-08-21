#!/usr/bin/env python3
"""Desktop icons for the drives this machine actually has.

EmuDesk picks a drive's icon by drive number and nothing else --
deskapp.c:721 is `(i > 1) ? IG_HARD : IG_FLOPPY`. It has nothing else
to go on: the block layer records a drive's geometry, not what kind of
thing it is. On a TOS machine the drive number answers the question
anyway, because A: and B: are the floppy drives and C: onward are hard
disk partitions, so the rule is right every time.

It is right here too, and still wrong, because these letters are not
those things: A: is a cartridge save chip, B: is a compact disc, C: is
a ramdisk in cartridge ROM and D: is eight kilobytes of battery-backed
RAM inside the console. Two floppies and two hard drives is the correct
answer to a question about different hardware.

EmuTOS has a supported way out: app_rdicon() loads EMUICON.RSC from the
root of the boot drive and only falls back to the built-in eight if it
is missing, and load_icon_file() is happy with a resource that has more
than eight. The icon a drive uses is a field in EMUDESK.INF, also read
from the boot drive. So both halves are files we generate onto the
ramdisk, which is the boot drive, and not one line of EmuDesk changes.

Icons are 32x32, one bit deep, with a 32x32 mask, which is what the
built-in set is: see the ICONBLK array at the end of desk/icons.c.

  #  black ink
  .  white
  :  50% dither, the grey the floppy icon is drawn in
  (space) outside the icon: mask clear, desktop shows through

Run with --preview <file.ppm> to look at them before believing in them.
"""
import sys

W = H = 32


def frame(rows):
    """Pad art to 32x32 and check nobody drew outside the lines."""
    out = []
    for r in rows:
        if len(r) > W:
            raise SystemExit('row wider than %d: %r' % (W, r))
        out.append(r.ljust(W))
    if len(out) > H:
        raise SystemExit('art taller than %d rows' % H)
    while len(out) < H:
        out.append(' ' * W)
    return out


def disc():
    """A compact disc: drawn rather than typed, because a circle in
    ASCII art is a circle nobody can read back."""
    g = [[' '] * W for _ in range(H)]
    cx, cy = 15.5, 15.5
    for y in range(H):
        for x in range(W):
            dx, dy = x - cx, y - cy
            d = (dx * dx + dy * dy) ** 0.5
            if d > 15.4:
                continue
            if d > 13.4:
                g[y][x] = '#'           # rim
            elif d > 5.8:
                g[y][x] = ':'           # the playing surface, dithered
            elif d > 4.6:
                g[y][x] = '#'           # ring around the hub
            else:
                g[y][x] = '.'           # the hub -- solid rather than
                                        #   holed, because the desktop
                                        #   draws the drive letter here
                                        #   and it needs clean paper
    # A clean arc of white off the upper left, so it reads as something
    # shiny rather than as a washer. A blob there read as a smudge.
    for y in range(H):
        for x in range(W):
            dx, dy = x - cx, y - cy
            d = (dx * dx + dy * dy) ** 0.5
            if 9.6 < d < 11.8 and dx < -1 and dy < 1 and g[y][x] == ':':
                g[y][x] = '.'
    return [''.join(r) for r in g]


# A 5x7 face, enough to write a word under an icon. Same shapes as the
# on-screen keyboard's font, which is where they came from.
FACE = {
    'R': ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    'A': ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    'M': ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
}


# A 5x5 face, for writing on an icon rather than under one.
#
# Two pixels a stroke, drawn that way rather than by doubling a 3x5: at
# three columns a letter's counter is one pixel wide, and doubling it
# closes -- "101" becomes four solid pixels and the R loses its bowl. So
# the strokes are two wide and the gaps are one, which needs five
# columns, and the whole face had to be redrawn to fit them.
#
# The first version was 3x5 with one-pixel strokes, on the reasoning
# that black plastic either side of a white pixel is contrast enough.
# It is, on a monitor. On a television it was barely there, which is how
# it was reported: legible only while the icon was selected, because
# selection inverts and thickens it.
SMALL = {
    'R': ["11110", "11011", "11110", "11010", "11011"],
    'A': ["01110", "11011", "11111", "11011", "11011"],
    # Five rows is not enough for an M's V. One filled row under the
    # shoulders suggests it; filling two makes a solid block with legs.
    'M': ["11011", "11111", "11011", "11011", "11011"],
}


def stamp_small(rows, text, top, left, ink='.'):
    """Write text into the art in `ink`, two pixels a stroke.

    Bold the same way stamp() is and for a related reason, but the
    reason is not the same one: stamp() writes over the desktop, where a
    one-pixel stroke falls into the dither pattern. This writes on the
    chip's own black plastic, where nothing competes with it -- and one
    pixel still disappeared at arm's length on a CRT.

    Six columns of pitch: five of glyph and one of gap."""
    g = [list(r) for r in rows]
    for i, ch in enumerate(text):
        for y, line in enumerate(SMALL[ch]):
            for x, c in enumerate(line):
                if c == '1':
                    g[top + y][left + i * 6 + x] = ink
    return [''.join(r) for r in g]


def stamp(rows, text, top):
    """Write text into the art, centred, in black.

    Bold, by drawing every stroke twice one pixel apart. A one-pixel
    stroke on a transparent background disappears into a dithered
    desktop pattern -- the checkerboard has a black pixel every other
    column and the letters stop being letters. Two pixels always has one
    of them landing on a light square."""
    g = [list(r) for r in rows]
    w = len(text) * 7 - 1               # 6-wide bold glyph, 1 of gap
    left = (W - w) // 2
    for i, ch in enumerate(text):
        for y, line in enumerate(FACE[ch]):
            for x, c in enumerate(line):
                if c == '1':
                    g[top + y][left + i * 7 + x] = '#'
                    g[top + y][left + i * 7 + x + 1] = '#'
    return [''.join(r) for r in g]


def ram():
    """The DIP package, with its name in white on the black between the
    top edge and the label window.

    That is where a chip's part marking actually is, and it stops the
    icon needing a caption of its own -- the desktop already writes the
    drive letter and the volume name underneath, so "RAM" in black below
    the package was a second caption arguing with the first.

    Ten blank rows above it. With the caption gone the package alone
    sat high in its cell, and EmuTOS's own hard disk -- which is what I:
    gets -- occupies rows 11..31. Two drive icons in a row want the same
    footing, so this one takes rows 10..31.

    The label window starts at 21 rather than 20, and that row is the
    reason: with one-pixel strokes the marking's last row touching the
    window above it went unnoticed, and with two-pixel strokes the M's
    legs ran into the white and looked like they were bleeding. The
    window loses nothing by it -- the desktop draws the drive letter at
    row 22 and it is five rows tall, so 21..27 still has a row of white
    above and below it."""
    art = frame([' '] * 10 + [
        '     ..    ..    ..    ..     ',
        '     ##    ##    ##    ##     ',
        '     ##    ##    ##    ##     ',
        '  ##########################  ',
        '  ##########################  ',
        '  ##########################  ',
        '  ###..#####################  ',
        '  ###..#####################  ',
        '  ##########################  ',
        '  ##########################  ',
        '  ##########################  ',
        '  ###....................###  ',
        '  ###....................###  ',
        '  ###....................###  ',
        '  ###....................###  ',
        '  ###....................###  ',
        '  ###....................###  ',
        '  ###....................###  ',
        '  ##########################  ',
        '     ##    ##    ##    ##     ',
        '     ##    ##    ##    ##     ',
        '     ..    ..    ..    ..     ',
    ])
    # Rows 15..19 are the black band: the body starts at 13 and the
    # label window at 20. The pin-1 dimple used to be four columns wide
    # across four rows; two-pixel strokes need six columns a letter
    # instead of four, and eighteen columns of word left it one black
    # column short of the dimple, which read as the two touching. So the
    # dimple is a 2x2 dot now. It is still the only thing on a DIP that
    # says which end pin 1 is, and at this size four pixels said it no
    # more clearly than sixteen.
    #
    # Left 9 centres the word in what is left of the package rather than
    # in the package: two black columns after the dimple, two before the
    # right edge.
    return stamp_small(art, 'RAM', 15, 9)


ICONS = {
    'ram': ram(),

    # D: the disc.
    'disc': disc(),
}

# Drive order A B C D. Two of them keep EmuTOS's own artwork: the
# cartridge is media you slot in and take out, which is what the floppy
# icon has always meant, and the console's eight kilobytes is a fixed
# internal store, which is what the hard disk icon has always meant.
# They only looked wrong when all four drives were wearing one or the
# other.
ORDER = ['stock:1', 'disc', 'ram', 'stock:0']


def to_bits(art):
    """(data, mask) as lists of 32 rows of 32 bits."""
    data, mask = [], []
    for y, row in enumerate(art):
        d = m = 0
        for x, c in enumerate(row):
            bit = 1 << (31 - x)
            if c == ' ':
                continue
            m |= bit
            if c == '#':
                d |= bit
            elif c == ':' and ((x + y) & 1) == 0:
                d |= bit
        data.append(d)
        mask.append(m)
    return data, mask


def stock_icon(n):
    """One of EmuTOS's own built-ins, read out of desk/icons.c so the preview
    shows the row as it will actually look rather than three new icons
    beside an imagined fourth."""
    import re as _re
    src = open('emutos/desk/icons.c').read()
    got = {}
    for m in _re.finditer(r'static const WORD (rs_icon(?:mask|data)\d+)\[\] = \{(.*?)\};',
                          src, _re.S):
        got[m.group(1)] = [int(v, 16)
                           for v in _re.findall(r'0x([0-9A-Fa-f]+)', m.group(2))]
    # icon 5 shares icon 0's mask, which is why the mask is looked up
    # by what exists rather than by index.
    d = got['rs_icondata%d' % n]
    m = got.get('rs_iconmask%d' % n, got['rs_iconmask0'])
    return ([(d[y * 2] << 16) | d[y * 2 + 1] for y in range(H)],
            [(m[y * 2] << 16) | m[y * 2 + 1] for y in range(H)])


def preview(path):
    S, PAD = 4, 6
    n = len(ORDER)
    w = (W * S + PAD) * n + PAD
    h = H * S + 2 * PAD
    img = [[(210, 210, 210)] * w for _ in range(h)]
    for i, name in enumerate(ORDER):
        if name.startswith('stock:'):
            data, mask = stock_icon(int(name.split(':')[1]))
        else:
            data, mask = to_bits(ICONS[name])
        ox, oy = PAD + i * (W * S + PAD), PAD
        for y in range(H):
            for x in range(W):
                bit = 1 << (31 - x)
                if not (mask[y] & bit):
                    continue
                c = (0, 0, 0) if (data[y] & bit) else (255, 255, 255)
                for dy in range(S):
                    for dx in range(S):
                        img[oy + y * S + dy][ox + x * S + dx] = c
    out = bytearray()
    for row in img:
        for p in row:
            out += bytes(p)
    with open(path, 'wb') as f:
        f.write(b'P6\n%d %d\n255\n' % (w, h))
        f.write(bytes(out))
    sys.stderr.write('%s: %s\n' % (path, ' '.join(ORDER)))





# ---------------------------------------------------------------------
# EMUICON.RSC
#
# rs_readit() in aes/gemrslib.c reads the 36-byte header, allocates
# rsh_rssize, reads the whole file in, and turns the ICONBLK offsets into
# pointers with fix_long -- which treats -1 as "no pointer" and anything
# else as a byte offset from the start of the file. Then setup_mono_icons()
# in desk/deskapp.c wants rsh_nib >= 8 and every ib_wicon/ib_hicon equal to
# the built-in's, and setup_iconblks() copies the lot into fresh memory
# before the resource is freed. So the smallest file that works is a
# header, an ICONBLK array, the bitmaps, and one NUL for the text
# pointers: no objects, no trees, no strings.
#
# The eight standard icons have to be here too, in their standard order,
# because the resource replaces the built-in set rather than extending it.
# They are copied out of desk/icons.c, which is EmuTOS's own GPL artwork
# under the same licence as this tree.

RSHDR_SIZE = 36
ICONBLK_SIZE = 34               # 3 longs + 11 words, no padding on m68k
ICON_BYTES = 128                # 32x32, 1bpp


def read_builtins():
    """The eight standard ICONBLKs and their bitmaps, out of icons.c."""
    import re as _re
    src = open('emutos/desk/icons.c').read()
    words = {}
    for m in _re.finditer(r'static const WORD (rs_icon(?:mask|data)\d+)\[\] = \{(.*?)\};',
                          src, _re.S):
        words[m.group(1)] = [int(v, 16)
                             for v in _re.findall(r'0x([0-9A-Fa-f]+)', m.group(2))]
    m = _re.search(r'const ICONBLK icon_rs_iconblk\[\] = \{(.*?)\n\};', src, _re.S)
    if not m:
        raise SystemExit('icons.c: icon_rs_iconblk not found')
    out = []
    for e in _re.finditer(r'\{\s*\(WORD \*\)(\w+),\s*\(WORD \*\)(\w+),\s*"",\s*'
                          r'([^,]+),\s*((?:-?\d+\s*,\s*){9}-?\d+)\s*\}', m.group(1)):
        mask, data, char, nums = e.groups()
        ch = _re.match(r"0x1000\|'(.*?)'$", char.strip())
        if not ch:
            raise SystemExit('cannot read ib_char %r' % char)
        lit = ch.group(1)
        val = 0 if lit in ('\\000',) else ord(lit)
        fields = [int(v) for v in nums.replace('\n', ' ').split(',')]
        out.append({'mask': words[mask], 'data': words[data],
                    'char': 0x1000 | val, 'fields': fields})
    if len(out) < 8:
        raise SystemExit('found %d built-in ICONBLKs, expected at least 8' % len(out))
    return out[:8]


def to_bytes(rows32):
    b = bytearray()
    for v in rows32:
        b += bytes(((v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF))
    return bytes(b)


def words_to_bytes(ws):
    b = bytearray()
    for v in ws:
        b += bytes(((v >> 8) & 0xFF, v & 0xFF))
    return bytes(b)


# Where the desktop draws the drive letter inside each new icon, and how
# big the icon is. The last eight numbers match the built-ins exactly so
# the new icons sit in the same box as the old.
EXTRA = [
    # Where the desktop draws the drive letter. It ORs the letter into
    # ib_char itself (deskapp.c:1397) and every built-in icon reserves a
    # spot for it, so these do too rather than fight it: the white
    # window in the package, and the disc's hub.
    # name    xchar ychar
    # The package moved down ten rows when its caption came off, so this
    # moved with it: at 12 the letter was drawn on the black band that
    # now carries the part marking, in black, on black, underneath it.
    ('ram',   13,   22),
    ('disc',  13,   13),
]


def write_rsc(path):
    builtin = read_builtins()
    blocks = []                 # (maskbytes, databytes, char, fields)

    for b in builtin:
        blocks.append((words_to_bytes(b['mask']), words_to_bytes(b['data']),
                       b['char'], b['fields']))

    ref = builtin[0]['fields']  # xicon yicon wicon hicon xtext ytext wtext htext
    for name, xc, yc in EXTRA:
        data, mask = to_bits(ICONS[name])
        blocks.append((to_bytes(mask), to_bytes(data), 0x1000,
                       [xc, yc] + ref[2:]))

    n = len(blocks)
    ib_off = RSHDR_SIZE
    bits_off = ib_off + n * ICONBLK_SIZE
    text_off = bits_off + n * 2 * ICON_BYTES
    total = text_off + 2       # one NUL for every ib_ptext, padded even

    hdr = [0,                  # rsh_vrsn
           total,              # rsh_object   (empty, point past the data)
           total,              # rsh_tedinfo
           ib_off,             # rsh_iconblk
           total,              # rsh_bitblk
           total,              # rsh_frstr
           total,              # rsh_string
           bits_off,           # rsh_imdata
           total,              # rsh_frimg
           total,              # rsh_trindex
           0, 0, 0,            # nobs, ntree, nted
           n,                  # rsh_nib
           0, 0, 0,            # nbb, nstring, nimages
           total]              # rsh_rssize

    out = bytearray(words_to_bytes(hdr))
    for i, (mask, data, char, f) in enumerate(blocks):
        pm = bits_off + i * 2 * ICON_BYTES
        pd = pm + ICON_BYTES
        out += bytes(((pm >> 24) & 0xFF, (pm >> 16) & 0xFF, (pm >> 8) & 0xFF, pm & 0xFF))
        out += bytes(((pd >> 24) & 0xFF, (pd >> 16) & 0xFF, (pd >> 8) & 0xFF, pd & 0xFF))
        out += bytes(((text_off >> 24) & 0xFF, (text_off >> 16) & 0xFF,
                      (text_off >> 8) & 0xFF, text_off & 0xFF))
        out += words_to_bytes([char] + f)
    for mask, data, char, f in blocks:
        out += mask + data
    out += b'\0\0'
    if len(out) != total:
        raise SystemExit('rsc is %d bytes, header says %d' % (len(out), total))
    open(path, 'wb').write(bytes(out))
    sys.stderr.write('%s: %d icons, %d bytes\n' % (path, n, total))


# ---------------------------------------------------------------------
# EMUDESK.INF
#
# Same shape build_inf() produces, with the icon number in each #M line
# chosen per drive instead of by `(i > 1) ? IG_HARD : IG_FLOPPY`.

IG_HARD, IG_FLOPPY, IG_TRASH, IG_PRINT = 0, 1, 3, 4
IG_RAM, IG_DISC = 8, 9          # our two, appended after the standard set

DRIVE_ICON = {
    'C': IG_RAM,        # the ramdisk: memory, not media, and the system
    'D': IG_DISC,       # the disc
    'I': IG_HARD,       # the console's own 8K, a fixed internal store
    'S': IG_FLOPPY,     # the cartridge: media you slot in and take out
}

INF_HEAD = ("#R 02\r\n"
            "#E 1A E0 00 00 60\r\n"
            "#Q 41 40 43 40 43 40\r\n"
            "#W 00 00 02 06 26 0C 00 @\r\n"
            "#W 00 00 02 08 26 0C 00 @\r\n"
            "#W 00 00 02 0A 26 0C 00 @\r\n"
            "#W 00 00 02 0D 26 0C 00 @\r\n")

INF_TAIL = ("#F FF 07 @ *.*@\r\n"
            "#N FF 07 @ *.*@\r\n"
            "#D FF 02 @ *.*@\r\n"
            "#Y 06 FF *.GTP@ @\r\n"
            "#G 06 FF *.APP@ @\r\n"
            "#G 06 FF *.PRG@ @\r\n"
            "#P 06 FF *.TTP@ @\r\n"
            "#F 06 FF *.TOS@ @\r\n")


def write_inf(path, drives='CDIS', xcnt=8, ycnt=6):
    s = INF_HEAD
    for i, d in enumerate(drives):
        s += "#M %02X %02X %02X FF %c DISK %c@ @\r\n" % (
            i % xcnt, i // xcnt, DRIVE_ICON[d], d, d)
    s += INF_TAIL
    s += "#T %02X %02X %02X FF   TRASH@ @\r\n" % (0, ycnt - 1, IG_TRASH)
    s += "#O %02X %02X %02X FF   PRINTER@ @\r\n" % (xcnt - 1, ycnt - 1, IG_PRINT)
    open(path, 'wb').write(s.encode('ascii'))
    sys.stderr.write('%s: %d bytes, drives %s\n' % (path, len(s), drives))


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--preview':
        preview(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == '--rsc':
        write_rsc(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == '--inf':
        write_inf(sys.argv[2])
    else:
        raise SystemExit('usage: mkicon.py --preview|--rsc|--inf <out>')
