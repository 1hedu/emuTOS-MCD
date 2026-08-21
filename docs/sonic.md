# Sonic on the servant

Sonic runs on the desktop that is already on the screen. He is not drawn
into the framebuffer — he is Mega Drive sprites over it, and the desktop
underneath is never touched, so nothing has to be repaired when he
leaves. The furniture is the terrain: a window frame, a menu bar's
underside, an icon's top edge and a divider are all runs of set pixels,
and all of them hold him up.

The engine is GEOS-Genesis's, and its movement is Sonic 1's constant for
constant. This is the port of it onto a machine it was not written for.

**Desk → Sonic.** That is the whole of the user interface: no program to
launch, no file to pick, no key to press. He falls onto the desktop that
is on the screen at that moment, and the icons and the menu bar and any
open window are what he lands on.

    tools/build-sonic.sh
    SLIMC=1 SONICACC=1 ADISK_DIR=$PWD/datadisc ADISK_SIZE=0x1C000 \
        tools/build-iso.sh U
    tools/build-rom.sh boot/m1emu.S          # or keep the ISO

`ADISK_DIR` puts files on C: the way `DDISK_DIR` puts them on D:,
`SONICACC` adds the accessory, and `SLIMC` drops SHOW, EDIT and DEMO.PI1
to make room. The ramdisk is the whole region then — 114688 bytes,
nothing left over — which is fine, because a file already on C: is not
copied anywhere. See "The art is where it lies" below.

The disc build works the same way with the files on D: and
`tools/build-datadisc.sh U`, and `NATIVE.PRG` will still run him from a
menu. That is the general tool and it is the wrong way to see him: read
"Why an accessory" below.

Start ends it at any time; walking into the sign post at the end of the
fourth screen ends it the other way.

## What it needs from you

Your own Sonic 1 and Sonic 2 dumps in `assets/sonic/`. Nothing extracted
from them is committed and nothing is distributed — `assets/` is
git-ignored on the same rule `vendor/` follows. `tools/build-sonic-art.sh`
unpacks the art out of the ROMs and reads six files from the Sonic Retro
disassemblies for the mappings; what comes out is
`build/sonic/sonic_data.bin`, and that becomes `SONIC.MDD`.

## The joins

The engine is unmodified except where a comment says PORTED. There are
six of those, and `payload/sonic/port.inc` is the rest of the
difference — constants re-based onto the servant's VDP layout.

**The anchor.** GEOS keeps a 64KB memory image at `$FF0000` and points
A6 at logical `$8000`, so every variable is a displacement off A6. All
of that address space is the servant's here. But the engine only ever
names 68 symbols and they span `$850A`..`$9F5A` — eight kilobytes. So A6
points at 8KB of the payload's own workspace and every one of those
displacements works untouched.

**The ground.** GEOS's bitmap is one bit per pixel in 8-byte cards; the
ST screen is 320x200 in four bitplanes interleaved a word at a time.
`sonic_pixel` is rewritten for that. The rule does not change: GEOS asks
"is this bit set", here it is "is this pixel not the background colour",
which is the OR of the four planes, and three in a row still make a
surface. Every caller is untouched.

**The art.** 49568 bytes of tiles, and a payload has 32000 for
everything. It goes in PRG RAM above the C: ramdisk instead and is read
through the Mega CD's 128KB window — see docs/payload.md, "Bulk data".
The engine's nine `lea sonic_blob,%aN` become `movea.l sn_blob,%aN`, one
word longer each, and that is the whole change. The art's bank stays
selected for the run; `sonic_pixel` borrows the screen's for four words,
because it is the only thing in the engine that reads the screen.

## The art is where it lies

There is 112KB of PRG RAM between EmuTOS's `phystop` and the timeshare's
scratch, and the C: ramdisk is at the bottom of it. A disc can keep C:
small and load `SONIC.MDD` into what is left. A **cartridge** cannot: C:
is the only drive it has, so the file must be on C: — and 52KB of file
plus 52KB of copy does not fit in 112KB with anything else.

So it is not copied. A file on C: is a run of sectors in an image that
is itself PRG RAM, in the same window bank, at an address the servant
can hand straight to the payload. `NATIVE.PRG` looks for the file's
first block in the ramdisk image and then reads the whole file through,
comparing — so what it hands over is either an address proved correct
byte for byte, or nothing, and it falls back to copying. It never walks
the FAT, so it never has to be right about the filesystem.

That is why the disc build and the cartridge build differ only in where
the two files sit.

**The frame.** GEOS ran the tile uploads out of a VBlank ISR and the
engine's loop said `stop #$2000`. On a Mega CD the vector table is in
the boot ROM — or in the cartridge, on a Mode 1 boot — and neither is
ours to write; the servant does not run an interrupt handler either, it
polls. So `sn_frame` polls the VDP's own VBlank flag and calls
`geos_sonic_vblank` itself, which puts the tile uploads and the SAT and
the scroll register inside the blanking period exactly as the interrupt
did.

**The sprite table.** On GEOS entry 0 was the mouse pointer and 1-7 were
the application's, so Sonic's chain started at 8. The servant draws the
pointer into the ST screen like everything else and parks the table as a
single dummy entry whose link is zero — so a chain starting at 8 was
never walked and nothing appeared at all. `SONIC_SAT_FIRST` is 0 here.

## Why an accessory

A `.PRG` launched from the desktop is a program, and GEM hands a program
the screen. So the first thing `NATIVE.PRG` does is say what it is for --
a list of files, a letter to press -- and by the time Sonic starts, the
desktop he was supposed to be running on has been replaced by a console.
He was walking on a blank white page. Every horizontal surface in the
world was the bottom of the screen.

`SONIC.ACC` is a desk accessory, which is what he is on GEOS too. It
loads at boot, adds one line to the Desk menu, and when that line is
picked the AES sends it a message and *does not touch the picture*. The
desktop is still drawn; the accessory hands the payload over; he falls
into what was already there.

It prints nothing, asks nothing and offers no choice. The one thing it
will interrupt the desktop for is a GEM alert when its files are missing.

Two things about accessories cost an afternoon each and are worth
writing down, because neither is a bug in anything:

  * **A static initialiser holding another static's address does not
    work.** These programs are built `-mpcrel` and carry no relocation
    table -- that is what lets them load anywhere -- so an AES parameter
    block written out as `{ contrl, global, ... }` is six link-time
    addresses that nothing fixes up. EmuTOS answered with "Unsupported
    AES function #4121", which is what it reads when the opcode it
    fetched was never an opcode. The block is filled in at run time.

  * **An accessory is entered with no stack.** `gotopgm()` switches to
    user mode and jumps to the start of the TEXT segment with A0 = the
    basepage and whatever the USP happened to hold, which at AES-init
    time is nothing; a `.PRG` is handed a stack by TOS and never has to
    think about it. Nor can the stack go at the top of the accessory's
    memory: `load_one_acc()` has already `Mshrink`'d the block to exactly
    basepage + text + data + bss, so a stack there grows straight down
    through every variable the program has. It gets four kilobytes of its
    own BSS instead. Both failures printed the answer -- `USP: FFFFFF5C`,
    then a program counter in the vector table.

## The level: the desktop, repeated

The act is four desktops laid end to end, and every one of them is the
desktop: level column L shows desktop column L mod 40. Nothing is
painted and nothing is uploaded -- a repeated column names a card that
is already in VRAM -- and the first screen is left alone because he has
to land on what is under him.

It is a byte table, one entry a column, built once at the start:
`sonic_pixel` asks which desktop column a level column shows a couple of
hundred times a frame, and the answer used to be a `DIVU`.

### Two things that were tried here

The cartridge turned each repeat after the first by a random number of
cards. Then a version of this made runs of *stalls* -- one column held
for three to ten cards, so the printer's icon stretched into a bar you
could stand on, with the floppy's bar above it because they share a
column -- the idea being to draw terrain out of the desktop's own
furniture.

Both are gone. The stalls were built and shipped and the verdict on
seeing them was that a clean repeat reads better, so a clean repeat is
what it is.

Worth recording from the attempt: the seed. `sn_seed` lived in the
payload's `.data`, and a payload's `.data` is bytes in a file the
servant stages fresh on every run, so the generator drew the *same*
"random" level every time. There is no clock on the Genesis side to ask
instead; the servant's main-loop pass counter is what there is, and it
now reaches a payload in the top half of `d0` (docs/payload.md). That
handover stayed, because it is a real hole in the payload contract and
the next payload that wants a random number will have the same problem.
This one no longer does.

## The skid: four frames of screech, a whole brake of pose

The `$400` test gates where `anim` is **written**. It does not gate how
long the pose lasts, and reading it as though it did cost this port two
wrong answers in a row.

Here is the branch it guards, in `Sonic_MoveLeft`:

    cmpi.w  #$400,d0                ; changed direction while really fast?
    blt.s   .nostopping             ; if not, no skid animation or sound
    move.b  #id_Stop,obAnim(a0)
    bclr    #0,obStatus(a0)
    move.w  #sfx_Skid,d0
    jsr     (QueueSound2).l
  .nostopping:
    rts                             ; and obAnim is left exactly as it was

Compare the other exit from the same routine — the one taken when he is
accelerating the way he already faces — which ends
`move.b #id_Walk,obAnim(a0)`. So `anim` holds `id_Stop` from the frame
it is set until he is actually moving the new way. Every remaining frame
of the brake leaves via `.nostopping` and does not touch it, the
sign-change frame included: that one reaches the same test with `d0` =
`-$80` and falls through it too.

`SonAni_Stop` is `dc.b 7, fr_Stop1, fr_Stop2, afEnd` — two cels at eight
frames each. It needs those frames to be a skid rather than a flicker.

This port cannot inherit that by doing nothing, because it clears `anim`
to Walk at the top of every frame. That was written down as coming "to
the same thing", and for the duck and the charge it does — they assert
their animation every frame they own him. For the skid it does not, and
what it cost was four frames of cel one, never reaching cel two: on
hardware, a screech over a Sonic who barely skids at all.

So the latch is written out. `sn_skid_on` is set on the frames the
cartridge writes `id_Stop`; `sn_skid_was` carries it into the next
frame; a braking frame with nothing to write asserts the pose anyway if
the brake had already earned it. Any frame that is not a braking frame
clears it, which is the `move.b #id_Walk` on the other exit. The screech
does not move — it stays on the strict `$400` test, four retriggers from
top speed and not a drone, because there that really is the same test
and the sound is meant to stop before he does.

Measured on a turn, tracing `anim`, the cel and the inertia into work
RAM a frame at a time:

    inertia $565      walking
    $4E5  $465        pose + screech      (>= $400)
    $3E5 .. $65       pose, no screech    (the rest of the brake)
    -$80              pose                (the sign-change frame)
    -$8C              walking again

Eleven frames of pose, and the cel changes at frame eight — the two-cel
script running as written, which is the whole point of it.

An earlier attempt held the pose for the whole brake by asserting it
unconditionally on every braking frame. That produced very nearly this
picture and was reverted as a divergence, because the reasoning behind
it was "four frames is too short to see" rather than anything read out
of the cartridge. The behaviour was close to right and the justification
was wrong, so it did not survive contact — which is the correct outcome
for a guess, even a lucky one. What is here now is the disassembly.

## Twelve lines above everything

He floated. Dead centre on a program icon, in the middle of the hollow,
with clear white under his shoes; standing on the trash can's lid,
hovering over it; the same distance every time, in every pose, on every
surface.

`iofw` centres the ST screen: the display is 224 lines, the picture is
200, and `SCREEN_YOFF` is 12, applied as a vertical scroll of plane A so
the desktop sits in the middle with a border above and below — the way a
television shows an ST. **Sprites are not scrolled with the plane.** So a
sprite placed at framebuffer row Y appears twelve lines above framebuffer
row Y of the desktop.

GEOS's own `GEOS_DISPLAY_Y` is 12, for exactly this reason on exactly this
kind of screen. `port.inc` overrode it to zero, with a comment asserting
that "the servant puts the 200 lines at the top of the 224, not centred".
That assertion was simply wrong, and it was written down as fact, which
is why it went unexamined through four other explanations.

The measurement that ended it, and the one that should have been made
first: correlate the set-pixel count of every framebuffer row against
every row of the rendered frame.

    best row offset:  display = framebuffer + 12   (mean error 0.0 px/row)

What made it survive so long is worth recording. Every check of "are his
feet on the surface" compared the engine's feet row — a framebuffer
number — against the sprite's lowest inked row measured off the rendered
frame — a display number. The two differ by exactly the error being
looked for, so the error cancelled and every measurement said zero. On
the floor it cancelled twice over, because `.st_floor` clamps him to
`199 - SN_HEIGHT` whatever the sensor says.

Verified after: standing, feet at framebuffer row 200, which is display
row 212; his shoes' last inked row is 211, and the screen's bottom border
begins at 212.

## Two things found on the way, which stay

Neither was the fault above, and both were measured on the framebuffer
alone, so neither is contaminated by it.

**Hairlines.** `sonic_solid` is three adjacent set pixels, which is GEOS's
rule and fine on a level drawn as terrain. The desktop is line art. The
printer's paper tray is a ten-pixel run with seven rows of white under it
and the printer's real top edge eight pixels lower; the trash can's lid
handle is five pixels; icon labels are four. A run he may **land** on now
has to be `SN_SURF_MIN` = 12 pixels — the length of the run his foot is
touching, from wherever on it the foot is, because the trash lid is 21
pixels and he has to be able to come down on either end of it. Landing
only: once standing, three pixels under any sensor still keep him up, so
ledge edges are unchanged.

**A third ground sensor.** His shoulders are 18 pixels apart and a program
icon's top edge is 15, so standing dead centre on one, *both* sensors
missed it and he fell through to the icon's bottom edge. Two sensors is
right for terrain made of 16-pixel tiles, where nothing is ever narrower
than he is; it is wrong for a desktop. There is a third now, between his
feet.

## He stood on things that were not on the screen

He would be dead centre over a program icon, a few pixels up, with clear
white under his feet — not perched on an edge, not on a hairline, on
nothing at all. And the same picture had a leftover strip of the Desk
menu sitting inside one of the icons.

Those two are the same fact. The servant's pump copies 25 lines of the
framebuffer a frame, so a change takes eight frames to be noticed, and
the first block of a payload stops the pump dead because the payload
lands in the pump's own cache. An accessory runs the instant the AES has
restored the desktop under its menu — which is exactly the region that
has not been swept yet — so VRAM froze holding a picture several frames
out of date while the payload read the live one. He was standing on a
window edge that the screen was still drawing as white.

Fixed in the servant, not here: the first staging block is refused until
a forced full sweep has completed. docs/payload.md. A third of a second,
once, before he drops in.

The band of card rows that used to go blank part way through an act was
the same thing seen from the other end: the pump was stopped *mid-sweep*
and the lines it had not reached were a contiguous band. It went with
the flush and has not been seen since — the owner, who is the one who
reported it, confirms the act is clean.

It stayed written here as a live unknown well after it was fixed, and
was repeated back to them as an open item until they said they had no
idea what it referred to. A "Known" with no date is a rumour.

## The kernal, reduced to seven symbols

`payload/sonic/shim.S`. Three are the YM2612, three are the kernal's
mouse and event plumbing, one is a VDP helper. None of them is deep.
(There was an eighth, a random number, and it went with the level
generator below -- nothing else ever asked for one.) `_DoCheckButtons` is where the pad is
read, because on GEOS the input layer would have filled
`GEN_M3_PAD_STATE` in and here nothing else does.

## The sound

All five sounds the accessory makes, and they are on both chips.

    jump              PSG channel 0        Sonic 1, SndA0
    skid              PSG channels 1, 2    Sonic 2, $A4
    spindash rev      FM5                  Sonic 2, $E0
    spindash release  FM5 + PSG noise      Sonic 2, $BC
    signpost fanfare  FM4 and FM5          Sonic 1, SndCF

The YM2612 answers on the Z80's side of the machine, so the 68000 may
touch it only while it holds the Z80's bus. `main.S` takes that bus at
entry and keeps it for the whole run — the signpost fanfare is fifty
register writes and would otherwise be fifty requests — and pulses the
reset line first, because the reset line is the Z80's and the YM2612's
together and what the Mega CD's boot ROM left in the chip is written
down nowhere. Both chips are silenced again on the way out: the servant
restores video and knows nothing about sound, so a channel left keyed on
would sound over the desktop for ever.

### The Z80 driver that could not run

Three of those sounds — the skid and both halves of the spindash — are
Sonic 2's, and on the cartridge GEOS-Genesis played them by queueing
sound IDs to Sonic 2's own Z80 driver. It did that on purpose: four goes
at writing an SMPS player out of the disassembly's annotations produced
four different wrong sounds, each wrong where a comment and the
cartridge disagreed, and the cartridge never disagrees with itself.

That answer does not survive the Mega CD. The driver reads its scores
through the Z80's 32KB window onto the 68000's bus, and the only memory
this machine has there is the Mega CD's PRG-RAM window — which shows one
128KB bank at a time, and which this payload switches several hundred
times a frame between the bank holding the ST screen and the bank
holding the art. Every read the driver made would be a coin toss. There
is nowhere else: Genesis work RAM is 64KB and all of it is the servant's
or the payload's, and Word RAM has EmuTOS executing out of it.

So the scores are played on this side, and the original objection is
answered rather than argued with: **nothing is transcribed**.
`tools/sonic-tools/build_sfx.py` finds each sound's header in bank 31 of
your own dump — by the shape of the header, not by an address, and it
insists on exactly one match — follows its pointers, and renders the
script a tick at a time. Header, script and voice all come out of the
cartridge. What the disassembly is for is knowing what the bytes mean:
that `$E6` is alter-volume and `$F7` is a loop that counts the first
pass, that Sonic 2's voice bytes are register-sequential where Sonic 1's
go +0, +8, +4, +C, that `zVolTLMaskTbl` differs from Sonic 1's
`FMSlotMask` at exactly the algorithm the signpost uses.

Two things in the driver are behaviour rather than data and are
reimplemented here rather than rendered. The spindash's pitch climbs a
semitone with every rev, stops climbing at twelve, and starts from the
bottom again if sixty frames pass without one — `zPlaySound_CheckSpindash`,
nine instructions in `sonic_fm_rev`. And the release's PSG half is white
noise at channel 2's pitch, which means the frequency goes to channel
2's register and the attenuation to the noise channel's, exactly as
`zPSGUpdateFreq` turns `$E0` into `$C0` for the one write and not the
other.

## The bug that was not ours

The frame index arrives in D0 from a `move.b` at the call site, which
leaves bits 16-31 as whatever was in D0 before — and `sonic_frame_record`
adds all thirty-two of them to the index table's offset. On GEOS those
bits happened to be clear and it worked for years. Here they were not:
the record pointer came back sixteen megabytes out, the DPLC entry count
read as 255 instead of 2, and the first VBlank wrote four thousand tiles
from a wild pointer straight through VRAM and out the other side. The
screen was noise from the very first frame.

It is worth writing down because of how it looked. Every plausible
suspect — the window bank, the blob's address, the VRAM map, the palette
— was wrong, and each of them would have produced something that looked
similar. What settled it was making the payload write the DPLC entry
count and the record pointer into the servant's telemetry block and
reading them out of a WRAM dump: 255 and `$010893FC`, against 2 and a
pointer that should have been inside a 52KB blob. One `andi.l`.
