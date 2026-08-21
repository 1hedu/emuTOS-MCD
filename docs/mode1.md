# Mode 1: boot EmuTOS from the cartridge

The CD stops being the boot device and becomes a data disc. The
cartridge boots, carries everything, and plants EmuTOS in the sub CPU
the same way `boot/m1tool.S` already plants its dump program.

EmuTOS still runs on the **sub** 68000. That is not negotiable and it
is the whole reason the port exists: address 0 has to be RAM for the ST
low-memory ABI to be byte-authentic, and on the cartridge side address
0 is ROM. The cart is where EmuTOS *lives*, not where it *runs*.

## Why

Iteration currently costs a CD-R per question. That is the binding
constraint on everything else -- the `v11/v10/v01/v00` measurement that
would settle whether STAT0 bit 7 marks the chaff is sitting unburned
right now because there are no discs left. Reflashing a cart costs
nothing.

Two things fall out that were not the point but are worth as much:

* **No CDBIOS to evict.** There is no CD boot, so nothing ever loads
  into `$0-$5FFF`. Those addresses are ours from the first instruction
  instead of being taken back from firmware mid-flight. The entire
  timeshare apparatus -- parking the BIOS at `0xBA000`, exchanging 24 KB
  in and out per read, the `$5F7C` freeze, the watchdog -- becomes
  optional rather than forced.
* **The boot flag can be read properly.** The cart reads the pad on the
  main CPU before anything else happens, with no servant handshake to
  be late. The current arrangement needs a direction held for about
  thirty seconds and then breaks the desktop for having held it.

  Done, in `boot/m1emu.S`: the same two-half read and the same bit
  assignments IOFW uses, written to `$A12012` before the sub leaves
  reset. A button tapped at power-on is simply there. Verified in gpgx
  end to end -- holding Down at power-on brings up the D: diagnostic
  instead of the desktop, which on a CD boot cost a disc to ask for.

What it does **not** fix: C: will still fail at LBA 1624. Same drive,
same driver, same disc. What changes is that hypotheses about it stop
costing media.

## What the cart carries

Verified sizes, from the current build:

| | bytes | goes to |
|---|---|---|
| `emutos-segacd.img` | 236360 | sub `0x080000` (Word RAM) |
| `ADISK.IMG` | 114688 | sub `0x060000` (PRG-RAM bank 3) |
| `iofw.bin` | 11576 | main `0xFF1000` (Genesis work RAM) |

362624 bytes of payload, so a 512 KB ROM. `iofw.bin` already relocates
itself to `0xFF1000` in `iofw/crt0.S`, so it does not care that it
started life in ROM rather than in Word RAM.

`0x080000` and `0x060000` are read off `boot/sp.S`, not remembered.

## The sequence

Steps 1, 2, 4 and 6 are already proven on hardware by `boot/m1tool.S`.

1. Reset the gate array. Four writes, megadev's sequence.
2. Request the sub CPU's bus with SBRQ, wait for the grant.
3. Copy the payloads:
   * `ADISK.IMG` into PRG-RAM bank 3 -- the Mode 1 window at `$420000`
     is 128 KB, and `GA_MEMMODE` bits 7:6 select which bank appears
     there, so this is bank 3 at window offset 0.
   * EmuTOS into Word RAM. In 2M mode the main CPU sees Word RAM flat
     at `$600000`, which is simpler than the PRG banking -- 236 KB into
     a 256 KB window, no paging.
   * A four-longword vector table at PRG-RAM 0: initial SP, and initial
     PC pointing at `0x080000`. The sub fetches both on reset, so no
     bootstrap code is needed at all -- the reset vector *is* the jump
     that `Op_BootEmutos` currently performs with `jmp 0x80000`.
4. Hand Word RAM to the sub, release the bus and the reset in one
   `move.b`. Never `bclr`/`bset`: read-modify-write re-requests the bus
   the write half is giving back, and the sub never runs.
5. The sub resets into EmuTOS.
6. The main CPU relocates `iofw.bin` to work RAM and becomes the
   servant, exactly as it does today.

## Open questions, in the order they can bite

**Word RAM handover.** ~~The one step with no hardware proof behind
it.~~ Wrong when written: the handshake is exercised on this console
every boot, just not by `m1tool.S` -- `bset #1,0xA12003` in
`boot/ip.S` and `iofw/main.c`, `bset #0,0xFF8003` coming back in
`boot/sp.S`.

The real question was an ordering one, and `boot/m1wram.S` answers it.
The CD-boot path has the sub already running firmware, so the sub sets
2M mode before anyone copies anything; Mode 1 has to fill Word RAM
before a sub program exists. Under gpgx:

* `$FF8003` reads `0x02` at the sub's first instruction. MODE clear, so
  **2M is the reset default** -- nothing has to set it.
* The main CPU owns Word RAM straight after the gate array reset and
  can fill all 256 KB at `$600000`, verified by read-back in the write
  loop.
* `DMNA` hands it across intact. The sub walked all 65536 longwords and
  disagreed with none.

Not yet run on hardware.

One thing the first attempt got wrong, kept here because it will bite
again: on the **sub** side of that register bit 0 is RET and bit 1 is
DMNA. Waiting on bit 0 for ownership hangs, because the sub already
owns the memory when it starts looking.

**The PRG-RAM write protect.** Not on the original list, and it cost
an afternoon. `reset_ga` leaves `0xFF` in the high byte of `$A12002`,
which protects the first `0xFF * 0x200` bytes of PRG-RAM -- 130560 of
them -- from the **sub** CPU. The cart writes through its own window
and is unaffected, so the vector table plants perfectly and the fault
surfaces somewhere else entirely: EmuTOS's first act is to install an
exception vector at address 16 and then deliberately raise that
exception, and with the write swallowed it faults through whatever the
cart left there. Silent stop at trace E001.

`move.w #0x0000,0xA12002` before releasing the sub. `m1tool.S` and
`m1wram.S` both do it; `m1emu.S` did not, and startup.S now probes for
it permanently.

**Does the drive need the BIOS to have run?** Pier Solar answers the
first half: it cold-boots from a cart with no CD boot at all and gets
the drive to seek and play, so the drive does respond from a standing
start. What that does not cover is data sectors through the CDC, which
is a different path once the disc is moving. The CD driver is
BIOS-free and does its own `cdc_setup`, its own CDD command queue, its
own HOCK. But on a CD boot the firmware has already spun the drive up
and read the TOC before EmuTOS ever sees the hardware. In Mode 1
nothing has. Whether the drive answers a cold `CDD_CMD_INFO` from a
standing start is untested, and if it does not, that is a real piece of
work rather than a tweak.

**What the servant loses.** `boot/sp.S` disappears -- there is no sub
program but EmuTOS. IOFW reads `GA_STAT0`/`GA_STAT1` for status and
publishes the pad word, all of which survives, but anything that asks
the sub side to fetch a file does not. The A: ramdisk stops being "SP
reads it off the disc" and becomes "the cart already put it there",
which is strictly simpler.

**The timeshare's fate.** With no CDBIOS in memory there is nothing to
park and nothing to exchange. `segacd_bios_*` should compile out or
report unavailable rather than fail six times a round. Not urgent --
`segacd_bios_available()` already gates it -- but it should say so
honestly rather than looking like a broken firmware.

## What a cold drive costs the boot

Not a hypothesis any more. In gpgx with no disc in the tray, the Mode 1
boot spent **57 seconds** between the version banner and the desktop,
and **under 15** with the CD driver latched dead from the first
instruction. The console agreed: "it just took foreeeeever".

None of it was drive spin-up and none of it was Mode 1. It was
`CD_XCHG_BUDGET`, eight seconds, charged per failed block and spent
twice -- `cd_read_block` retries a failed fill with double the leash --
across every block the boot reads. `disk_init_all` probes the unit,
`pun_info_setup` follows it, and the desktop asks `Dfree` of every drive
before it draws anything.

Holding A, which stops the driver sending the drive any commands at
all, changed nothing: the waits are on a clock, not on the
conversation. That is what ruled out the drive being cold and pointed
at the leash.

Fixed in the driver rather than here, since a CD boot with an
unreadable disc has the same problem: the boot and any read following a
failure get the mount probe's two seconds and one try, the dead latch
expires after ten seconds instead of never, and a single successful
read puts the drive back on Sega's full patience. Mode 1 now reaches
the desktop in about 15 seconds of emulated time; the CD boot still
reaches it with A, B and C mounted and zero CD errors.

## Build order

1. ~~Word RAM handover~~ **done, on hardware.** `boot/m1wram.S`:
   `verdict C0DE`, all 65536 longwords, `$FF8003 = 0x02`. The console
   and gpgx agree byte for byte.
2. ~~The full plant~~ **done in emulation.** `boot/m1emu.S`: EmuTOS to
   Word RAM with matching checksums, ADISK to bank 3, two longwords of
   vector table, release. Boot trace reaches **E005, biosmain** -- so
   EmuTOS runs on the sub CPU out of a cartridge.
3. ~~IOFW from the cart, and a desktop~~ **done, on hardware.** The
   desktop boots from the cartridge with nothing in the tray.

   What stood between step 2 and this was one constant: `iofw/hw.h`
   defined `PRG_WINDOW` as `0x020000`, which is where the PRG-RAM
   window sits in Mode 2. In Mode 1 it is at `0x420000`, and
   `0x020000` is inside the cartridge's own ROM -- so the pump's reads
   succeeded and returned EmuTOS's image, which is 68000 code, and its
   writes went to ROM and vanished. Every screenful of "corruption"
   was the cartridge displaying itself. The window is now picked at
   runtime from the `M1OK` flag the loader plants at `0xFF0140`.
4. ~~A boot that does not take forever~~ **done.** See above.
5. C:, which is where the CD-boot branch left off, only now each
   attempt costs a reflash instead of a disc.

   Nothing here can answer it: neither emulator will attach a disc to
   a cart boot. gpgx emulates the Mega CD hardware for a Mode 1 cart
   but with an empty tray; PicoDrive does not attach the Mega CD at
   all, and the loader's own probe correctly refuses and halts at mark
   `10` rather than running into nothing. So C: on Mode 1 is a
   hardware-only question from here.
