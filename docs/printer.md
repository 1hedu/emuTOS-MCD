# PRN: a printer on the serial port

`Bconout(0, c)` on a real ST goes to the Centronics port on the back.
This machine has no Centronics port, and it has no MFP either -- the chip
whose registers `bios/mfp.c` writes to drive one -- so EmuTOS is built
here with `CONF_WITH_PRINTER_PORT=0` and the vector for device 0 is
free. That is where this goes.

What the bytes leave on is the EXT connector, which `iofw/uart.c` already
drives as an 8N1 serial port for the keyboard: the 315-5309's serial
engine, 4800 baud, about 480 bytes a second. It has been full duplex
since that file was written -- `uart_enable()` sets SOUT beside SIN --
and nothing had ever had anything to say on the transmit side.

The receiving end is a GEOS-Genesis printer driver, and the frame below
is its wire format rather than anything invented here. That end is in
this repo too, unmodified: `hardware/pico-printer/` is the RP2040
firmware that deframes the stream and drives an HP DeskJet 340 over
Centronics, with the wiring, the level-shifting warning and a prebuilt
`.uf2` in its README, and the whole path drawn in
`hardware/pico-printer/wiring.svg`. The frame is its frame precisely so that the
printer cannot tell which of the two operating systems is talking.

## The frame

    [0xA5]  sync
    [mode]  0 text, 1 raster, 1bpp
    [len]   two bytes, high first
    [...]   len bytes of payload
    [sum]   8-bit sum of everything after the sync

One frame is one page: mode 0 is printed and then form-fed, so the page
break is the frame boundary and there is no separate command for it.

## The two sides

EmuTOS is on the sub CPU and the serial port is on the main one, so a
byte written to `PRN:` has to cross. It does not cross a byte at a time
-- that would be one gate-array handshake per character at 480 characters
a second, for a page that takes nine seconds anyway.

**Sub side, `emutos/bios/segacd.c`.** `segacd_bconout0()` accumulates
into a 4096-byte page buffer. `segacd_printer_init()`, called from
`bios_init()`, points `bconout_vec[0]` at it and rounds the buffer to a
512-byte boundary inside its own arena, because the servant addresses it
in 512-byte blocks and a linker section is not obliged to land on one.
An earlier version used `__attribute__((aligned(512)))`, which aligned
the object correctly *within its section* and then the section landed at
`0x2140`; the servant read `0x2000` and streamed a page of zeros.

A page goes out when it fills, or when `prn_tick()` -- called from the
INT2 handler beside the keyboard -- has seen half a second of silence.
That is what makes a `Bconout` loop of arbitrary length turn into pages
without the program having to say so. `prn_flush()` is two cart ops:

  * **op 9** -- where the page is. `GA_CART_LBA` carries the 128KB bank
    in its top two bits and the 512-byte block index in the rest, the
    same encoding op 7 uses for a payload's bulk data.
  * **op 10** -- how long it is, and go. Refused while a page is still
    on the wire; the sub tries again, and can be nine seconds ahead.

**Main side, `iofw/main.c`.** `prn_step()` is a state machine over the
frame's fields -- sync, mode, the two length bytes, the body, the
checksum -- and it offers the port exactly one byte per call, taking
`uart_send()`'s refusal as "come back later". It is called from both
spin loops in `wait_vblank()`, which is the only idle this program has
and is worth about a hundred bytes a frame: far more than the wire can
take, so the wire is the limit and the pump loses nothing.

The body is read through the Mega CD's window in 64-byte chunks by
`prn_fill()` -- one bus grab and one bank switch per chunk rather than
per byte. That runs once per frame at frame-loop level, and not from
inside `wait_vblank()`: the sub CPU's bus is not grantable from there and
the first version asked from there and was refused every single time.

`prn_watchdog()`, also once a frame, abandons a job whose state machine
has not moved in ten seconds. Nine is the honest worst case for a whole
four-kilobyte page, so this only fires on a port that is not draining --
and without it, one such job would leave the servant refusing every page
for the rest of the session, which is exactly what an unenabled UART did.

## Turning the port on

`uart_enable(1)` is called from op 10 if the port is not already up,
rather than at boot. A connector nobody has said is a serial port is not
a thing to start driving pins on unasked; wanting to print is one of the
two ways of saying so, and the serial keyboard is the other. It stays on
afterwards -- the receive side is gated on the same flag, and an
unplugged keyboard says nothing.

## What is not here

`Prtblk` (XBIOS 24) is `xbios_unimpl` in EmuTOS and `v_hardcopy` is not
implemented either, so there is no screen dump, and mode 1 of the frame
above has nothing generating it yet. `Setprt` (XBIOS 21) is a stored word
that nothing reads. Per-printer drivers were GDOS's job on a real ST and
EmuTOS ships no GDOS, so what reaches the wire is the bytes the program
wrote and nothing has translated them.

`desk/desksupp.c`'s `print_file()` copies a file to `PRN:` raw, which is
what the desktop's Print does, and `progs/prntest.c` is twenty-three
bytes of the same thing.

## Testing it

`PRNAUTO=1 tools/build-iso.sh U` puts `AUTO/PRNTAUT.PRG` on the disc; it
writes one line to `PRN:` and waits. `tools/build-rom.sh` refuses to wrap
that file into a cartridge, along with the other emulator-only AUTO
programs.

Genesis Plus GX and PicoDrive both model the port's registers as plain
storage -- the write lands, TFUL never sets, and the byte goes nowhere,
because there is no peer. So an emulator can prove what was *emitted* and
nothing else. `uart_send()` mirrors the last sixty-four transmitted bytes
and a count to `0xFF0E80`, which is what that proves it with:

    a5 00 00 17 45 6d 75 54 4f 53 20 6f 6e 20 74 68
    65 20 4d 65 67 61 20 43 44 0d 0a ea

Sync, text, twenty-three bytes, `EmuTOS on the Mega CD\r\n`, and the
checksum.

The same arithmetic on GEOS's own bring-up packet gives
`a5 00 00 0a "GEOS-PRINT" f2`: 744 for the ten characters, plus ten for
the length, is 754, and 754 & 0xFF is `0xF2`. That is the cheapest check
there is that the two senders agree about the frame, and it costs
nothing to keep here.

`0xFF0D40` carries the job's own counters: `'PRNT'`, the current page's
length, pages completed, jobs the watchdog abandoned, and the state
machine's state beside the port's enable flag.

## The bug that hid all of this

Nothing reached the wire at first, and none of the telemetry was wrong
about anything -- it was being written correctly and then overwritten.

The servant's code is linked at `0xFF1000` and the planar screen cache
starts at `0xFF7000`. Its BSS ended at `0xFF7408`. Everything declared
last -- `prn_state`, `prn_buf`, `prn_len`, `prn_pos`, `prn_bank`,
`prn_base`, and `uart_on`, which is the serial keyboard's own enable flag
and had been sitting there for weeks -- was inside the region the pump
fills with four thousand bytes of screen every frame.

Nothing was checking. `tools/build-iso.sh`'s `cbin()` now links to an ELF
and fails the build if `__bss_end` is past `0xFF7000`, and the 1600-byte
BRMINIT work area that was a quarter of the servant's BSS borrows the
cache deliberately instead, the same way a payload does.
