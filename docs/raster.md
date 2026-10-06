# The raster palette

An ST shows sixteen colours at a time, but a program can show more by
reprogramming the palette partway down the screen. It counts scan lines
with MFP Timer B and rewrites some colours each time the timer fires.
NEOchrome's colour picker, Dungeon Master's status panel and Cyber
Paint's menus all do this. On this machine there is no Timer B, the
MFP's addresses alias the gate array, and the palette belongs to the
Genesis VDP on the other CPU. So a program describes the changes, and
the servant makes them from the VDP's own line interrupt.

## For a program

Find the `SgCD` cookie (API version 36 or later) and call

    api->control(SCD_RASTER, (long)table, 0, 0);    /* on */
    api->control(SCD_RASTER, 0, 0, 0);              /* off */

`struct scd_raster` is in `emutos/bios/scdapi.h`:

    gen        bump it after every change to the table
    count      entries, at most SCD_RASTER_MAX (32)
    entries    line (1..199), mask, then one 0x0RGB word per set bit
               of mask, lowest colour first

Each entry means that from ST line `line` down, the colours in `mask`
are the ones given. Lines must ascend. The top of every frame is the
ordinary palette, as `Setpalette` left it. The table stays in the
program's memory and must stay put until the program turns the raster
off. The desktop clears it when it starts, in case a program dies
holding one.

`progs/neo.c` is a complete example. It translates NEOchrome's Timer B
handlers into a table, double-buffered so the servant never reads a
table being rewritten.

**Keep the VBL routine cheap.** The servant holds the sub CPU's bus for
roughly 184 of the frame's 262 lines while it reads the screen (it
does, raster or not). What is left is all a program has, VBL routines
included. The first version of NEOchrome's launcher rebuilt its
261-word table every VBL. That was enough to stop NEOchrome's own main
loop. It now checks three words per frame and rebuilds only on a
change.

## The block

EmuTOS writes the table's address beside the screen on display, the
same way the palette and pointer blocks go there (`scd_ras_vbl`,
`emutos/bios/segacd.c`):

    +32000  PAL!  palette             (38 bytes)
    +32064  CUR!  pointer sprite      (144)
    +32208  scroll notice             (6)
    +32216  blit notice               (18)
    +32240  RST!  gen, table address  (10)

So a screen needs 32,256 bytes from a 256-byte boundary, and it must
not cross a 128 KB boundary in sub RAM, because the servant reads it
through one bank of the window.

## The servant

`ras_follow()` in `iofw/main.c` reads the block in the same bus grab as
the palette. When the address or generation changes, it reads the
table through the window and builds an event list. There are two
buffers above the planar cache. The new list waits in `RAS_NEXT`, and
the handler takes it up at the bottom of a frame, so a list is never
used half-built.

`iofw/raster.S` is the handler. The VDP reloads its line counter
(register 10) whenever the counter runs out, with the value it holds
at that moment. A value written in the handler therefore sets the gap
after the *next* interrupt, and the list is built one step ahead. Every
frame runs the same shape:

    line 2, line 5      empty events, in the top border, which set the
                        gap to the first real one in time
    each entry          the colours, at the end of the line before it
    line 216            under the picture: the ordinary palette back,
                        2 for the next vblank, and any new list taken up

Each event records its line, and the handler checks it against the
V counter. If one is ever late, the chain would be wrong for every
frame after. So on a mismatch the handler skips colours until the next
frame's line 2 and starts again in step.

The handler moves the VDP's address to CRAM, and the address can't be
read back. So:

- the tile pump (`scd_tile`), which writes VRAM all through the
  display, clears `RAS_HIT` before each tile and redoes the tile if the
  handler has set it;
- every other VDP sequence in the servant runs with interrupts masked:
  palette and pointer uploads, nametable rows, the sprite, the OSK.

The servant runs at interrupt mask 7 as always. Only while a raster is
in use does it drop to 3, which lets the level 4 line interrupt in and
nothing else.

Level 4's vector is a six-byte jump slot at `$FFFD0C` in RAM. The Mega
CD BIOS points it there, and `tools/build-rom.sh` points the
cartridge's there too. The servant writes `jmp ras_hint` into the slot
when the raster starts and puts back what was there when it stops. A
payload switches the raster off before it runs.

Memory: the lists and their state live at `0xFFED00`, between the end
of the planar cache and the stack, which measures under 500 bytes of
the 3,840 there. Code space was the tight part: 112 bytes were left
under the cache. Building the cold code for size (`osk.c`, `uart.c`,
`input.c`, start-up probes, the list builder) made room. 220 bytes are
left now.

## What it costs

A colour change costs about 600 main-CPU cycles, a little more than one
scan line. NEOchrome's toolbox uses 21 events a frame. In Genesis Plus
GX, NEOchrome with its toolbox up gets 84 VBLs per 100 frames instead of
100, because some frames of servant work now run long. With the toolbox
hidden the table is empty and only three events run. The desktop and
anything else that does not ask for a raster pay nothing. The desktop
frame is byte-identical.

## On a real console

Two things to look for that an emulator does not show:

- **CRAM dots.** Writing CRAM while the beam is in the picture puts a
  speck of the written colour where the beam is. The handler starts in
  the horizontal blank, but 13 colours run on into the next line's left
  edge. NEOchrome's bands change on the one-pixel gap between picker
  rows, which is where the ST's own late writes landed too.
- **Timing.** The H-interrupt fires near the start of the horizontal
  blank. The emulator's timing for this is close to, but not exactly,
  the console's.
