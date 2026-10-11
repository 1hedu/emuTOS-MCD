#!/usr/bin/env python3
"""Vectorman, out of the owner's own cartridge, as VECTOR.MDD.

Vectorman is not stored as pictures, and not as sprite mappings either. He
is a skeleton: the game keeps joint angles for every limb, and each frame
it works out where twenty-one balls and limb pieces go and DMAs freshly
drawn limb graphics into VRAM for the five pieces that rotate. There is no
table in the ROM that says "frame 12 of the walk looks like this" -- the
frame only exists once the game has computed it.

So this does not read the ROM. It runs it. tools/gpgx-vdpdump.patch makes
Genesis Plus GX write the VDP and work RAM out after every frame, a
harness plays scripted pad input into the first level, and what comes
back is every pose the game actually drew: the sprite table, the tiles
the sprites name, and the game's own state beside them -- position,
speed, facing, which segment of the walk cycle it is in. Every animation
in VECTOR.MDD is a run of those frames, cut where the game's own state
says the animation starts and stops.

Captures, all from the start of the first level:

    idleR   standing, facing right, for 1550 frames: the breathing loop
            and the three things he does when he is bored
    idleL   the same, facing left
    movesR  walk, stop, turn, jump, land, look down, shoot, boost
    movesL  the same moves facing left
    rev     sixteen runs back and forth, each a different length, so the
    rev2    turnarounds land in every segment of the walk cycle; see
            walk_keys() for why that is needed
    stopL   a run to the left, and a stop

The emulator is deterministic and the ROM is checked by hash, so the
frame numbers below are the same on every run. Each cut is still checked
against the game's state rather than trusted, and the extractor stops
rather than write a file that is wrong.

Nothing this produces is committed or distributed. VECTOR.MDD is made of
Sega's graphics and lives in build/; the cartridge stays in assets/.
"""
import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vdp import Frame, pixels  # noqa: E402

ROM_SHA1 = '57a64d08028b539dc236a693d383f2e1269a5dd4'  # Vectorman (USA, Europe)

# ---- where the game keeps him ------------------------------------------
# Work RAM offsets, found by watching them change under the pad.
OBJ_X = 0xE364          # pixel; the fraction is the next word
OBJ_Y = 0xE368
OBJ_VX = 0xE36C         # 1/256 pixel a frame
OBJ_VY = 0xE36E
OBJ_FACING = 0xE348     # 8 facing right, 0 facing left
OBJ_SEG = 0xE39A        # which eighth of the walk cycle
CAM_X = 0xAF82
CAM_Y = 0xAF8A
GROUND_Y = 1280         # the floor he starts on

# ---- the captures --------------------------------------------------------
# Four presses of Start get through the title, the menu and the level
# intro. More than that pauses the game.
PREFIX = [(1300, 1305, 'START'), (1450, 1455, 'START'),
          (1600, 1605, 'START'), (1700, 1704, 'START')]


def runs(first, lengths, at=2000):
    """Back and forth, each run a different length."""
    out, d = [], first
    for n in lengths:
        out.append((at, at + n - 1, d))
        at += n
        d = 'LEFT' if d == 'RIGHT' else 'RIGHT'
    return out


CAPTURES = {
    'idleR': (PREFIX, 1950, 3500),
    'idleL': (PREFIX + [(2000, 2005, 'LEFT')], 1950, 3700),
    'movesR': (PREFIX + [(2000, 2119, 'RIGHT'), (2200, 2319, 'LEFT'),
                         (2320, 2379, 'RIGHT'), (2440, 2441, 'C'),
                         (2520, 2599, 'RIGHT'), (2540, 2541, 'C'),
                         (2660, 2719, 'DOWN'), (2760, 2761, 'B'),
                         (2800, 2860, 'B'), (2900, 2901, 'C'),
                         (2918, 2919, 'C')], 1990, 3000),
    'movesL': (PREFIX + [(2000, 2005, 'LEFT'), (2100, 2101, 'C'),
                         (2200, 2201, 'C'), (2218, 2219, 'C'),
                         (2300, 2301, 'B'), (2400, 2459, 'DOWN'),
                         (2520, 2521, 'RIGHT'), (2600, 2601, 'C'),
                         (2700, 2701, 'C'), (2718, 2719, 'C')], 1990, 2800),
    'rev': (PREFIX + runs('RIGHT', [44, 52, 60, 68, 76, 84, 92, 100,
                                    48, 56, 64, 72, 80, 88, 96, 104]),
            1990, 3250),
    'rev2': (PREFIX + runs('LEFT', [40, 58, 46, 66, 54, 74, 62, 82,
                                    70, 90, 78, 98]), 1990, 2820),
    # A stop facing left, well clear of the level's left wall: rev's last
    # run ends by walking into it, and that is not a stop.
    'stopL': (PREFIX + [(2000, 2099, 'RIGHT'), (2100, 2169, 'LEFT')], 1990, 2260),
}

# ---- the animations, and the order VECTOR.MDD stores them in -------------
# Each is stored twice, facing left (even) then facing right (odd). The
# payload has the same list; payload/vectorman/vm.c, A_LOOP and on.
ANIMS = ['LOOP', 'FIDGET1', 'FIDGET2', 'FIDGET3'] + \
        ['WALK%d' % s for s in range(8)] + \
        ['STOP', 'JUMP', 'LAND', 'BOOST', 'SHOOT', 'DOWN_IN', 'DOWN_OUT']
LEFT, RIGHT = 0, 1

# ---- sprites -------------------------------------------------------------
# His own twenty-one are on palette line 0 with tiles in 1400..1899. The
# effects are on line 3: the bolt at 128..136, the muzzle flash and the
# boost flames at 190..215, the casing and the sparks at 1927..1930.
# Everything else on line 3 near him is the level -- a pylon at 2032, a
# girder at 173, a light at 1931 -- and a range that took in the girder
# turned every fidget into three different ones.


def is_body(s):
    return s['pal'] == 0 and 1400 <= s['tile'] < 1900


def is_fx(s):
    t = s['tile']
    return s['pal'] == 3 and (128 <= t < 137 or 190 <= t < 216 or 1927 <= t < 1931)


class Gfx:
    """Every distinct sprite picture: (palette line, w, h, tile bytes)."""
    def __init__(self):
        self.ids = {}
        self.list = []

    def get(self, key):
        i = self.ids.get(key)
        if i is None:
            i = self.ids[key] = len(self.list)
            self.list.append(key)
        return i


class F:
    """One frame, reduced: the game's state and the sprites it drew."""
    __slots__ = ('f', 'fac', 'vx', 'vy', 'seg', 'y', 'body', 'spr', 'c0', 'c3')


def reduce(path, prev, gfx):
    d = Frame(path)
    fr = F()
    fr.fac = d.ram8(OBJ_FACING)
    fr.vx, fr.vy = d.sram16(OBJ_VX), d.sram16(OBJ_VY)
    fr.seg = d.ram8(OBJ_SEG)
    fr.y = d.ram16(OBJ_Y)
    # The sprite table is a frame behind the object: what is drawn now is
    # where he was when the last frame was computed.
    ax = prev.ram16(OBJ_X) - prev.ram16(CAM_X)
    ay = prev.ram16(OBJ_Y) - prev.ram16(CAM_Y)
    spr, body = [], []
    for s in d.sprites():
        b = is_body(s)
        if not (b or is_fx(s)):
            continue
        g = gfx.get((2 if b else 3, s['w'], s['h'], d.tiles(s['tile'], s['w'] * s['h'])))
        k = (g, s['hf'], s['vf'], s['x'] - ax, s['y'] - ay)
        spr.append(k)
        if b:
            body.append(k)
    fr.body, fr.spr = tuple(body), tuple(spr)
    fr.c0 = tuple(d.colour(n) for n in range(16))
    fr.c3 = tuple(d.colour(48 + n) for n in range(16))
    return fr, d


def capture(name, harness, core, rom, work, gfx):
    events, first, last = CAPTURES[name]
    cap = os.path.join(work, name)
    shutil.rmtree(cap, ignore_errors=True)
    os.makedirs(cap)
    ev = os.path.join(work, name + '.ev')
    with open(ev, 'w') as f:
        for a, b, k in events:
            f.write('%d %d %s\n' % (a, b, k))
    sysdir = os.path.join(work, 'system')
    os.makedirs(sysdir, exist_ok=True)
    env = dict(os.environ, GPGX_VDPDUMP=cap, GPGX_VDPDUMP_FROM=str(first - 1),
               GPGX_VDPDUMP_TO=str(last))
    subprocess.run([harness, '--core', core, '--rom', rom, '--frames', str(last),
                    '--input', ev, '--system-dir', sysdir],
                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   check=False)
    out, prev = {}, None
    for n in range(first - 1, last + 1):
        p = os.path.join(cap, 'f%05d.bin' % n)
        if not os.path.exists(p):
            raise SystemExit('%s: frame %d was not dumped -- did the harness '
                             'run the patched core?' % (name, n))
        if prev is None:
            prev = Frame(p)
            continue
        fr, prev = reduce(p, prev, gfx)
        fr.f = n
        out[n] = fr
    shutil.rmtree(cap)
    return out


# ---- the idle loop and what interrupts it ---------------------------------
def find_loop(frames):
    """The 32-frame breathing loop: the first place the sprites repeat
    with that period for three whole turns."""
    seq = [frames[k].body for k in sorted(frames)]
    for i in range(len(seq) - 128):
        if all(seq[i + j] == seq[i + j + 32] for j in range(96)):
            return seq[i:i + 32]
    raise SystemExit('no idle loop found')


def rejoin(seq, k, loop, need=8):
    """From index k on, the first place seq runs along the loop for `need`
    frames, and the loop phase it runs along at. None if it never does."""
    for e in range(k, len(seq) - need):
        for q in range(32):
            if all(seq[e + j] == loop[(q + j) % 32] for j in range(need)):
                return e, q
    return None


def deviations(frames, loop):
    """Everything that leaves the loop and comes back to it: (frames,
    phase it left at, phase it came back at)."""
    seq = [frames[k].body for k in sorted(frames)]
    r = rejoin(seq, 0, loop)
    k, ph = r
    out = []
    while k < len(seq):
        if seq[k] == loop[ph]:
            k += 1
            ph = (ph + 1) % 32
            continue
        r = rejoin(seq, k, loop)
        if r is None:
            break
        out.append((tuple(frames[f].spr for f in sorted(frames)[k:r[0]]), ph, r[1]))
        k, ph = r
    return out


def cut_to_loop(frames, start, loop, need=8):
    """From frame number start until he is back in the idle loop: the
    frames, and the loop phase he comes back at. "Back" is his own
    sprites; a casing still falling does not keep him out of it, and is
    kept in the frames it falls through."""
    keys = sorted(frames)
    seq = [frames[k].body for k in keys]
    k = keys.index(start)
    r = rejoin(seq, k, loop, need)
    if r is None:
        raise SystemExit('frame %d never returns to the idle loop' % start)
    return [frames[f].spr for f in keys[k:r[0]]], r[1]


def first_off_loop(frames, after, loop):
    """The first frame that leaves the idle loop, once he is in it at or
    after frame `after`."""
    keys = sorted(frames)
    seq = [frames[k].body for k in keys]
    r = rejoin(seq, keys.index(after), loop)
    if r is None:
        raise SystemExit('he is never in the idle loop after frame %d' % after)
    k, ph = r
    while seq[k] == loop[ph]:
        k += 1
        ph = (ph + 1) % 32
    return keys[k]


# ---- the walk ------------------------------------------------------------
def walk_keys(caps):
    """The walk cycle, 64 poses a facing.

    The game splits the cycle into eight segments of eight poses and runs
    each segment over eight frames -- or over four, two poses a frame, if
    he was near top speed when the segment began. So at full speed only
    the even poses are ever drawn, and the odd ones show only while he is
    speeding up. From a standstill he speeds up through the first four
    segments; the last four he only ever crosses slowly coming out of a
    turnaround, which is what rev and rev2 are for.

    Every frame that is plainly mid-walk is placed in the cycle by the
    game's own segment counter, and every pose the cycle position is seen
    with has to agree. They all do -- but this checks.
    """
    keys = {}
    for name in ('movesR', 'movesL', 'rev', 'rev2'):
        frames = caps[name]
        events = CAPTURES[name][0]

        def held(f):
            return {k for a, b, k in events if a <= f <= b}
        fs = sorted(frames)
        starts = [i for i in range(1, len(fs)) if frames[fs[i]].seg != frames[fs[i - 1]].seg]
        for a, b in zip(starts, starts[1:]):
            dur = b - a
            if dur not in (4, 8):
                continue
            ok = True
            for i in range(a, b):
                fr = frames[fs[i]]
                h = held(fr.f - 1) | held(fr.f)
                d = 1 if 'RIGHT' in h else -1 if 'LEFT' in h else 0
                if d == 0 or fr.vx * d <= 0 or fr.vy or fr.y != GROUND_Y:
                    ok = False
                if (fr.fac == 8) != (d == 1):
                    ok = False
            # A segment straight out of a standstill is not the cycle: the
            # game is blending from wherever the idle had got to.
            if sum(frames[fs[j]].vx == 0 for j in range(max(0, a - 12), a)) >= 2:
                ok = False
            if not ok:
                continue
            for t, i in enumerate(range(a, b)):
                fr = frames[fs[i]]
                k = fr.seg * 8 + t * (8 // dur)
                keys.setdefault((RIGHT if fr.fac == 8 else LEFT, k), set()).add(fr.body)
    out = {}
    for fac in (LEFT, RIGHT):
        for k in range(64):
            seen = keys.get((fac, k))
            if not seen:
                raise SystemExit('walk pose %d facing %s never seen' % (k, 'LR'[fac]))
            if len(seen) > 1:
                raise SystemExit('walk pose %d facing %s seen %d ways'
                                 % (k, 'LR'[fac], len(seen)))
            out[(fac, k)] = next(iter(seen))
    return out


# ---- the airborne ones -----------------------------------------------------
def jump_cut(frames, after):
    """The first jump from frame `after`: (takeoff index, landing index,
    boost index or None) as positions in the sorted frame list."""
    fs = sorted(frames)
    i = fs.index(after)
    while not (frames[fs[i]].vy == -2080 and frames[fs[i - 1]].vy == 0):
        i += 1
    t = i
    b = None
    i += 1
    while not (frames[fs[i]].y == GROUND_Y and frames[fs[i]].vy == 0):
        if b is None and frames[fs[i]].vy - frames[fs[i - 1]].vy < -1000:
            b = i
        i += 1
    return fs, t, i, b


def airborne(caps, name, after):
    """JUMP (takeoff to the falling pose), LAND (back to the loop) and,
    if there was a second press, BOOST (to the falling pose)."""
    frames = caps[name]
    fs, t, l, b = jump_cut(frames, after)
    hold = frames[fs[l]].spr               # the pose he comes down in
    seq = [frames[k].spr for k in fs]
    end = b if b is not None else l
    jump, m = None, t + 1
    while m <= end and seq[m] != hold:
        m += 1
    if m <= end:
        jump = seq[t + 1:m + 1]
    elif b is None:
        raise SystemExit('%s: the jump never reaches its falling pose' % name)
    boost = None
    if b is not None:
        m = b + 1
        while seq[m] != hold:
            m += 1
        boost = seq[b + 1:m + 1]
    return jump, hold, fs[l] + 1, boost


def mirror(sprites, gfx):
    """Effects facing the other way, as the game draws them: the x offset
    reflected about the anchor and the sprite flipped. Only used for the
    shot, whose left-facing capture has the bolt hitting the level."""
    out = []
    for g, hf, vf, dx, dy in sprites:
        w = gfx.list[g][1]
        out.append((g, hf ^ 1, vf, -dx - 8 * w, dy))
    return tuple(out)


# ---- VRAM ------------------------------------------------------------------
# What a payload may use without disturbing the desktop it is drawn over:
# the servant keeps the ST screen at tiles 0..1001, the nametables at
# $B000, $C000 and $E000, and the sprite table and scroll table at the
# top. 1024..1079 is the on-screen keyboard's (the payload turns the
# window plane off, and the servant uploads them again on the way out),
# and 1080..1083 is the pointer's, which nothing rewrites -- so it is
# stepped round. $F000..$F7FF, between plane B and the sprite table, is
# nobody's. docs/vectorman.md.
REGIONS = [(1024, 56), (1084, 132), (1216, 192), (1664, 128), (1920, 64)]


def place(gfx, used):
    """Give every picture in `used` a home in VRAM. A picture whose tiles
    are a run inside one already placed -- the muzzle flash is the same
    tiles cut four different sizes -- shares them."""
    regions = [[a, n, bytearray()] for a, n in REGIONS]
    where = {}
    for g in sorted(used, key=lambda g: -len(gfx.list[g][3])):
        data = gfx.list[g][3]
        hit = None
        for r in regions:
            img = bytes(r[2])
            at = img.find(data)
            while at >= 0 and at % 32:
                at = img.find(data, at + 1)
            if at >= 0:
                hit = r[0] + at // 32
                break
        if hit is None:
            for r in regions:
                if len(r[2]) // 32 + len(data) // 32 <= r[1]:
                    hit = r[0] + len(r[2]) // 32
                    r[2] += data
                    break
        if hit is None:
            raise SystemExit('VRAM: %d tiles will not fit in the %d there are'
                             % (sum(len(gfx.list[x][3]) for x in used) // 32,
                                sum(n for a, n in REGIONS)))
        where[g] = hit
    return where, [(r[0], bytes(r[2])) for r in regions if r[2]]


# ---- encoding ---------------------------------------------------------------
# A frame record:
#     u8 frames to show it for
#     u8 kind: 0 = his sprites whole, 1 = changes from the previous record
#     u8 his sprites (21)
#   whole:   per sprite: s16 dx, s16 dy, u8 picture, u8 flips (bit 0 h, 1 v)
#   changes: a bit per sprite, eight to a byte and the first sprite in
#            bit 7 of the first byte, set where it moved; then a byte for
#            each that did, dx change in the high nibble and dy change in
#            the low, each -8..7 -- 0x88 means "replaced below"; then u8
#            count, and per replaced sprite u8 slot and the six bytes a
#            whole record carries. Half of him is still from one frame
#            to the next while he idles, and the bits are what that
#            saves.
#     u8 effects in front of him, and six bytes each
#     u8 effects behind him, and six bytes each
# An animation always starts with a whole record, so it can be played
# from its beginning without knowing what came before.
#
# The effects are kept out of the changes because they blink: the casing
# and the boost's sparks are drawn every other frame, and with them in the
# same list the count changed every frame and every record was whole.
# There are never more than a dozen and they are written out each time.
ESC = 0x88


def spr_bytes(s, gid):
    g, hf, vf, dx, dy = s
    return struct.pack('>hhBB', dx, dy, gid[g], hf | (vf << 1))


def split(spr, gfx):
    """Front effects, his own sprites, back effects -- in the order the
    game chained them, which is the order they are drawn in."""
    kind = [gfx.list[s[0]][0] == 2 for s in spr]
    if True not in kind:
        raise SystemExit('a frame without him in it')
    a = kind.index(True)
    b = len(kind) - kind[::-1].index(True)
    if not all(kind[a:b]):
        raise SystemExit('an effect drawn in among his own sprites')
    return spr[:a], spr[a:b], spr[b:]


def encode(frames, gid, gfx):
    recs = []
    for spr in frames:
        if recs and recs[-1][1] == spr and recs[-1][0] < 255:
            recs[-1][0] += 1
        else:
            recs.append([1, spr])
    out = bytearray()
    prev = None
    for dur, spr in recs:
        front, body, back = split(spr, gfx)
        whole = prev is None or len(prev) != len(body)
        if not whole:
            mask, nib, chg = bytearray((len(body) + 7) // 8), bytearray(), []
            for i, (a, b) in enumerate(zip(prev, body)):
                ddx, ddy = b[3] - a[3], b[4] - a[4]
                if a == b:
                    continue
                mask[i >> 3] |= 0x80 >> (i & 7)
                if a[:3] == b[:3] and -8 <= ddx <= 7 and -8 <= ddy <= 7 \
                        and (ddx, ddy) != (-8, -8):
                    nib.append(((ddx & 15) << 4) | (ddy & 15))
                else:
                    nib.append(ESC)
                    chg.append(i)
            rec = bytes([dur, 1, len(body)]) + bytes(mask) + bytes(nib) + bytes([len(chg)]) + \
                b''.join(bytes([i]) + spr_bytes(body[i], gid) for i in chg)
            if len(rec) >= 3 + 6 * len(body):
                whole = True
        if whole:
            rec = bytes([dur, 0, len(body)]) + b''.join(spr_bytes(s, gid) for s in body)
        for fx in (front, back):
            rec += bytes([len(fx)]) + b''.join(spr_bytes(s, gid) for s in fx)
        out += rec
        prev = body
    return bytes(out), len(recs), len(frames)


def decode(blob, off, nrec):
    """The payload's decoder, in Python, run over what was just written.
    What comes out is each frame's sprites in drawing order."""
    def one(o):
        return list(struct.unpack('>hhBB', blob[o:o + 6]))
    cur, out = [], []
    for _ in range(nrec):
        dur, kind, n = blob[off], blob[off + 1], blob[off + 2]
        off += 3
        if kind == 0:
            cur = [one(off + 6 * i) for i in range(n)]
            off += 6 * n
        else:
            mask = blob[off:off + (n + 7) // 8]
            off += (n + 7) // 8
            for i in range(n):
                if not mask[i >> 3] & (0x80 >> (i & 7)):
                    continue
                b = blob[off]
                off += 1
                if b != ESC:
                    cur[i][0] += ((b >> 4) ^ 8) - 8
                    cur[i][1] += ((b & 15) ^ 8) - 8
            c = blob[off]
            off += 1
            for _ in range(c):
                cur[blob[off]] = one(off + 1)
                off += 7
        fx = []
        for _ in range(2):
            m = blob[off]
            fx.append([one(off + 1 + 6 * i) for i in range(m)])
            off += 1 + 6 * m
        frame = tuple(tuple(s) for s in fx[0] + cur + fx[1])
        out += [frame] * dur
    return out


def lowest(frames, gfx):
    """The lowest opaque row and the opaque x extent of his feet over a run
    of poses, relative to the anchor."""
    bottom, xs = -999, []
    for spr in frames:
        for g, hf, vf, dx, dy in spr:
            pal, w, h, data = gfx.list[g]
            rows = pixels(data, w, h)
            for y in range(8 * h):
                for x in range(8 * w):
                    if rows[y][x]:
                        yy = dy + (8 * h - 1 - y if vf else y)
                        bottom = max(bottom, yy)
    for spr in frames:
        for g, hf, vf, dx, dy in spr:
            pal, w, h, data = gfx.list[g]
            rows = pixels(data, w, h)
            for y in range(8 * h):
                yy = dy + (8 * h - 1 - y if vf else y)
                if yy < bottom - 3:
                    continue
                for x in range(8 * w):
                    if rows[y][x]:
                        xs.append(dx + (8 * w - 1 - x if hf else x))
    return bottom, min(xs), max(xs)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--harness', required=True)
    ap.add_argument('--core', required=True, help='gpgx with gpgx-vdpdump.patch')
    ap.add_argument('--rom', required=True)
    ap.add_argument('--work', required=True, help='scratch space for the dumps')
    ap.add_argument('-o', '--out', required=True)
    a = ap.parse_args()

    sha = hashlib.sha1(open(a.rom, 'rb').read()).hexdigest()
    if sha != ROM_SHA1:
        raise SystemExit('%s is not the cartridge this was measured on '
                         '(Vectorman (USA, Europe), SHA-1 %s); its frames would '
                         'not line up.' % (a.rom, ROM_SHA1))
    os.makedirs(a.work, exist_ok=True)
    gfx = Gfx()
    caps = {}
    for name in CAPTURES:
        print('capturing %s' % name, flush=True)
        caps[name] = capture(name, a.harness, a.core, a.rom, a.work, gfx)

    anims = {}          # (name, facing) -> (frames, entry phase, exit phase)
    loops = {}
    for fac, name in ((RIGHT, 'idleR'), (LEFT, 'idleL')):
        loop = find_loop(caps[name])
        devs = deviations(caps[name], loop)
        # Rotate the loop to start where his fidgets hand back to it, which
        # is also where stopping, landing and shooting do.
        back = max(set(d[2] for d in devs), key=lambda q: sum(d[2] == q for d in devs))
        loop = loop[back:] + loop[:back]
        loops[fac] = loop
        fidgets = []
        for seq, ent, ext in deviations(caps[name], loop):
            if all(seq != f[0] for f in fidgets):
                fidgets.append((seq, ent, ext))
        if len(fidgets) != 3:
            raise SystemExit('%s: %d fidgets, expected 3' % (name, len(fidgets)))
        fidgets.sort(key=lambda f: len(f[0]))
        anims[('LOOP', fac)] = (loop, 0, 0)
        for i, f in enumerate(fidgets):
            anims[('FIDGET%d' % (i + 1), fac)] = f

    walk = walk_keys(caps)
    for fac in (LEFT, RIGHT):
        for s in range(8):
            anims[('WALK%d' % s, fac)] = ([walk[(fac, 8 * s + t)] for t in range(8)], 0, 0)

    def walking(spr):
        return any(spr == p for p in walk.values())

    # Stopping: right from movesR's first release, left from rev's last.
    for fac, name, release in ((RIGHT, 'movesR', 2119), (LEFT, 'stopL', 2169)):
        frames = caps[name]
        f = release + 1
        while walking(frames[f].spr):
            f += 1
        seq, back = cut_to_loop(frames, f, loops[fac])
        anims[('STOP', fac)] = (seq, 0, back)

    holds = {}
    for fac, name, after in ((RIGHT, 'movesR', 2430), (LEFT, 'movesL', 2090)):
        jump, hold, landed, _ = airborne(caps, name, after)
        if jump is None:
            raise SystemExit('%s: no plain jump at %d' % (name, after))
        seq, back = cut_to_loop(caps[name], landed, loops[fac])
        anims[('JUMP', fac)] = (jump, 0, 0xFF)
        anims[('LAND', fac)] = (seq, 0, back)
        holds[fac] = hold
    for fac, name, after in ((RIGHT, 'movesR', 2890), (LEFT, 'movesL', 2190)):
        jump, hold, landed, boost = airborne(caps, name, after)
        if boost is None or hold != holds[fac]:
            raise SystemExit('%s: the boost did not come down like a jump' % name)
        anims[('BOOST', fac)] = (boost, 0, 0xFF)

    for fac, name, at in ((RIGHT, 'movesR', 2750), (LEFT, 'movesL', 2280)):
        frames = caps[name]
        f = first_off_loop(frames, at, loops[fac])
        # Two frames back in the loop is back: the right-facing capture
        # held B, and the second shot starts before eight have gone by.
        # None of the shot's own poses is a loop pose, so two is enough.
        seq, back = cut_to_loop(frames, f, loops[fac], need=2)
        anims[('SHOOT', fac)] = (seq, 0, back)
    # The left shot's bolt hits the level and bursts. Keep his own left-
    # facing frames and give him the right-facing shot's effects, mirrored.
    r, l = anims[('SHOOT', RIGHT)][0], anims[('SHOOT', LEFT)][0]
    if abs(len(r) - len(l)) > 2:
        raise SystemExit('the two shots are %d and %d frames' % (len(r), len(l)))
    bodies = [tuple(s for s in spr if gfx.list[s[0]][0] == 2) for spr in l]
    fx = [tuple(s for s in spr if gfx.list[s[0]][0] == 3) for spr in r] + [()] * len(l)
    anims[('SHOOT', LEFT)] = ([mirror(e, gfx) + b for b, e in zip(bodies, fx)],
                              0, anims[('SHOOT', LEFT)][2])

    for fac, name, at in ((RIGHT, 'movesR', 2650), (LEFT, 'movesL', 2390)):
        frames = caps[name]
        f = first_off_loop(frames, at, loops[fac])
        seq, back = cut_to_loop(frames, f, loops[fac])
        hold = max(set(seq), key=seq.count)
        i, j = seq.index(hold), len(seq) - 1 - seq[::-1].index(hold)
        anims[('DOWN_IN', fac)] = (seq[:i + 1], 0, 0xFF)
        anims[('DOWN_OUT', fac)] = (seq[j + 1:], 0, back)

    # ---- the palettes ------------------------------------------------------
    # Line 0 is his, and it is not constant: the level's lightning lights
    # him up for a frame at a time, and two entries cycle -- 9, the glow in
    # his joints, over 64 frames, and 10 over four. What goes in the file
    # is each entry's usual colour, and 64 frames of 9 and 10.
    allf = [fr for c in caps.values() for fr in c.values()]
    body = [max(set(fr.c0[n] for fr in allf), key=[fr.c0[n] for fr in allf].count)
            for n in range(16)]
    fxp = list(allf[0].c3)
    if any(fr.c3 != tuple(fxp) for fr in allf):
        raise SystemExit('palette line 3 changes')
    idle = [caps['idleR'][k] for k in sorted(caps['idleR'])]
    cyc = [(fr.c0[9], fr.c0[10]) for fr in idle[:64]]
    if any((fr.c0[9], fr.c0[10]) != cyc[i % 64] for i, fr in enumerate(idle)
           if fr.c0[1] == body[1]):
        raise SystemExit('entries 9 and 10 do not cycle every 64 frames')

    # ---- pictures, VRAM, and the file ----------------------------------------
    used = sorted({s[0] for (fr, e, x) in anims.values() for spr in fr for s in spr})
    if len(used) > 255:
        raise SystemExit('%d pictures: one byte names 255' % len(used))
    where, upload = place(gfx, used)
    gid = {g: i for i, g in enumerate(used)}

    foot, fx0, fx1 = lowest(loops[RIGHT], gfx)
    names = [(n, fac) for n in ANIMS for fac in (LEFT, RIGHT)]
    # Walking off an edge, or dropping through one, starts partway down a
    # jump: at the first frame past the top where his feet are back level
    # with where he stands. The top itself would do for the speed, but
    # his legs are tucked twenty pixels up there, and he would hop.
    jr = anims[('JUMP', RIGHT)][0]
    feet = [lowest([tuple(x for x in spr if gfx.list[x[0]][0] == 2)], gfx)[0] for spr in jr]
    top = feet.index(min(feet))
    jump_fall = next((i for i in range(top, len(jr)) if feet[i] >= foot - 1), len(jr) - 1)

    HDR = 360
    groups = b''.join(struct.pack('>HBB', where[g] | ((gfx.list[g][0]) << 13),
                                  ((gfx.list[g][1] - 1) << 2) | (gfx.list[g][2] - 1),
                                  gfx.list[g][1] * 8) for g in used)
    runtab_len = 8 * len(upload)
    animtab_len = 12 * len(names)
    off_runs = HDR
    off_groups = off_runs + runtab_len
    off_anims = off_groups + len(groups)
    off_data = off_anims + animtab_len
    tiles = bytearray()
    runtab = bytearray()
    for vt, data in upload:
        runtab += struct.pack('>HHI', vt, len(data) // 32, off_data + len(tiles))
        tiles += data
    off_recs = off_data + len(tiles)
    recs = bytearray()
    animtab = bytearray()
    sizes = {}
    for key in names:
        seq, ent, ext = anims[key]
        data, nrec, nfr = encode(seq, gid, gfx)
        if decode(data, 0, nrec) != [tuple((s[3], s[4], gid[s[0]], s[1] | (s[2] << 1))
                                           for s in spr) for spr in seq]:
            raise SystemExit('%s %s: does not decode to what was encoded' % key)
        animtab += struct.pack('>IHHBBH', off_recs + len(recs), nrec, nfr, ent, ext, 0)
        recs += data
        if len(recs) & 1:
            recs += b'\0'
        sizes[key[0]] = sizes.get(key[0], 0) + len(data)
    total = off_recs + len(recs)

    hdr = bytearray(HDR)
    struct.pack_into('>4sHHIHH', hdr, 0, b'VMDD', 1, HDR, total, len(upload), len(names))
    struct.pack_into('>III', hdr, 16, off_runs, off_groups, off_anims)
    struct.pack_into('>HhbbH', hdr, 28, len(used), foot + 1, fx0, fx1, jump_fall)
    struct.pack_into('>16H', hdr, 36, *body)
    struct.pack_into('>16H', hdr, 68, *fxp)
    for i, (c9, c10) in enumerate(cyc):
        struct.pack_into('>HH', hdr, 100 + 4 * i, c9, c10)
    blob = bytes(hdr) + bytes(runtab) + groups + bytes(animtab) + bytes(tiles) + bytes(recs)
    assert len(blob) == total
    with open(a.out, 'wb') as f:
        f.write(blob)

    print('%s: %d bytes -- %d pictures in %d tiles of VRAM, %d bytes of poses'
          % (a.out, total, len(used), len(tiles) // 32, len(recs)))
    for n in ANIMS:
        if not n.startswith('WALK') or n == 'WALK0':
            fl = [len(anims[(n if not n.startswith('WALK') else 'WALK%d' % s, fac)][0])
                  for fac in (LEFT, RIGHT) for s in (range(8) if n.startswith('WALK') else [0])]
            print('  %-9s %4d frames %6d bytes' % (n.rstrip('0') if n.startswith('WALK') else n,
                                                    sum(fl), sizes[n] if not n.startswith('WALK')
                                                    else sum(sizes['WALK%d' % s] for s in range(8))))
    print('  feet: bottom row %+d, x %+d..%+d; falls from jump frame %d'
          % (foot, fx0, fx1, jump_fall))


if __name__ == '__main__':
    main()
