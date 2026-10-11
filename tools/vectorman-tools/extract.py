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
    shots   eight runs back and forth with two shots in each, at sixteen
            different points of the stride; turns on the spot both ways;
            and a shot at four points of a jump, facing each way

The patch also logs every write to the sound chips, and four more runs
record his sound effects; see sounds().

The emulator is deterministic and the ROM is checked by hash, so the
frame numbers below are the same on every run. Each cut is still checked
against the game's state rather than trusted, and the extractor stops
rather than write a file that is wrong.

Nothing this produces is committed or distributed. VECTOR.MDD is made of
Sega's graphics and sounds and lives in build/; the cartridge stays in
assets/.
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


def shot_runs():
    """Eight hundred-frame runs, alternately right and left, each with a
    shot while he is still speeding up and one at full speed -- every pair
    at a different point of the stride. Between 1160 and 1540 in the level,
    clear of its left wall and of what is further right."""
    out, d = [], 'RIGHT'
    for i, (a, b) in enumerate([(10, 60), (12, 62), (14, 66), (16, 70),
                                (5, 58), (8, 56), (17, 72), (19, 74)]):
        s = 2000 + 100 * i
        out += [(s, s + 99, d), (s + a, s + a + 1, 'B'), (s + b, s + b + 1, 'B')]
        d = 'LEFT' if d == 'RIGHT' else 'RIGHT'
    out += [(2850, 2851, 'RIGHT'), (2950, 2951, 'LEFT'), (3000, 3001, 'RIGHT')]
    for c, b in ((3050, 3054), (3150, 3160), (3250, 3268), (3350, 3378)):
        out += [(c, c + 1, 'C'), (b, b + 1, 'B')]
    out += [(3440, 3441, 'LEFT')]
    for c, b in ((3500, 3504), (3600, 3610), (3700, 3718), (3800, 3828)):
        out += [(c, c + 1, 'C'), (b, b + 1, 'B')]
    # Sixteen more runs, three shots in each -- slow, middling and at full
    # speed -- to put the aiming walk's sixty-four poses all on the record.
    # Each shot aims for 23 frames, and each is clear of the one before
    # and of the turn at the end of its run.
    d = 'RIGHT'
    for i in range(16):
        s = 3950 + 120 * i
        e, m, l = 12 + (i * 5) % 9, 46 + (i * 7) % 9, 82 + (i * 3) % 9
        out += [(s, s + 119, d), (s + e, s + e + 1, 'B'), (s + m, s + m + 1, 'B'),
                (s + l, s + l + 1, 'B')]
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
    'shots': (PREFIX + shot_runs(), 1990, 5900),
}

# His sounds, which need no pictures: a shot, a jump (whose landing is
# the sound), and a jump with a boost.
SOUND_RUNS = {
    'sShot': PREFIX + [(2100, 2101, 'B')],
    'sJump': PREFIX + [(2100, 2101, 'C')],
    'sBoost': PREFIX + [(2100, 2101, 'C'), (2118, 2119, 'C')],
}

# ---- the animations, and the order VECTOR.MDD stores them in -------------
# Each is stored twice, facing left (even) then facing right (odd). The
# payload has the same list; payload/vectorman/vm.c, A_LOOP and on.
#   AIM0-7     the walk while aiming: the game's second 64-pose cycle
#   AIRSHOT    a shot in the air, ending in the falling pose
#   TURN       a turn on the spot, stored under the way he ends up facing
#   SHOTFX     a running shot's flash and bolt, and nothing of him: the
#              flash rides with him, the bolt keeps to where it was fired
#   AIRFX      the same for the shot in the air
ANIMS = ['LOOP', 'FIDGET1', 'FIDGET2', 'FIDGET3'] + \
        ['WALK%d' % s for s in range(8)] + \
        ['STOP', 'JUMP', 'LAND', 'BOOST', 'SHOOT', 'DOWN_IN', 'DOWN_OUT'] + \
        ['AIM%d' % s for s in range(8)] + \
        ['AIRSHOT', 'TURN', 'SHOTFX', 'AIRFX']
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


def is_bolt(s):
    return 128 <= s['tile'] < 137


# Which part of him a sprite is, from the tile the game drew it at: the
# five limb pieces have a slot each, and the balls are told apart by
# their drawing. The payload blends one pose into another by moving
# each sprite to the nearest one of the same part (payload/vectorman/
# vm.c, blend), so a hand never turns into a foot on the way. 0 is an
# effect, and is never blended.
PARTS = {1850: 1, 1856: 2, 1862: 2, 1868: 3, 1874: 3,
         1441: 4, 1451: 4, 1455: 4, 1459: 4,
         1445: 5, 1434: 6, 1438: 7, 1439: 8, 1440: 9, 1449: 10, 1450: 11}


class Gfx:
    """Every distinct sprite picture: (palette line, w, h, tile bytes,
    part). Two parts drawn the same are two pictures; they share their
    tiles in VRAM, place() sees to that."""
    def __init__(self):
        self.ids = {}
        self.list = []
        self.bolt = set()

    def get(self, key):
        i = self.ids.get(key)
        if i is None:
            i = self.ids[key] = len(self.list)
            self.list.append(key)
        return i


class F:
    """One frame, reduced: the game's state and the sprites it drew."""
    __slots__ = ('f', 'fac', 'vx', 'vy', 'seg', 'y', 'wx', 'wy',
                 'body', 'spr', 'front', 'back', 'c0', 'c3')


def reduce(path, prev, gfx):
    d = Frame(path)
    fr = F()
    fr.fac = d.ram8(OBJ_FACING)
    fr.vx, fr.vy = d.sram16(OBJ_VX), d.sram16(OBJ_VY)
    fr.seg = d.ram8(OBJ_SEG)
    fr.y = d.ram16(OBJ_Y)
    # The sprite table is a frame behind the object: what is drawn now is
    # where he was when the last frame was computed.
    fr.wx, fr.wy = prev.ram16(OBJ_X), prev.ram16(OBJ_Y)
    ax = fr.wx - prev.ram16(CAM_X)
    ay = fr.wy - prev.ram16(CAM_Y)
    spr, body = [], []
    for s in d.sprites():
        b = is_body(s)
        if not (b or is_fx(s)):
            continue
        g = gfx.get((2 if b else 3, s['w'], s['h'], d.tiles(s['tile'], s['w'] * s['h']),
                     PARTS.get(s['tile'], 12) if b else 0))
        if is_bolt(s):
            gfx.bolt.add(g)
        k = (g, s['hf'], s['vf'], s['x'] - ax, s['y'] - ay)
        spr.append(k)
        if b:
            body.append(k)
    fr.body, fr.spr = tuple(body), tuple(spr)
    kind = [gfx.list[k[0]][0] == 2 for k in spr]
    if True in kind:
        a = kind.index(True)
        b = len(kind) - kind[::-1].index(True)
        fr.front, fr.back = fr.spr[:a], fr.spr[b:]
    else:
        fr.front, fr.back = (), fr.spr
    fr.c0 = tuple(d.colour(n) for n in range(16))
    fr.c3 = tuple(d.colour(48 + n) for n in range(16))
    return fr, d


def write_events(path, events):
    with open(path, 'w') as f:
        for a, b, k in events:
            f.write('%d %d %s\n' % (a, b, k))


def capture(name, harness, core, rom, work, gfx):
    events, first, last = CAPTURES[name]
    cap = os.path.join(work, name)
    shutil.rmtree(cap, ignore_errors=True)
    os.makedirs(cap)
    ev = os.path.join(work, name + '.ev')
    write_events(ev, events)
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


def held(events, f):
    return {k for a, b, k in events if a <= f <= b}


def presses(events, key):
    return [a for a, b, k in events if k == key]


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
    keys = sorted(frames)
    seq = [frames[k].body for k in keys]
    k, ph = rejoin(seq, 0, loop)
    out = []
    while k < len(seq):
        if seq[k] == loop[ph]:
            k += 1
            ph = (ph + 1) % 32
            continue
        r = rejoin(seq, k, loop)
        if r is None:
            break
        out.append((tuple(frames[f].spr for f in keys[k:r[0]]), ph, r[1]))
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
# A running shot: the press, then two frames later the pose changes, and
# for AIM_FRAMES frames from there he runs aiming. The first of those
# frames eases into the aim and is not part of either cycle.
SHOT_ONSET = 2
AIM_HOLD = 15       # frames from the onset that the aim holds still


def walk_cycles(caps, gfx):
    """The walk cycle and the aiming walk, 64 poses a facing each.

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

    A shot on the run does not stop the cycle. For the 23 frames he aims,
    the game draws a second cycle -- the same legs, his arm out and his
    body leaning into it -- at whatever point the first has reached, and
    then goes back to the first. Its pose depends on that point and on
    nothing else: the same aiming pose turns up at the same point of the
    stride in shots fired at different times. shots captures it at
    sixteen points of the stride, half of them while he is still speeding
    up.

    The segment counter is no use for placing those: straight after a
    turnaround it runs a short segment while the poses still step one at
    a time, and shots turns round every hundred frames. The legs place
    them instead. His feet in an aiming pose are exactly where they are
    in the walk pose at the same point, so each aiming frame goes where
    its feet match, the counter only breaking a tie.
    """
    keys = {}
    for name in ('movesR', 'movesL', 'rev', 'rev2'):
        frames = caps[name]
        events = CAPTURES[name][0]
        fs = sorted(frames)
        starts = [i for i in range(1, len(fs)) if frames[fs[i]].seg != frames[fs[i - 1]].seg]
        for a, b in zip(starts, starts[1:]):
            dur = b - a
            if dur not in (4, 8):
                continue
            ok = True
            for i in range(a, b):
                fr = frames[fs[i]]
                h = held(events, fr.f - 1) | held(events, fr.f)
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
                keys.setdefault(('WALK', RIGHT if fr.fac == 8 else LEFT, k), set()).add(fr.body)
    out = {}
    for fac in (LEFT, RIGHT):
        for k in range(64):
            seen = keys.get(('WALK', fac, k))
            if not seen:
                raise SystemExit('walk pose %d facing %s never seen' % (k, 'LR'[fac]))
            if len(seen) > 1:
                raise SystemExit('walk pose %d facing %s seen %d ways'
                                 % (k, 'LR'[fac], len(seen)))
            out[('WALK', fac, k)] = next(iter(seen))
    walkset = {out[('WALK', f, k)] for f in (LEFT, RIGHT) for k in range(64)}

    # Placing an aiming frame: by where his feet are, and only that. The
    # aim lowers his hips a pixel or two, so his knees do not match the
    # walk; and facing right the game draws the aiming limbs mirrored with
    # the flip bit the other way, which looks the same and compares
    # different. The feet's positions are the same, and no two walk poses
    # share them.
    def feet(body):
        return tuple(sorted((s[3], s[4]) for s in body if gfx.list[s[0]][4] == 2))
    byfeet = {}
    for fac in (LEFT, RIGHT):
        for k in range(64):
            byfeet.setdefault((fac, feet(out[('WALK', fac, k)])), []).append(k)
    if any(len(v) > 1 for v in byfeet.values()):
        raise SystemExit('two walk poses with the same feet')

    frames = caps['shots']
    events = CAPTURES['shots'][0]
    aim_len = []
    aimkeys = {}
    for p in presses(events, 'B'):
        if not ('RIGHT' in held(events, p) or 'LEFT' in held(events, p)):
            continue
        n = 0
        while frames[p + SHOT_ONSET + n].body not in walkset and n < 60:
            n += 1
        aim_len.append(n)
        # The aim holds still for its first fifteen frames and then eases
        # back towards the walk; only the first fifteen are the cycle, and
        # the first of those is the easing in.
        for f in range(p + SHOT_ONSET + 1, p + SHOT_ONSET + AIM_HOLD):
            fr = frames[f]
            fac = RIGHT if fr.fac == 8 else LEFT
            ks = byfeet.get((fac, feet(fr.body)))
            if ks:
                aimkeys.setdefault((fac, ks[0]), []).append(fr.body)
    filled = 0
    for fac in (LEFT, RIGHT):
        have = [k for k in range(64) if (fac, k) in aimkeys]
        if len(have) < 48:
            raise SystemExit('only %d aiming poses facing %s' % (len(have), 'LR'[fac]))
        for k in range(64):
            seen = aimkeys.get((fac, k))
            if seen:
                # one stride point is drawn two ways; the commoner wins
                out[('AIM', fac, k)] = max(set(seen), key=seen.count)
        for k in range(64):
            if ('AIM', fac, k) not in out:
                # Not on the record: the nearest that is, a pixel or two off.
                near = min(have, key=lambda h: min((h - k) % 64, (k - h) % 64))
                out[('AIM', fac, k)] = out[('AIM', fac, near)]
                filled += 1
    common = max(set(aim_len), key=aim_len.count)
    if aim_len.count(common) < len(aim_len) // 2:
        raise SystemExit('aiming lasts %s frames: no agreement' % sorted(aim_len))
    if filled:
        print('  %d aiming poses borrowed from a neighbour' % filled)
    return out, common, walkset


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


def effects(frames, f0, n):
    """n frames of a shot's effects from frame f0, and nothing of him: the
    flash where it was drawn relative to him, the bolt relative to where
    he was when it was fired -- the game's own world coordinates, so that
    his running or falling since does not carry it along."""
    x0, y0 = frames[f0].wx, frames[f0].wy

    # Whatever of the level was already drawn the frame before is not the
    # shot's: the left-facing air shot has a piece of scenery in it, which
    # sits still in the world while everything of the shot moves.
    def world(fr, s):
        g, hf, vf, dx, dy = s
        return g, hf, vf, dx + fr.wx, dy + fr.wy
    pre = frames[f0 - 1]
    level = {world(pre, s) for s in pre.front + pre.back}

    out = []
    for f in range(f0, f0 + n):
        fr = frames[f]
        front = tuple(s for s in fr.front if world(fr, s) not in level)
        back = tuple((g, hf, vf, dx + fr.wx - x0, dy + fr.wy - y0)
                     for g, hf, vf, dx, dy in fr.back
                     if world(fr, (g, hf, vf, dx, dy)) not in level)
        out.append((front, (), back))
    while out and out[-1] == ((), (), ()):
        out.pop()
    return out


# ---- sound ---------------------------------------------------------------
def load_sound(path):
    """The sound chip log as {frame: [(port, register, value)]} for the
    YM2612, in the order written."""
    per, cur = {}, [0, 0]
    for line in open(path):
        f, c, chip, a, v = line.split()
        if chip != 'Y':
            continue
        f, a, v = int(f), int(a), int(v)
        if a & 1 == 0:
            cur[a >> 1] = v
        else:
            per.setdefault(f, []).append((a >> 1, cur[a >> 1], v))
    return per


def channel(port, reg, v):
    """Which FM channel a write is for, 0..5, or None."""
    if reg == 0x28:
        c = v & 7
        return None if c & 3 == 3 else (c & 3) + (3 if c & 4 else 0)
    if 0x30 <= reg <= 0xB6 and reg & 3 != 3:
        return (reg & 3) + 3 * port
    return None


def fm_effect(log, ref, after, ch, to):
    """An FM effect, frame by frame: everything written to channel ch from
    the first time a voice is loaded into it after frame `after` until the
    first frame it is left alone -- moved to channel `to`, so that on this
    side the three effects never have to share one.

    While an effect plays the game's driver keeps the music off its
    channel, so in that window every write to it is the effect's. The
    music uses all six channels, and the start is where it can be told
    apart: a voice load, thirty-odd registers and a key-on in one frame,
    that the same run without the press -- `ref` -- did not make."""
    f = after
    while True:
        w = [x for x in log.get(f, ()) if channel(*x) == ch]
        r0 = [x for x in ref.get(f, ()) if channel(*x) == ch]
        if len(w) >= 20 and any(r == 0x28 and v & 0xF0 for p, r, v in w) and w != r0:
            break
        f += 1
        if f > after + 60:
            raise SystemExit('no effect on channel %d after frame %d' % (ch, after))
    out, quiet = [], 0
    while True:
        w = [x for x in log.get(f, ()) if channel(*x) == ch]
        if not w:
            # The shot rests for three frames between its click and its
            # sweep; six is the end of it. The music's next note on the
            # channel is never sooner than seventeen.
            quiet += 1
            if quiet == 6:
                break
            out.append([])
            f += 1
            continue
        quiet = 0
        frame = []
        for p, r, v in w:
            if r == 0x28:
                frame.append((0, 0x28, (v & 0xF0) | (to % 3) | (4 if to >= 3 else 0)))
            else:
                frame.append((1 if to >= 3 else 0, (r & ~3) | (to % 3), v))
        out.append(frame)
        f += 1
    while out and not out[-1]:
        out.pop()
    # and off at the end, whatever the game left it doing
    out.append([(0, 0x28, (to % 3) | (4 if to >= 3 else 0))])
    return out


def voices(effects):
    """The voices out of the FM effects, to be loaded once.

    A voice load is thirty-odd register writes, and the YM2612 wants a
    pause after each; done in the frame an effect starts it took a sixth
    of the frame, every shot. But the game never changes a voice once it
    is loaded -- what follows is a frequency a frame and the key -- so
    each voice an effect loads is given a channel of its own, written
    there once when he arrives, and the effect keeps only the rest. The
    shot loads two in turn (its click and its sweep), the boost one; the
    sixth channel is the DAC's.

    Returns the setup writes and the effects, each frame's writes moved
    to the channel of the voice last loaded. An effect starts by keying
    off every channel it uses, so that one fired over itself starts
    clean."""
    free = [3, 0, 4, 1, 2]
    setup, out = [], []

    def to(ch, p, r, v):
        if r == 0x28:
            return 0, 0x28, (v & 0xF0) | (ch % 3) | (4 if ch >= 3 else 0)
        return (1 if ch >= 3 else 0), (r & ~3) | (ch % 3), v

    for fx in effects:
        cur, used, frames = None, [], []
        for frame in fx:
            voice = [w for w in frame if 0x30 <= w[1] <= 0x9F or 0xB0 <= w[1] <= 0xB6]
            if len(voice) >= 20:
                if not free:
                    raise SystemExit('more voices than channels')
                cur = free.pop(0)
                used.append(cur)
                setup += [to(cur, *w) for w in voice]
                frame = [w for w in frame if w not in voice]
            if cur is None:
                raise SystemExit('an FM effect that writes before it loads a voice')
            frames.append([to(cur, *w) for w in frame])
        off = [to(c, 0, 0x28, 0) for c in used]
        frames[0] = off + frames[0]
        frames[-1] = frames[-1] + [w for w in off if w not in frames[-1]]
        out.append(frames)
    return setup, out


def dac_effect(log, after):
    """A sampled effect: the bytes written to the DAC, in order, from the
    first frame after `after` that writes any."""
    f = after
    while not any(r == 0x2A for p, r, v in log.get(f, ())):
        f += 1
        if f > after + 120:
            raise SystemExit('no sample after frame %d' % after)
    out = bytearray()
    while any(r == 0x2A for p, r, v in log.get(f, ())):
        out += bytes(v for p, r, v in log[f] if p == 0 and r == 0x2A)
        f += 1
    return bytes(out)


def sounds(harness, core, rom, work):
    """The shot, the boost and the landing.

    The jump itself is silent. The shot and the boost are FM, on the
    sixth channel; the landing is a sample, played through the DAC at
    8860 Hz. Each comes from its own run, the moment after the press, so
    that it is the effect and not the level's music that is recorded --
    a shot fired a few seconds later finds the music using the channel,
    and the driver gives the music the right of way."""
    logs = {}
    sysdir = os.path.join(work, 'system')
    os.makedirs(sysdir, exist_ok=True)
    for name, events in SOUND_RUNS.items():
        ev = os.path.join(work, name + '.ev')
        write_events(ev, events)
        log = os.path.join(work, name + '.snd')
        subprocess.run([harness, '--core', core, '--rom', rom, '--frames', '2250',
                        '--input', ev, '--system-dir', sysdir],
                       env=dict(os.environ, GPGX_SNDLOG=log),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        if not os.path.exists(log):
            raise SystemExit('%s: no sound log -- is the core patched?' % name)
        logs[name] = load_sound(log)
        os.remove(log)
        os.remove(ev)
    shot = fm_effect(logs['sShot'], logs['sJump'], 2100, 5, 3)
    boost = fm_effect(logs['sBoost'], logs['sJump'], 2118, 5, 4)
    land = dac_effect(logs['sJump'], 2130)
    if not (15 <= len(shot) <= 45 and 15 <= len(boost) <= 45 and 1000 <= len(land) <= 6000):
        raise SystemExit('the sounds are not what was measured: %d, %d frames, %d samples'
                         % (len(shot), len(boost), len(land)))
    return shot, boost, land


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


def hflip(data, w, h):
    """A sprite picture mirrored left to right: its columns of tiles in
    the other order, and every row of every tile reversed. Big-endian
    tiles, two pixels a byte, the left one in the high nibble."""
    tiles = [data[i * 32:(i + 1) * 32] for i in range(w * h)]
    out = bytearray()
    for c in range(w):
        for r in range(h):
            t = tiles[(w - 1 - c) * h + r]
            for y in range(8):
                row = t[4 * y:4 * y + 4]
                out += bytes(((b & 15) << 4) | (b >> 4) for b in reversed(row))
    return bytes(out)


def canonical(gfx, used):
    """Pictures that are another picture mirrored: drawn instead as that
    one with the flip bit the other way. Facing right while he aims, the
    game draws his limbs pre-mirrored with the flip bit off, and every
    one of those is a left-facing limb VRAM already holds."""
    seen, alias = {}, {}
    for g in sorted(used):
        pal, w, h, data, part = gfx.list[g]
        key = (pal, w, h, part)
        m = seen.get(key + (hflip(data, w, h),))
        if m is not None:
            alias[g] = (m, 1)
        else:
            seen.setdefault(key + (data,), g)
            alias[g] = (g, 0)
    return alias


def place(gfx, used):
    """Give every picture in `used` a home in VRAM. A picture whose tiles
    are a run inside one already placed -- the muzzle flash is the same
    tiles cut four different sizes, and two parts can be drawn the
    same -- shares them."""
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
                             % (sum(len(r[2]) for r in regions) // 32 + len(data) // 32,
                                sum(n for a, n in REGIONS)))
        where[g] = hit
    return where, [(r[0], bytes(r[2])) for r in regions if r[2]]


# ---- encoding ---------------------------------------------------------------
# A frame record:
#     u8 frames to show it for
#     u8 kind: 0 = his sprites whole, 1 = changes from the previous record
#     u8 his sprites (21, or none in an effects-only animation)
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
    g, flip = gid[g]
    return struct.pack('>hhBB', dx, dy, g, (hf ^ flip) | (vf << 1))


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


def encode(frames, gid):
    """frames: (front, body, back) triples."""
    recs = []
    for tri in frames:
        if recs and recs[-1][1] == tri and recs[-1][0] < 255:
            recs[-1][0] += 1
        else:
            recs.append([1, tri])
    out = bytearray()
    prev = None
    for dur, (front, body, back) in recs:
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
            pal, w, h, data, part = gfx.list[g]
            rows = pixels(data, w, h)
            for y in range(8 * h):
                for x in range(8 * w):
                    if rows[y][x]:
                        yy = dy + (8 * h - 1 - y if vf else y)
                        bottom = max(bottom, yy)
    for spr in frames:
        for g, hf, vf, dx, dy in spr:
            pal, w, h, data, part = gfx.list[g]
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
    print('recording sounds', flush=True)
    sfx_shot, sfx_boost, sfx_land = sounds(a.harness, a.core, a.rom, a.work)
    sfx_setup, (sfx_shot, sfx_boost) = voices((sfx_shot, sfx_boost))

    def tri(spr):
        return split(spr, gfx)

    anims = {}          # (name, facing) -> (triples, entry phase, exit phase)
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
        anims[('LOOP', fac)] = ([tri(s) for s in loop], 0, 0)
        for i, (seq, ent, ext) in enumerate(fidgets):
            anims[('FIDGET%d' % (i + 1), fac)] = ([tri(s) for s in seq], ent, ext)

    cyc, aim_frames, walkset = walk_cycles(caps, gfx)
    for fac in (LEFT, RIGHT):
        for s in range(8):
            for c in ('WALK', 'AIM'):
                anims[('%s%d' % (c, s), fac)] = (
                    [tri(cyc[(c, fac, 8 * s + t)]) for t in range(8)], 0, 0)

    def walking(spr):
        return spr in walkset

    # Stopping: right from movesR's first release, left from stopL's.
    for fac, name, release in ((RIGHT, 'movesR', 2119), (LEFT, 'stopL', 2169)):
        frames = caps[name]
        f = release + 1
        while walking(frames[f].spr):
            f += 1
        seq, back = cut_to_loop(frames, f, loops[fac])
        anims[('STOP', fac)] = ([tri(s) for s in seq], 0, back)

    holds = {}
    for fac, name, after in ((RIGHT, 'movesR', 2430), (LEFT, 'movesL', 2090)):
        jump, hold, landed, _ = airborne(caps, name, after)
        if jump is None:
            raise SystemExit('%s: no plain jump at %d' % (name, after))
        seq, back = cut_to_loop(caps[name], landed, loops[fac])
        anims[('JUMP', fac)] = ([tri(s) for s in jump], 0, 0xFF)
        anims[('LAND', fac)] = ([tri(s) for s in seq], 0, back)
        holds[fac] = hold
    for fac, name, after in ((RIGHT, 'movesR', 2890), (LEFT, 'movesL', 2190)):
        jump, hold, landed, boost = airborne(caps, name, after)
        if boost is None or hold != holds[fac]:
            raise SystemExit('%s: the boost did not come down like a jump' % name)
        anims[('BOOST', fac)] = ([tri(s) for s in boost], 0, 0xFF)

    # The standing shot. Its bolt is taken out: every shot's bolt is the
    # running shot's, flown in the desktop's own coordinates by the payload
    # (SHOTFX), because the one recorded here stops dead a hundred and
    # thirty pixels out -- it hit something in the level. The flash and
    # the casing stay.
    def nobolt(t):
        return (tuple(s for s in t[0] if s[0] not in gfx.bolt), t[1],
                tuple(s for s in t[2] if s[0] not in gfx.bolt))
    stand_bolt = {}
    for fac, name, at in ((RIGHT, 'movesR', 2750), (LEFT, 'movesL', 2280)):
        frames = caps[name]
        f = first_off_loop(frames, at, loops[fac])
        # Two frames back in the loop is back: the right-facing capture
        # held B, and the second shot starts before eight have gone by.
        # None of the shot's own poses is a loop pose, so two is enough.
        seq, back = cut_to_loop(frames, f, loops[fac], need=2)
        anims[('SHOOT', fac)] = ([tri(s) for s in seq], 0, back)
    # The left shot's bolt hits the level and bursts. Keep his own left-
    # facing frames and give him the right-facing shot's effects, mirrored.
    r, l = anims[('SHOOT', RIGHT)][0], anims[('SHOOT', LEFT)][0]
    if abs(len(r) - len(l)) > 2:
        raise SystemExit('the two shots are %d and %d frames' % (len(r), len(l)))
    r = r + [((), (), ())] * len(l)
    anims[('SHOOT', LEFT)] = ([(mirror(fr[0], gfx), b[1], mirror(fr[2], gfx))
                               for b, fr in zip(l, r)], 0, anims[('SHOOT', LEFT)][2])
    for fac in (LEFT, RIGHT):
        seq = anims[('SHOOT', fac)][0]
        i = next(i for i, t in enumerate(seq) if any(s[0] in gfx.bolt for s in t[2]))
        b = next(s for s in seq[i][2] if s[0] in gfx.bolt)
        stand_bolt[fac] = (i, b[3], b[4])
        anims[('SHOOT', fac)] = ([nobolt(t) for t in seq], 0, anims[('SHOOT', fac)][2])

    for fac, name, at in ((RIGHT, 'movesR', 2650), (LEFT, 'movesL', 2390)):
        frames = caps[name]
        f = first_off_loop(frames, at, loops[fac])
        seq, back = cut_to_loop(frames, f, loops[fac])
        hold = max(set(seq), key=seq.count)
        i, j = seq.index(hold), len(seq) - 1 - seq[::-1].index(hold)
        anims[('DOWN_IN', fac)] = ([tri(s) for s in seq[:i + 1]], 0, 0xFF)
        anims[('DOWN_OUT', fac)] = ([tri(s) for s in seq[j + 1:]], 0, back)

    # ---- from the shots capture ---------------------------------------------
    shots = caps['shots']
    events = CAPTURES['shots'][0]
    bs = presses(events, 'B')

    # Turning on the spot: a tap the other way from the idle loop. Stored
    # under the way he ends up facing; it hands over to that facing's loop.
    for fac, at in ((RIGHT, 2840), (LEFT, 2940)):
        f = first_off_loop(shots, at, loops[1 - fac])
        seq, back = cut_to_loop(shots, f, loops[fac])
        anims[('TURN', fac)] = ([tri(s) for s in seq], 0, back)

    # A running shot's effects: the second shot of the first run, facing
    # right, and of the second, facing left -- both at full speed, and
    # both fifty frames clear of the shot before, so nothing else is in
    # the air. The flash and the bolt only; he is the AIM cycle.
    for fac, p in ((RIGHT, bs[1]), (LEFT, bs[3])):
        f0 = p + SHOT_ONSET
        if shots[f0 - 1].spr != shots[f0 - 1].body:
            raise SystemExit('frame %d: something already in the air' % f0)
        anims[('SHOTFX', fac)] = (effects(shots, f0, 48), 0, 0xFF)

    # A shot in the air: from where the jump gives way to the shot, until
    # he is falling in the jump's own falling pose. His sprites are the
    # AIRSHOT animation and the effects AIRFX, so that the bolt can stay
    # where it was fired while he falls.
    for fac, p in ((RIGHT, 3054), (LEFT, 3504)):
        jump = {t[1] for t in anims[('JUMP', fac)][0]}
        hold = anims[('JUMP', fac)][0][-1][1]
        f = p + 1
        while shots[f].body in jump:
            f += 1
        seq = []
        while True:
            seq.append(((), shots[f].body, ()))
            if shots[f].body == hold:
                break
            f += 1
            if len(seq) > 60:
                raise SystemExit('the air shot at %d never falls' % p)
        anims[('AIRSHOT', fac)] = (seq, 0, 0xFF)
    # The air shot's effects, from the left-facing one only, mirrored for
    # the right: the right-facing bolt flies into the level and bursts,
    # and the burst alone is a hundred and sixty tiles of VRAM. The game
    # draws its effects either way round by flipping them, so the mirror
    # is what it would have drawn.
    fx = effects(shots, 3504 + SHOT_ONSET, 48)
    anims[('AIRFX', LEFT)] = (fx, 0, 0xFF)
    anims[('AIRFX', RIGHT)] = ([(mirror(t[0], gfx), (), mirror(t[2], gfx)) for t in fx],
                               0, 0xFF)

    # Where the standing shot's own bolt was, against the running one's:
    # the payload flies SHOTFX's bolt from there.
    bolt_at = {}
    for fac in (LEFT, RIGHT):
        fx = anims[('SHOTFX', fac)][0]
        i = next(i for i, t in enumerate(fx) if any(s[0] in gfx.bolt for s in t[2]))
        b = next(s for s in fx[i][2] if s[0] in gfx.bolt)
        si, sx, sy = stand_bolt[fac]
        bolt_at[fac] = (sx - b[3], sy - b[4], si - i)
        # only the bolt from SHOTFX is borrowed by the standing shot
    # The glow: firing lights him up, for 23 frames, and he fades back
    # over four. Entries 1 to 8 of his palette, frame by frame from the
    # first frame of the standing shot until he is back to normal.
    f0 = first_off_loop(caps['movesR'], 2750, loops[RIGHT])
    normal = None
    allf = [fr for c in caps.values() for fr in c.values()]
    body_pal = [max(set(fr.c0[n] for fr in allf), key=[fr.c0[n] for fr in allf].count)
                for n in range(16)]
    normal = tuple(body_pal[1:9])
    glow, f, lit = [], f0, False
    while True:
        c = caps['movesR'][f].c0[1:9]
        if c != normal:
            lit = True
        elif lit:
            break
        glow.append(c)
        f += 1
        if len(glow) > 64:
            raise SystemExit('the glow does not fade')

    # ---- the palettes ------------------------------------------------------
    # Line 0 is his, and it is not constant: firing lights him up (above),
    # and two entries cycle -- 9, the glow in his joints, over 64 frames,
    # and 10 over four. What goes in the file is each entry's usual colour,
    # and 64 frames of 9 and 10.
    fxp = list(allf[0].c3)
    if any(fr.c3 != tuple(fxp) for fr in allf):
        raise SystemExit('palette line 3 changes')
    idle = [caps['idleR'][k] for k in sorted(caps['idleR'])]
    cyc64 = [(fr.c0[9], fr.c0[10]) for fr in idle[:64]]
    if any((fr.c0[9], fr.c0[10]) != cyc64[i % 64] for i, fr in enumerate(idle)):
        raise SystemExit('entries 9 and 10 do not cycle every 64 frames')

    # ---- pictures, VRAM, and the file ----------------------------------------
    used = sorted({s[0] for (fr, e, x) in anims.values() for t in fr for part in t for s in part})
    alias = canonical(gfx, used)
    kept = sorted({c for c, flip in alias.values()})
    if len(kept) > 255:
        raise SystemExit('%d pictures: one byte names 255' % len(kept))
    where, upload = place(gfx, kept)
    index = {g: i for i, g in enumerate(kept)}
    gid = {g: (index[c], flip) for g, (c, flip) in alias.items()}
    used = kept

    foot, fx0, fx1 = lowest([t[1] for t in anims[('LOOP', RIGHT)][0]], gfx)
    names = [(n, fac) for n in ANIMS for fac in (LEFT, RIGHT)]
    # Walking off an edge, or dropping through one, starts partway down a
    # jump: at the first frame past the top where his feet are back level
    # with where he stands. The top itself would do for the speed, but
    # his legs are tucked twenty pixels up there, and he would hop.
    jr = anims[('JUMP', RIGHT)][0]
    feet = [lowest([t[1]], gfx)[0] for t in jr]
    top = feet.index(min(feet))
    jump_fall = next((i for i in range(top, len(jr)) if feet[i] >= foot - 1), len(jr) - 1)

    HDR = 384
    groups = b''.join(struct.pack('>HBB', where[g] | (gfx.list[g][0] << 13),
                                  ((gfx.list[g][1] - 1) << 2) | (gfx.list[g][2] - 1),
                                  gfx.list[g][4]) for g in used)
    off_runs = HDR
    off_groups = off_runs + 8 * len(upload)
    off_anims = off_groups + len(groups)
    off_data = off_anims + 12 * len(names)
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
        data, nrec, nfr = encode(seq, gid)
        want = [tuple((s[3], s[4], gid[s[0]][0], (s[1] ^ gid[s[0]][1]) | (s[2] << 1))
                      for s in t[0] + t[1] + t[2]) for t in seq]
        if decode(data, 0, nrec) != want:
            raise SystemExit('%s %s: does not decode to what was encoded' % key)
        animtab += struct.pack('>IHHBBH', off_recs + len(recs), nrec, nfr, ent, ext, 0)
        recs += data
        if len(recs) & 1:
            recs += b'\0'
        base = key[0].rstrip('0123456789') if key[0].startswith(('WALK', 'AIM')) else key[0]
        sizes[base] = sizes.get(base, 0) + len(data)
    # The glow, then the sounds: a table of four -- u32 offset, u16 count,
    # u16 kind -- for the shot, the boost, the landing and the voices.
    # Kind 0 is an FM effect, `count` frames each a u8 count of writes and
    # three bytes a write (port, register, value); kind 1 a sample, `count`
    # bytes for the DAC; kind 2 writes to make once, `count` of them.
    off_glow = off_recs + len(recs)
    glowtab = b''.join(struct.pack('>8H', *c) for c in glow)
    off_sfx = off_glow + len(glowtab)
    sfxdata = bytearray()
    sfxtab = bytearray()
    base_sfx = off_sfx + 32
    for kind, body in ((0, sfx_shot), (0, sfx_boost), (1, sfx_land), (2, sfx_setup)):
        at = base_sfx + len(sfxdata)
        if kind == 0:
            for fr in body:
                sfxdata += bytes([len(fr)]) + b''.join(bytes(w) for w in fr)
            n = len(body)
        elif kind == 2:
            sfxdata += b''.join(bytes(w) for w in body)
            n = len(body)
        else:
            sfxdata += body
            n = len(body)
        if len(sfxdata) & 1:
            sfxdata += b'\0'
        sfxtab += struct.pack('>IHH', at, n, kind)
    total = base_sfx + len(sfxdata)

    hdr = bytearray(HDR)
    struct.pack_into('>4sHHIHH', hdr, 0, b'VMDD', 2, HDR, total, len(upload), len(names))
    struct.pack_into('>III', hdr, 16, off_runs, off_groups, off_anims)
    struct.pack_into('>HhbbH', hdr, 28, len(used), foot + 1, fx0, fx1, jump_fall)
    struct.pack_into('>16H', hdr, 36, *body_pal)
    struct.pack_into('>16H', hdr, 68, *fxp)
    for i, (c9, c10) in enumerate(cyc64):
        struct.pack_into('>HH', hdr, 100 + 4 * i, c9, c10)
    struct.pack_into('>HHI', hdr, 356, aim_frames, len(glow), off_glow)
    struct.pack_into('>bbbbbb', hdr, 364, *bolt_at[LEFT], *bolt_at[RIGHT])
    struct.pack_into('>I', hdr, 372, off_sfx)
    blob = bytes(hdr) + bytes(runtab) + groups + bytes(animtab) + bytes(tiles) + \
        bytes(recs) + glowtab + bytes(sfxtab) + bytes(sfxdata)
    assert len(blob) == total
    with open(a.out, 'wb') as f:
        f.write(blob)

    print('%s: %d bytes -- %d pictures in %d tiles of VRAM, %d bytes of poses'
          % (a.out, total, len(used), len(tiles) // 32, len(recs)))
    for n in sorted(sizes, key=lambda n: ANIMS.index(n if n in ANIMS else n + '0')):
        fl = sum(len(anims[k][0]) for k in names
                 if k[0] == n or (k[0].rstrip('0123456789') == n and n in ('WALK', 'AIM')))
        print('  %-9s %4d frames %6d bytes' % (n, fl, sizes[n]))
    print('  aiming lasts %d frames; the glow %d; the sounds: shot %d frames, '
          'boost %d, landing %d samples' % (aim_frames, len(glow), len(sfx_shot),
                                             len(sfx_boost), len(sfx_land)))
    print('  feet: bottom row %+d, x %+d..%+d; falls from jump frame %d'
          % (foot, fx0, fx1, jump_fall))


if __name__ == '__main__':
    main()
