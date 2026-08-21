# Native payloads: running Genesis-side code from a disc

EmuTOS runs on the Mega CD's sub 68000. The Genesis' own 68000 is the
servant, and it is the only one of the two with a path to the VDP, the
pads and the sound hardware. So a `.PRG` can draw into a framebuffer and
nothing else: no sprites, no scrolling planes, no PSG. Anything that
wants the actual machine has to run over there.

This is how it does. A payload is Genesis-side code on a disc; a program
on the sub side feeds it across and asks for it to be run; the servant
gives it the VDP, and takes the machine back when it returns.

It is deliberately not shaped around any one application. The first
payload is a port of the Sonic accessory out of GEOS-Genesis, but the
servant knows nothing about Sonic — it stages bytes, jumps, and cleans
up. A second native thing costs a file on the data disc and no change
here.

## The file, in two parts

    SONIC.MDP   the code: a 32-byte header and the image, 32000 bytes
                at most including its working space. Staged across into
                the servant's planar cache and jumped to.
    SONIC.MDD   optional, and only if the code needs more memory than
                that: bytes, of any shape the payload likes, loaded
                straight into PRG RAM. Same name, different extension,
                same folder.

## The header

The `.MDP`'s first 32 bytes:

    +0   long   'MDPL'
    +4   word   format version, 1
    +6   word   flags, none defined, must be zero
    +8   long   entry offset from the start of the image
    +12  long   image length in bytes
    +16  long   bytes of working space wanted after the image
    +20  8      a name, for the loader to show and for diagnosis

The image is loaded at **$FF7000** and is linked for it. Position
independence is not required and not expected: the address is fixed and
is the same in both boot modes, because it is Genesis work RAM rather
than anything the Mega CD moves around.

`image length + working space` must fit in **32000 bytes**. That is not
a guess at what is spare — see below.

## Where it runs, and why there is room

Genesis work RAM is 64KB and all of it is spoken for: the servant's code
at `$FF1000`, the disc sector buffer and telemetry below that, and a
32000-byte planar cache at `$FF7000` that holds a copy of the ST screen
so the converter can tell which tiles changed.

That cache is the payload's. While a payload runs the pump is stopped —
the payload owns the VDP, so there is nothing to convert and nothing to
compare against. On the way out the servant sets `pump_resync`, which
already exists for the cartridge swap: the cache is rebuilt from scratch
and three full sweeps repaint every tile. So the payload gets 32000
bytes of real memory at a fixed address, and the only cost is that the
screen is rebuilt afterwards, which it has to be anyway.

Word RAM is **not** available and is worth saying so explicitly, because
it is the obvious place to look: EmuTOS itself executes from it, at sub
`$80000`, on both boot paths. There is no spare 256KB on this machine.

## What the payload is handed

Entry is an ordinary `jsr`, and a payload returns with `rts`.

    a0   the ST screen, 32000 bytes, planar, read-only
         320x200 in four bitplanes interleaved a word at a time -- the
         desktop as it stands. A payload that wants the picture on the
         screen as terrain, or as anything else, reads it here.
    a1   the working space: the first byte after the image
    d0.l bit 0    set on a Mode 1 boot (cartridge), clear on a CD boot.
                  Almost nothing should care; it is there because the
                  two maps differ and a payload that talks to the Mega
                  CD needs to know.
         bits 9-8 which 128KB bank of the window the bulk data is in
         bits 11-10 which bank the ST screen is in
         bits 31-16 a value that differs from one run to the next: the
                  servant's main-loop pass counter, which has been
                  running since the console came up. It is not entropy
                  and nothing should treat it as any, but a payload is
                  staged from a file, so its `.data` — and therefore any
                  seed it ships with — is identical on every run, and
                  without this a "random" payload draws the same random
                  thing forever. Seed from it.
    d1.l the bulk data, as an address in the window -- or zero when
         there is none. See below.
    d2.l how many bytes of bulk data were actually loaded.

Interrupts are masked to 7 on entry. The payload owns the VDP, VRAM,
CRAM, the pads and the Z80 completely, and may leave every one of them
in any state it likes.

## The screen has to be the screen first

The pump notices changes 25 lines at a time — a full screen takes eight
frames to be *seen* — and the first staging block stops it dead, because
the planar cache is where a payload lands. So whatever had not been
swept at that moment stayed stale in VRAM for the whole run.

An accessory is called the instant the AES has restored the desktop
under its own menu, and that restore is precisely the part not yet
swept. The result: the picture the payload reads and the picture on the
screen were different pictures. Sonic stood on a window edge that VRAM
still showed as clear white, and a leftover fragment of the drop-down
sat inside a program icon.

So the servant now refuses the first staging block — silently, with no
acknowledgement — until a forced full sweep has been and gone. The sub
CPU is sitting in `cart_xfer`'s poll, which waits about three seconds;
this costs about a third of one, once, before the payload starts.

Nothing in the contract changes. A payload still gets `a0` = the ST
screen, and now that is also what is on the screen.

## Getting it back

The VDP register file is write-only. Nothing can read the old values
back, so the only way to return the machine is for the side that knows
them to write them all again — the servant does exactly that on return:
`vdp_init()` reprograms the registers, reloads the sixteen ST colours
and the keyboard's palette lines, rebuilds the nametables, re-uploads
the on-screen keyboard's tiles, and sets `pump_resync`.

The consequence for a payload is that it owes nothing. There is no state
to save and restore, no convention to honour, and no way to leave the
machine broken by forgetting one. That is the whole reason the handover
is shaped this way.

## Bulk data: more than 32000 bytes

A payload that needs more than its 32000 bytes -- an art pool, a sample
bank, a level -- puts it in a second file beside the `.MDP`, with the
same name and the extension `.MDD`. NATIVE.PRG loads it into PRG RAM and
tells the servant where it went; the servant hands the payload the
address in `d1` and the length in `d2`. Nothing in either program knows
what is in it.

Where it goes is not a constant, and the first version of this document
said it was: **sub `$60000`, 124KB free**. That address is the **C:
ramdisk**, which is the boot drive, and a payload's art landed on top of
it. What is actually true:

  * EmuTOS's own memory ends at `phystop`, sub `$60000`.
  * The C: ramdisk starts there. Its length is the total-sector count in
    its own boot sector, which is whatever `tools/build-iso.sh` built it
    at -- `ADISK_SIZE`, 0x1C000 by default.
  * Sub `$7C000`..`$7EFFF` is the timeshare's scratch, and `$7F000` up is
    the sector bounce buffer and the captured-sector slot.

So the bulk arena is **everything between the end of the ramdisk and
`$7C000`**, and on a disc whose ramdisk is the default size that is
twelve kilobytes. A disc that carries a payload with a `.MDD` builds a
smaller C: -- `tools/build-datadisc.sh` uses 32KB and drops from it
everything that is also on D:, which leaves 112KB.

`SCD_BULK_INFO` is how a program asks: it fills two longs, the byte
address and the length, and a length of zero means the ramdisk filled
the region. NATIVE.PRG refuses the payload rather than loading anything
anywhere.

The whole arena is inside **one 128KB bank of the Mega CD's window**, and
the ST screen is in another. A payload that wants both selects the bank
it needs, one byte to `GA_MEMMODE`, using the bank numbers it was handed
in `d0`:

    move.b  GA_MEMMODE,%d0
    andi.b  #0x3F,%d0
    or.b    bulk_bank_shifted,%d0
    move.b  %d0,GA_MEMMODE

This is the arrangement Sonic's port uses. Its art pool is 49568 bytes
of tiles which the engine streams from a frame at a time -- at most 23
tiles per VBlank into a 32-tile VRAM slot -- so the pool has to be
*readable memory*, not video memory. Its bank is the one selected for
the whole run, and `sonic_pixel` borrows the screen's for four words a
time, because it is the only thing in the engine that reads the screen.

(An earlier note here said the pool could go to VRAM and the per-frame
load cues could be dropped. That was wrong: it read the size of the pool
and not what the engine does with it, which is exactly what the
cartridge does with it.)

## The two ops

Both go through `GA_CART_REQ`, the register the sector path already
uses, as `(op << 8) | seq`, acknowledged on `GA_CART_ACK`.

  * **op 4 — stage.** `GA_CART_LBA` holds a block index. The servant
    copies 512 bytes out of the bounce buffer into `$FF7000 + index*512`.
    This is the sector path's own buffer and its own handshake, in the
    other direction; nothing new was invented to move the bytes.
  * **op 5 — run.** The servant checks the header, stops the pump, masks
    interrupts, jumps, and on return puts the machine back.
  * **op 6 — stage into VRAM.** 512 bytes at a block index, straight
    into video memory. For a payload that wants tiles in place before it
    starts rather than uploading them itself. Sonic does not use it --
    its engine does its own uploading, from the pool -- but the facility
    is general and costs nothing.
  * **op 7 — where the bulk data is.** `GA_CART_LBA` carries the bank in
    its top two bits and the 512-byte block index within it in the rest.
    The servant turns that into a window address for `d1`.
  * **op 8 — and how much of it there is,** in 512-byte blocks, for
    `d2`. Two ops rather than one because the request register carries
    one word, and a payload that reads past what was loaded reads the
    timeshare's scratch and would rather be told.

A payload that fails its header check is refused and the op acknowledged
anyway, so a bad file cannot hang the program that sent it.
