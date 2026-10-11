# Vectorman on the desktop

**Desk → Vectorman.** He drops in from above the middle of the screen
onto whatever is under it, and the desktop is his level: every
horizontal edge on it — a window's frame, the top of an icon, the menu
bar — is a girder he can land on, and he jumps up through them from
underneath the way he does in the game.

| | |
|---|---|
| left, right | walk |
| C | jump; C again in the air fires his boots |
| A or B | shoot (standing) |
| down | look down |
| down + C | drop through what he is standing on |
| Start | put the desktop back |

He is the cartridge's own drawing, moving by the cartridge's own
numbers, and he is not on any released disc: both are Sega's. What the
repository carries is the pipeline that records them off your own
dump, and the engine that plays them back on the Mega CD.

## Building it

Your own **Vectorman (USA, Europe)** dump at
`assets/vectorman/vectorman.md`, plain binary. `assets/` is git-ignored.
The extractor checks the dump's SHA-1 and refuses any other revision:
it cuts the recording at frame numbers measured on that one.

    tools/build-vectorman.sh

builds the emulator it records with (once, into `.emu/`), records, and
leaves two files in `datadisc/`:

    VECTOR.MDP    4.7 KB   the engine, payload/vectorman/vm.c
    VECTOR.MDD   56.7 KB   his poses, tiles and palette

The accessory, `VECTOR.ACC`, is part of the system build. It looks for
the other two on C:, then R:, then D:.

**A disc:**

    VECTORACC=1 tools/build-datadisc.sh U

Both files on D:, the accessory on C:. The data disc keeps C: at 32 KB,
which leaves room above it in PRG RAM for VECTOR.MDD to be loaded into.

**A cartridge:**

    VECTORACC=1 tools/build-iso.sh U
    DDISK_DIR=$PWD/datadisc tools/build-rom.sh boot/m1emu.S

Both files on R:, the romdisk in the cartridge ROM; the accessory on C:.
On R: VECTOR.MDD is never loaded at all. The cartridge ROM is the main
CPU's own memory, so the payload reads the file where it lies — it finds
it by its directory entry, since `tools/mkfat.py` writes every file in
one run of clusters — and it costs no RAM. That matters because C: is
already the whole of PRG RAM on a cartridge, and Sonic's art wants the
room there.

Don't put a cartridge built with VECTOR.MDD on R: anywhere public — it
carries Sega's graphics. `dist/` is tracked by git for that reason.

## How he was recorded

Vectorman is not stored as pictures, and not as sprite mappings either.
He is a skeleton: the game keeps an angle for every joint and works out,
each frame, where his twenty-one sprites go — sixteen balls and five
limb pieces — and draws the limb pieces freshly rotated into VRAM. There
is no table in the ROM that says what frame twelve of the walk looks
like; it only exists once the game has computed it.

So `tools/vectorman-tools/extract.py` runs the game rather than reading
it. `tools/gpgx-vdpdump.patch` makes Genesis Plus GX write VRAM, CRAM,
the VDP registers and work RAM out after every frame; the harness plays
scripted pad input into the first level; and the extractor reduces each
frame to his sprites, their tiles, and the game's own state beside them:

| work RAM | |
|---|---|
| `$FFE364`, `$FFE368` | his x and y, a pixel and a fraction |
| `$FFE36C`, `$FFE36E` | his speed, in 1/256 px a frame |
| `$FFE348` | facing: 8 right, 0 left |
| `$FFE39A` | which eighth of the walk cycle he is in |
| `$FFAF82`, `$FFAF8A` | the camera |

Seven captures: standing facing each way for half a minute (his bored
animations), every move facing each way, two sets of back-and-forth
runs, and a stop. Each animation is cut where the game's state says it
starts and ends, and checked rather than trusted. The extractor stops
rather than write a file that is wrong.

  * **The walk** is 64 poses in eight segments of eight. A segment runs
    over eight frames, or over four — two poses a frame — if he is near
    top speed when it begins. So at full speed only the even poses are
    ever drawn, and the odd ones only appear while he is speeding up.
    The last four segments he only ever crosses slowly coming out of a
    turnaround, which is what the back-and-forth runs are for. Every
    pose was seen at least once, and every time a pose was seen at the
    same point of the cycle it was the same pose.
  * **Idling** is a 32-frame breathing loop and three longer things he
    does when nobody is playing — 77, 135 and 311 frames. Each leaves
    the loop at one frame of it and returns at another; so do stopping,
    landing and shooting.
  * **Facing left is drawn, not mirrored.** The game lights him from the
    upper left either way round, so his left-facing limbs are separate
    drawings and his near arm is the bright one. Both facings are
    recorded.
  * **The effects** — the bolt, the muzzle flash, the casing, the boost
    flames and their sparks — are recorded as part of the frames they
    are in. The left-facing shot uses the right-facing shot's effects,
    mirrored, because in the capture its bolt hit the level.
  * **His palette** is not constant in the level: lightning lights him
    for a frame at a time, and two entries cycle — the glow in his
    joints over 64 frames and another over four. The file has each
    colour's usual value and the 64 frames of the cycle.

## VECTOR.MDD

The extractor's comments are the byte-level description. In outline: a
360-byte header (counts, offsets, his foot line and how far his feet
reach, both palettes and the colour cycle); the tiles, in up to five
runs that go straight into VRAM; 99 pictures, each a VRAM tile, a sprite
size and a palette line; and 38 animations, each facing in turn.

A frame record carries his own sprites either whole or as changes from
the record before — a bit per sprite for whether it moved, and a
nibble each for how far — then the effects in front of him and behind
him, written out whole. The effects are kept apart because they blink:
the casing and the sparks are drawn every other frame, and with them in
the same list every record changed length and had to be written whole.
That split and the bits took the file from 76 KB to 57.

His graphics take 495 tiles of VRAM. A payload may use what the servant
does not: 1024..1079 (the on-screen keyboard's, which the servant
uploads again afterwards), 1084..1215, 1216..1407, 1664..1791 and
1920..1983. Tiles 1080..1083 are the pointer's, which nothing rewrites,
so they are stepped round.

## The engine

`payload/vectorman/vm.c`, in C — the first payload that is not a port
of somebody's assembly, so `tools/build-payload.sh` compiles `.c` now.

**The ground.** The desktop is read once, at the start, into a bitmap
of which pixels are not colour 0. Nothing can draw on it while he runs,
so it is not read again. The rules are Sonic's: three drawn pixels in a
row are a surface, so the desktop's checkerboard paper never is; and he
only comes *down* on an edge at least sixteen pixels wide, so that a
label or the pointer's arrow does not hold him up in the air. Three
sensors, under each foot and between them. The screen's bottom is the
floor and its sides are walls.

**Edges are one way.** He stands on top of them and nothing else about
them is solid — up through them on a jump, along through them on a
walk. Walking, he follows a step up of two pixels or down of four. Down
with C drops him 24 rows: through the line he is on and through the rest
of an icon or a window's title bar, rather than onto the next line of
it. On the way in he falls past the menu bar, whose top edge is the
first thing under him; once he is in, anywhere is fair.

**The numbers** are the game's, measured frame by frame. On the ground
he gains 32/256 of a pixel a frame up to four pixels, and loses 128 a
frame on letting go. Pressing against his facing or his direction —
turning from a standstill, reversing at a run — changes his speed by 128
a frame and turns him at once, and the walk holds its pose for four
frames while he skids. A jump starts at −2080 and gravity takes 96 a
frame back; a second C sets −2048 once. In the air he steers at 8.

**The walk** runs on the game's own segment rule, so the 64 poses serve
every speed exactly as they do in the game.

**Drawing.** Each frame he is decoded from the record into a sprite
list — effects in front, his twenty-one, effects behind — and written
to the servant's sprite table in the vertical blank, with the two
cycling colours. Sprites off the screen are dropped; one at x = 0 would
mask everything under it.

## What is not here

  * **Sound.** The game's driver runs on the Z80 out of the cartridge;
    nothing of it is recorded.
  * **Shooting while walking or in the air.** The game composes that on
    the fly from his skeleton, and only the standing shot was recorded.
    A or B does nothing while he moves.
  * **The blend into a walk.** The game eases from wherever his idle
    had got to into the first stride over seven frames. Here he goes
    straight into the cycle's first pose: one quick step.
  * **Turning on the spot.** A tap of the other direction turns him; the
    game has a ten-frame turn for that, not recorded.
  * **Enemies, scenery, anything to shoot.** The desktop is the level.

## Testing without a hand on the pad

`VECAUTO=1` builds `VECTAUT.ACC` instead: it opens itself eight seconds
after the desktop is up, and again eight seconds after he is sent home,
because a second run is the one a payload is most likely to get wrong
(Sonic's was: see the note on `sn_sound_close` in payload/sonic/main.S).
It is emulator-only, by name, in `tools/build-rom.sh`'s guard.
