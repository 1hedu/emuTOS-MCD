# Vectorman on the desktop

**Desk → Vectorman.** He drops in from above the middle of the screen
onto whatever is under it, and the desktop is his level: every
horizontal edge on it — a window's frame, the top of an icon, the menu
bar — is a girder he can land on, and he jumps up through them from
underneath the way he does in the game.

| | |
|---|---|
| left, right | walk; a tap the other way turns him round on the spot |
| C | jump; C again in the air fires his boots |
| A or B | shoot — standing, on the run or in the air |
| down | look down |
| down + C | drop through what he is standing on |
| Start | put the desktop back |

He is the cartridge's own drawing, moving by the cartridge's own
numbers and making the cartridge's own noises, and he is not on any
released disc: all three are Sega's. What the
repository carries is the pipeline that records them off your own
dump, and the engine that plays them back on the Mega CD.

## Building it

Your own **Vectorman (USA, Europe)** dump at
`assets/vectorman/vectorman.md`, plain binary. `assets/` is git-ignored.
The extractor checks the dump's SHA-1 and refuses any other revision:
it cuts the recording at frame numbers measured on that one.

    tools/build-vectorman.sh

builds the emulator it records with (into `.emu/`, once for each
version of the patches), records, and leaves two files in `datadisc/`:

    VECTOR.MDP    7.8 KB   the engine, payload/vectorman/vm.c
    VECTOR.MDD   69.8 KB   his poses, tiles, palette and sounds

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
carries Sega's graphics and sounds. `dist/` is tracked by git for that
reason.

## How he was recorded

Vectorman is not stored as pictures, and not as sprite mappings either.
He is a skeleton: the game keeps an angle for every joint and works out,
each frame, where his twenty-one sprites go — sixteen balls and five
limb pieces — and draws the limb pieces freshly rotated into VRAM. There
is no table in the ROM that says what frame twelve of the walk looks
like; it only exists once the game has computed it.

So `tools/vectorman-tools/extract.py` runs the game rather than reading
it. `tools/gpgx-vdpdump.patch` makes Genesis Plus GX write VRAM, CRAM,
the VDP registers and work RAM out after every frame, and log every
write to the sound chips; the harness plays
scripted pad input into the first level; and the extractor reduces each
frame to his sprites, their tiles, and the game's own state beside them:

| work RAM | |
|---|---|
| `$FFE364`, `$FFE368` | his x and y, a pixel and a fraction |
| `$FFE36C`, `$FFE36E` | his speed, in 1/256 px a frame |
| `$FFE348` | facing: 8 right, 0 left |
| `$FFE39A` | which eighth of the walk cycle he is in |
| `$FFAF82`, `$FFAF8A` | the camera |

Eight captures: standing facing each way for half a minute (his bored
animations), every move facing each way, two sets of back-and-forth
runs, a stop, and a minute and a half of shooting — on the run at every
point of the stride, from the top of a jump at four heights, and turns
on the spot — plus three short runs for the sounds. Each animation is
cut where the game's state says it starts and ends, and checked rather
than trusted. The extractor stops rather than write a file that is
wrong.

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
  * **Shooting on the run** the game draws him out of two skeletons at
    once: the legs go on walking and the arms aim. So the aiming walk is
    a second cycle of 64 poses — the walk with the gun out — recorded
    from sixty-four shots spread over the stride. A shot jumps the
    stride four poses on, holds the aim for fifteen frames and eases
    back into the walk over eight. Nineteen of the 128 aiming poses,
    both facings, never came up; each is the nearest one that did, a
    pixel or two off. Facing right, the game draws the arms already
    mirrored, so the aiming poses were matched to the walk by the feet
    alone.
  * **Shooting in the air** is the jump's own pose giving way to an aim
    and then falling back into it: one recording each way.
  * **Turning on the spot** is ten frames, each way.
  * **The effects** — the bolt, the muzzle flash, the casing, the boost
    flames and their sparks — are recorded as part of the frames they
    are in, and the shots' are kept as animations of their own, so that
    a bolt flies on from where it was fired whatever he does next. The
    air shot's are the left-facing one's, mirrored: facing right its
    bolt hit the level and burst, and the burst alone is 160 tiles.
    Whatever of the level was already on the screen before a shot is
    left out of it.
  * **His sounds**: the game's driver plays them on the Z80, so they
    were recorded as what it wrote to the YM2612. The shot and the boost
    are FM — a voice loaded into a channel, then a frequency a frame —
    and the landing is a sample of 2604 bytes through the DAC. Each was
    recorded in a run of its own, against the same run without the
    press, because the level's music uses every channel and the effect
    starts where the two differ.
  * **His palette** is not constant in the level: lightning lights him
    for a frame at a time, and two entries cycle — the glow in his
    joints over 64 frames and another over four. The file has each
    colour's usual value and the 64 frames of the cycle.

## VECTOR.MDD

The extractor's comments are the byte-level description. In outline: a
384-byte header (counts, offsets, his foot line and how far his feet
reach, both palettes and the colour cycle, how long an aim lasts, where
the standing shot's bolt starts); the tiles, in up to five runs that go
straight into VRAM; 100 pictures, each a VRAM tile, a sprite size, a
palette line and which part of him it is; 62 animations, each facing in
turn; the 29 frames of the glow a shot lights him with; and the sounds.

A frame record carries his own sprites either whole or as changes from
the record before — a bit per sprite for whether it moved, and a
nibble each for how far — then the effects in front of him and behind
him, written out whole. The effects are kept apart because they blink:
the casing and the sparks are drawn every other frame, and with them in
the same list every record changed length and had to be written whole.
That split and the bits took the file from 76 KB to 57.

The sounds are register writes, a frame at a time, and the sample. A
voice is thirty-odd writes, and the YM2612 wants a pause after each, so
the extractor takes the voices out: each is given a channel of its own
and written once when he arrives, and the effects keep the frequency
and key writes — one to five a frame.

His graphics take 499 tiles of VRAM. A payload may use what the servant
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
every speed exactly as they do in the game. A tap the other way, let go
of within eight frames, is the turn on the spot rather than a walk.

**Blending.** The game moves from one thing to the next by easing the
skeleton: from standing into the first stride over seven frames, from
aiming back into the walk, into a jump. There is no skeleton here, so
each of his sprites travels instead from where it was last drawn to the
nearest sprite of the same part in the new pose — a hand to a hand, a
ball to a ball — and halfway takes the new picture. The file says which
part every picture is.

**Shots.** Standing, running or in the air, a shot is three things: his
pose, the flash and bolt flying as an animation of their own, and his
colours lit for 29 frames, entries 1 to 8 of his palette frame by frame
as the game does it. Up to four shots can be in the air. A shot shows
two frames after the button, standing, running or in the air, and its
sound starts one frame after — the game's own timing, measured the same
way off both. Those two frames are not a wind-up: they are how long any
press takes to reach the screen, in the game and here alike.

**Sound.** The 68000 plays the FM effects, a frame's writes in each
vertical blank. The landing is 8860 bytes a second, too fast for that,
so the Z80 plays it: a player of 67 bytes, this project's own, loaded
into Z80 RAM with the sample, which waits on a mailbox and writes the
DAC every 400 cycles. The 68000 takes the Z80's bus to reach the
YM2612, which can stop the Z80 between its two writes, so it always
leaves the DAC's register selected when it lets go.

**Drawing.** Each frame he is decoded from the record into a sprite
list — effects in front, his twenty-one, effects behind — and written
to the servant's sprite table in the vertical blank, with the two
cycling colours. Sprites off the screen are dropped; one at x = 0 would
mask everything under it.

**The frame.** A 68000 has 262 lines' worth of time a frame, and the
first version of this did not fit: a running shot took 251, and a
blend pushed every frame it was in past the end, so that while he
blended everything, the bolt in the air included, ran at half speed.
Measured with the VDP's line counter, a frame now takes about 60 lines,
and the worst in a test built to be hard on it — rapid fire, turning
while aiming, shots and boosts in the air — took 188.
What it took: the blend looks only among sprites of the same part (71
comparisons, not 441); the sprite table is built without a call per
sprite; the decoder walks its bit mask a byte at a time; the voices are
loaded once; and going over between the walk and the aiming walk
decodes at most three records a frame, catching up on the next.

## What is not here

  * **The game's skeleton.** Where the game eases one movement into
    another, the in-between frames here are the blend above: the same
    pictures getting to the same place, but not the poses the skeleton
    would have passed through.
  * **The music.** Only his own sounds.
  * **Enemies, scenery, anything to shoot.** The desktop is the level.

## Testing without a hand on the pad

`VECAUTO=1` builds `VECTAUT.ACC` instead: it opens itself eight seconds
after the desktop is up, and again eight seconds after he is sent home,
because a second run is the one a payload is most likely to get wrong
(Sonic's was: see the note on `sn_sound_close` in payload/sonic/main.S).
It is emulator-only, by name, in `tools/build-rom.sh`'s guard.
