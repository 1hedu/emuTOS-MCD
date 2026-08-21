# Where this branch stands

Written at `mode1-emutos`, submodule `23fc911`, 121 patches in
`patches/emutos/`.

There are now two ways to boot this port and they are level with each
other. Both reach a desktop on hardware with four drive letters, and
both are stuck at the same place, which is the disc.

## What the port is

EmuTOS runs on the Mega CD's **sub** 68000, where address 0 is PRG-RAM,
so the Atari ST low-memory ABI is byte-authentic rather than emulated.
The Genesis main CPU runs IOFW, a servant that converts EmuTOS's planar
framebuffer to the VDP and injects pad input as IKBD packets.

**CD boot** (the CD branch): security block, IP, SP,
`EMUTOS.IMG` off the disc. The console's CDBIOS is **evicted** at
handoff, because its code occupies `$0-$5FFF`, which is where the ST
sysvars have to live.

**Mode 1** (this branch): the cartridge boots and carries everything --
`emutos-segacd.img` to Word RAM, `ADISK.IMG` to PRG bank 3, `iofw.bin`
to Genesis work RAM -- plants a four-longword vector table at PRG 0 and
releases the sub CPU. The reset vector *is* the jump that
`Op_BootEmutos` performs from the disc side. No CD boot happens, so
there is no CDBIOS in low memory to evict and none parked in Word RAM;
`segacd_bios_probe()` returns `0xFFFF` and the timeshare never arms.
The disc, if there is one, is data only.

## What works, on hardware

Both boots:

* A desktop with four drives.
* **A:** a ramdisk -- from the disc on a CD boot, from the cartridge in
  Mode 1. Carries `FORMAT.PRG` (which asks which drive), `SRAMTOOL.PRG`
  and `CDTEST.PRG`.
* **B:** the console's 8 KB internal backup RAM as a FAT12 volume. Hold
  C at power-on to reformat it. Present on both boots, and no longer
  displaced by a cartridge. (If it is missing under an emulator, that
  is a blank `.brm` with nothing to format it, not a fault in the
  port.)
* **C:** the cartridge's save RAM, proxied by the servant. 63 sectors
  of the 64 in the 32 KB window; `boot/m1emu.S` keeps the last one for
  its own boot report.
* **D:** the FAT16 filesystem on the disc. It was C: until the cartridge
  took that letter. (This said "intermittently" for a week after it had
  stopped being intermittent -- see "What does not work" below.)
* The disc diagnostic is `CDTEST.PRG`, launched from the desktop like
  any other program. It reads the driver's counters through the `SgCD`
  cookie rather than being wired into the operating system, so a fault
  in it cannot take the boot down. Start stops it -- read off the pad
  at `0xFF8012` rather than through GEMDOS, because reaching the
  keyboard means opening the OSK and the OSK is toggled by Start.
  Holding Down at power-on used to run it instead of the desktop, and
  the drive wake used to be gated on the same button. Both are gone:
  no boot of this cartridge needs a button held for anything.
* It writes every line it prints to `C:\CDLOG.TXT`, and
  falls back to `B:` if there is no cartridge. On a Mode 1 boot that
  saves a boot: the log is already on the cartridge when the run ends,
  so there is no second boot into `boot/m1tool.S` to copy `B:` across.
  Getting the save file off the cart afterwards is whatever it always
  was -- this changes what has to be run, not how the card is read.

Mode 1 only:

* Boots to the desktop from the cartridge with an **empty tray**.
* `wake_drive` runs on **every** boot, no button: the
  loader finds all three of the console's packed sub-CDBIOS blobs, has
  the console unpack them, and lets Sega's firmware run DRV_INIT before
  EmuTOS is loaded. It can only happen there -- the wake needs the sub
  CPU running Sega's BIOS rather than EmuTOS, so no program can ask for
  it afterwards. It was gated on Down; the gate saved the DRV_INIT
  timeout on an empty tray and cost a combination nobody could confirm
  from outside the machine, which on hardware read `-` every time. The
  outcome is one character on the status line, `W`: `A` awake, `T` the
  firmware never answered, `H` a handover was not acknowledged, `B` no
  CDBIOS found, `U` unpacked but not into firmware.
* `boot/m1tool.S` lifts B: into cart SRAM so a flash cart carries it to
  an SD card. Proven and checksummed end to end; it produced every log
  quoted here, and has its own branch, `mcd-sram-tool`.

## What has been ruled out

The value of a day like the last one is mostly negative results, and
they are worth more written down than the guesses they replaced.

| ruled out | how |
|---|---|
| The disc | Sega's firmware read LBA 1600 off it, on this machine, twice (`W R`, `W Z`) |
| The drive | same |
| Our sector numbering | `W Z` -- 1600 failed then succeeded on retry, so the first failure was time, not the address |
| The CTRL0/CTRL1 pair | `tried 7`: all three of the firmware's decoder configurations went through the drive, none decoded a header |
| The read lead-in (`L20` vs `L2`) | identical results across every log |
| The sub-module header | megadev writes version `0x0100`, the BIOS's own module `0x0010`, Doom CD32X `0x0001`; all three work |
| Our transfer path | `CDC_DEST_SUB`, DBC/DAC/DTTRG and the drain loop all match CDCTRN; it never runs, because the ring is never filled |
| Our decoder init | register for register identical to the firmware's at `0x21B0` |
| Retrying with a decoder reset | ROMREAD reaches CDCSTART (US `0x3B50`), so Sega re-inits on retry too |
| "The timeshare is architecturally wrong" | it was unreachable on Mode 1 and testing the wrong half of a status word; it runs now |

And the instrument failures, which cost as much as the code ones:

* `hdr` was `cdc_hdr[]`, written only on a completed transfer -- so
  "the decoder is not decoding" was three sessions of reading a field
  that only ever meant "no sector arrived".
* The parked-CDBIOS banner tested `SCD_BIOS_SWAPS`, which counts
  exchanges *run* and is zero at startup whatever is parked.
* `tsrc`'s low byte was read as a drive state twice; it is only written
  on rounds where DRV_INIT is armed, and both failing rounds had it off.
* The rung was sampled instantaneously against a fast cycle, so three
  identical readings looked like a ladder that never moved.

## What does not work

### D: was intermittent on 13 August, and has not been since

**Resolved. Kept because how it stopped being true is the point.**

What was measured then, on the CD branch before the Mode 1 work: the
directory at CD LBA 1616 read and returned a real block, and the file's
own data at LBA 1624 did not -- `lastfail 1624 x2` in both rounds of the
hardware log, `Fopen` succeeding and `Fread` returning -11, with the
failure moving under the read lead-in (`why 1111` at twenty frames,
`why 2222` at two). A positioning problem with a knob on it.

The Mode 1 work then went in, the owner reported D: reading successfully
off the cartridge-launched build, and the branches were unified. The
fault has not been seen since, on either boot path.

Nobody re-ran this section. It sat under "What does not work" for a
week, was copied into every context summary as a live defect, and was
still being reported back to the owner as an open item after they had
already told me D: worked -- until they asked, plainly, whether they
even had the problem. They did not. **A finding is a measurement with a
date on it, and this file is where the date goes.** The rest of "What
does not work" below is subject to the same doubt and should be re-run
before it is repeated.

`DDAUTO=1 tools/build-iso.sh U` now puts `AUTO/DDISKAUT.PRG` on the
disc: it reads and verifies `D:\FILLER.BIN` from AUTO, bounded by
`DDAUTO_BYTES`, and holds the result on screen. That is a regression
guard, not a re-test of the above -- the original was a hardware log,
and no emulator run can speak to what a real drive does with its own
lead-in.

### 97% of what the sector ring sifts is chaff

The ring works on hardware -- 420 entries, then 1139. But beside those,
14,378 sectors were rejected by its mode gate, every one reading mode
byte `0x60`, and 14,398 had STAT0 bit 7 clear. The same population to
within twenty.

Bit 7 is the flag this driver deliberately stopped obeying, on the
grounds that gating on it once refused 11,949 consecutive sectors. If
those two counts are one set, that call was wrong and the gate goes
back in. The current build counts the four combinations
(`v11/v10/v01/v00`) to settle it. **That disc has not been burned.**

gpgx cannot answer it: it never clears bit 7, so `v01` and `v00` are
structurally zero there. Every conclusion this project has drawn about
that flag from emulation was drawn from a constant.

### The CDBIOS timeshare runs now, and fails later

It never ran on Mode 1 at all -- nothing was parked, `bios_parked()`
was false and `segacd_bios_probe()` answered `0xFFFF`. With the loader
parking 24 KB at sub `0xBA000`, the last hardware log reads
`CDBIOS probe 0000 near 0002 far 0300 sw 8`: CDBCHK answers, eight
swaps have run, and the diagnostic's `T` rounds went from
`rd 65535` (nothing to call) to executing.

They end at `tsrc 0300` -- stage 3, no sector was ever announced --
which is a later failure than the one below and a different one.

The `00` in that code is **not** a drive state. `34(a5)` was written
only inside the DRV_INIT block, and both failing rounds so far were
`I0` rounds, the arm that switches DRV_INIT off, so the byte kept its
cleared zero. `brd_fail_stat` samples `CDB_STAT` itself now, so the
next `03xx` discriminates: 1, 2 or 4 means the drive was live and the
sector is what never arrived (the CDC decode side), 0 or 7 means the
drive is dead at read time despite the wake (the swap window).

Two reasons it could not have worked before are gone. The park is one.
The other is that `brd_wait_init` masked `0x00FF` of the status word
and compared against 4 and 1, when the drive state is in the **high**
byte: it therefore never matched on any boot, spun to its 200-frame
timeout every visit, and read from a drive nothing had confirmed.

### What the timeshare used to do

Parking the firmware in Word RAM and exchanging it back into low memory
to perform reads works as a mechanism -- the BIOS executes, the CDD
exchange happens -- but every read fails, and names its own failure:
stage 3, "no sector was ever announced", with the drive reporting state
0. Six failures a round, then it stands down.

Moot in Mode 1, where there is no firmware to park. The diagnostic
still sweeps it; it reports `65535`, "nothing there to call".

### Neither emulator can put a disc in a Mode 1 machine

gpgx emulates the Mega CD hardware for a cart boot but with an empty
tray. PicoDrive does not attach the Mega CD at all -- the loader's own
probe catches that and halts at mark `10` rather than running into
nothing. So **the disc on Mode 1 is a hardware-only question**, and every
Mode 1 CD reading taken before `216eb6e` is void anyway: the servant
was reading the cartridge, not the framebuffer.

## What Pier Solar does, and where we differ

Pier Solar and the Great Architects is the one shipping cartridge that
does what this project does -- boot from a cart, use the Mega CD if one
is under it -- so its Mode 1 bring-up is the reference. Rev C, its ROM
addresses:

| what | where | notes |
|---|---|---|
| detect | `0xF95C` | version reg bit 5, `'SEGA'` at `0x400100`, `'BR'` at `0x400180`, two ASCII version digits at `0x400187/8` -> BCD |
| clear comm | `0xF710` | `clr.l` `$A12010/14/18/1C`, `clr.b` `$A1200E` |
| clear 32 K | `0xF7F4` | called 16 times: four chunks per bank, all four banks |
| copy | `0xF810` | |
| unpack | `0xF830` | **their own** LZSS, not the console's |
| load | `0xFA16` | the whole Mode 1 bring-up |
| handshake | `0xF9B8` | pulse IFL2, poll comm for `'OK'`, then hand over Word RAM |

Its `0xFA16` in order: write protect `0xFF00`; `3`, `2`, `0` to
`$A12001`; poll SBRQ until granted; zero all 512 K a bank at a time;
unpack the sub BIOS to PRG-RAM `0x0000`; copy the sub program to
`0x6000` clamped to `0x1A000`; `ori.b #1` and poll SRES back; `andi.b
#0xFD` and poll SBRQ back; handshake; `move.b #0x2A,$A12002`.

**Where we deliberately differ.** Pier Solar hardcodes the address of
the packed sub BIOS per BIOS revision -- `0x415800`, `0x41AD00`,
`0x416000`, selected from a version table -- and gives up entirely on a
revision it does not recognise. It then unpacks the blob with its own
reimplementation of Sega's compressor. `wake_drive` instead finds the
blob *and* the console's own unpacker by searching for one six-byte
instruction (`lea 0x20000,a1`, which occurs exactly once), and calls
the unpacker in place. No version table, no reimplementation, nothing
of Sega's copied into this tree, and it works on revisions nobody here
has ever seen.

**What we took.** Clearing the comm block before the firmware can read
it; zeroing PRG-RAM before unpacking into it; and above all splitting
the release into two acknowledged handovers -- SRES lifted and polled
back, *then* SBRQ dropped and polled back -- instead of one write of
`0x01` and an assumption. Marks `17` and `18` are those two
acknowledgements; `F9` and `FA` are their timeouts.

## Things that cost a run each, so they are written down

* **The "NULL buffer sets the media change" Rwabs trick is floppies
  only.** `blkdev.c:436` is `if ((dev < NUMFLOPPIES) && (buf == NULL))`.
  `FORMAT.PRG` ended with `Rwabs(RW_READ, NULL, 2, 0, drive)` for both
  its drives. B: is a floppy unit and took that branch; C: is not, so
  the identical line fell through into an ordinary two-sector *read
  into address zero* -- and on this port address zero is PRG-RAM
  holding the exception vector table. FORMATC wrote a perfectly good
  filesystem and then shot the sub CPU through the head: hourglass,
  then a screen of evenly spaced stripes, then nothing. The clue that
  named it was that the stripes ignored the 160-byte line stride --
  they were a linear run through memory, not anything a graphics call
  could draw.
  The replacement is EmuTOS's own documented path: `Getbpb` rebuilds
  the BPB and clears both change flags (`blkdev.c:612`), then a
  *logical* write of sector zero sets `forcechange`, because
  `blkdev.c:458` exists so that "altering logical sector zero causes a
  media change to be detected". Both programs now end by asking `Dfree`
  what GEMDOS sees, which is the only figure worth printing: it logs
  the drive, reads the new BPB and walks the FAT, so `57 of 57 clusters
  free` on C: and `11 of 11` on B: confirm the whole stack in one line.
* **The image has four bytes of headroom.** It is loaded at sub
  `0x80000` and the CDBIOS parks at Word RAM `0xBA000`, so 237568 bytes
  is a hard wall that `tools/build-iso.sh` enforces. The last feature
  was paid for by shortening string literals, which does not work
  twice. The obvious place to buy real space is the timeshare: 24 KB of
  park for a mechanism that has never once succeeded on hardware and is
  moot in Mode 1 anyway.
* **`PRG_WINDOW` moves by `$400000` between the two boots.** Mode 2 has
  it at `$020000`, Mode 1 at `$420000`. Getting this wrong is silent
  and convincing: `$020000` in Mode 1 is inside the cartridge's own
  ROM, so reads succeed and return plausible-looking bytes -- EmuTOS's
  own image, which is 68000 code and reads exactly like memory
  corruption -- while writes vanish into ROM. `iofw/main.c` now picks
  it at runtime from the `M1OK` flag at `0xFF0140`.
* **`reset_ga` leaves the PRG-RAM write protect at `0xFF`**, which
  protects the first 130560 bytes from the *sub* CPU. The cart writes
  through its own window and is unaffected, so the fault surfaces three
  layers away as a silent stop at trace E011. `move.w #0x0000,0xA12002`
  before releasing the sub. `startup.S` probes for it permanently.
* On the **sub** side of `$FF8003`, bit 0 is RET and bit 1 is DMNA.
  Waiting on bit 0 for ownership hangs -- the sub already owns it.
* 2M mode is the gate-array **reset default**: `$FF8003` reads `0x02`
  at the sub's first instruction. Nothing has to set it.
* `$5F7C` (`_LEVEL2`) points into the region the timeshare overwrites.
  Park an `rte` there first or the console freezes.
* `$A130F1` = `0x01` enables cart SRAM. `0x03` silently rejects writes;
  bit 1 is write **protect**, not enable.
* Releasing the sub CPU with `bclr`/`bset` never works: read-modify-write
  re-requests the bus the write half is giving back. One `move.b`.
* The planted sub program must spin, not `stop`. A halted CPU does not
  answer the bus request that comes next.
* Polling the sub's comm register while the sub runs freezes the main
  68000 at exactly the third read.
* A Mode 1 ROM must be a power of two, minimum 32 KB. 32878 bytes would
  not launch at all.
* A payload `lea` in `boot/m1emu.S` must be **absolute**. The images sit
  a quarter-megabyte down the ROM and PC-relative displacement is
  sixteen signed bits.
* megadev's `init_system` skips the VDP control-port cancel exactly when
  a flash-cart menu hands over. Do it unconditionally.
* `tools/build-iso.sh` used to leave stale `EMUTOS*.IMG` in `build/fs/`,
  which put three old builds on a disc behind buttons. It cleans now.
* The freshly built **J**-region ISO does not boot under gpgx; the U
  build does. Use `tools/build-iso.sh U` for emulator regression runs.
* **Neither emulator takes an address error, and the console does.**
  The gpgx core has no address-error path at all -- no symbol, no
  string -- and Musashi, which PicoDrive uses, guards its own behind
  `M68K_EMULATE_ADDRESS_ERROR` and calls it "partially emulated". So a
  misaligned word or long access runs perfectly here and kills the
  console. `CDTEST.PRG` did: a thirteen-byte string in `.data` left
  `.bss` on an odd address, every static in the program was misaligned,
  and the first long written to one panicked. `tools/prg.ld` pads every
  section to four now and `tools/mkprg.py` refuses to build a `.PRG`
  whose text or data length is not a multiple of four -- the linker
  script is easy to edit and hard to notice.
* **CRAM takes a byte address, not a palette-entry number.** A palette
  line starts every 32, so line 2 is at 64 and line 3 at 96 -- megadev
  loads its second palette at `to_vdp_addr(32)`. `osk.c` had entry
  numbers there for four days, so the two lines the keyboard actually
  draws in were never written by anything and it rendered in whatever
  CRAM was left over. It changed colour when the boot path changed,
  with no source change, which reads exactly like somebody broke it.
* **Colour index 0 is transparent on this VDP**, whatever the palette
  line holds. Anything designed as a background -- a key face, a
  selection highlight -- is the backdrop showing through and cannot be
  changed per palette line. Highlight the ink.

## Method notes, earned the hard way

* Read the build that wrote a log, not the build in the tree. The
  `why 1111` reading was taken from current source against a log written
  by a driver 44 commits older, where that nibble is computed from a
  counter zeroed nineteen lines earlier and *cannot* read otherwise.
* Look at frames, do not count their colours. Six commits were bisected
  as "broken" on a metric that was measuring a held button.
* Never pipe a build to `/dev/null`. Three runs went by against a stale
  ROM because a link error scrolled past.
* Price a suspicion before fixing it. "The drive is cold" was wrong --
  holding A, which stops the driver talking to the drive at all, moved
  the boot timeline not at all. The 42 seconds were a leash, not a lens.
* The cart occasionally hands back a stale save. A dump identical to the
  previous one is not evidence about the run -- and a dump can now be
  dated: `boot/m1emu.S` writes its report at byte 32256 and its marks at
  32320, where builds before `51e3a56` used 0 and 16384.
* A drive letter in a diagnostic is a bug waiting for the day another
  drive appears. The disc's diagnostic silently switched to testing the
  cartridge the moment the cartridge got C:, and reported the
  cartridge's cluster count while doing it.
* Confirm a telemetry offset against a known-value field before
  believing any other field in the same block. A misread offset in one
  work-RAM dump produced a whole false story about drive B:, with a
  sector count attached, while the field that disproved it sat four
  words away in the same dump.

## Rebuilding

```sh
git submodule update --init
cd emutos && make segacd ELF=1          # -> emutos-segacd.img
cd .. && tools/build-iso.sh U           # -> build/emutosmd-U.iso + .cue
                                        #    and build/iofw.bin, ADISK.IMG
tools/build-rom.sh boot/m1emu.S         # -> build/m1emu.bin, the cart
tools/run-emu.sh gpgx build/m1emu.bin 900 build/out
python3 tools/ppm2png.py build/out/frame.ppm /tmp/f.png
python3 tools/bram-extract.py build/out/cart.brm CDLOG.TXT
```

`tools/build-rom.sh` takes its payloads from `build/`, so the ISO build
has to run first even when no disc is wanted.

The submodule's remote is upstream EmuTOS, so the port lives as
`patches/emutos/*.patch` -- 136 of them, and the only copy of the
driver that exists on a server. To restore the submodule from scratch:
check out the base and `git am patches/emutos/*.patch`; the recipe is
in `patches/emutos/README.md` and it rebuilds a byte-identical tree.

"Regenerated on every change" was the intention and not the practice:
the series had drifted thirteen commits behind the branch, which is the
failure mode a snapshot has -- a stale one reads as a backup right up
until it is needed. Regenerate it in the same breath as any commit to
`emutos/`, before pushing.

### Boot is slower since the cartridge became a drive

Reported from the console, not reproducible here: gpgx still reaches
the desktop at the same frame it did before. The suspect is that every
C: sector is a round trip that waits on the servant's main loop, and
`cart_xfer` gives it four million spins -- about three seconds on a
12.5 MHz sub CPU -- before calling it a failure. A handful of those
would look exactly like this and would be invisible in an emulator
whose main loop is never late. Uninstrumented: there is no counter for
cart round trips or timeouts yet.

## Where it goes next

C:, from the cart. Same drive, same driver, same disc as the CD branch
left off with -- the `v11/v10/v01/v00` measurement of STAT0 bit 7 is
still the outstanding question, and it is now askable without burning
anything. Run `CDTEST.PRG` from the desktop and read the answer off
`C:\CDLOG.TXT`, which the flash cart carries to an SD card by itself.

### The welcome screen was never a keypress, it was the quiet window

`iofw/main.c` used to sit in a 300-frame loop after the handoff --
"give EmuTOS an undisturbed boot: no SBRQ, no INT2, for ~5 s" -- which
also did no screen conversion at all. EmuTOS paints the welcome screen
into the framebuffer at 0x58000 around frame 390 and clears it again
around frame 630; the quiet loop ran to about frame 690. The picture
came and went entirely inside a window in which nothing was converted,
so the television showed the backdrop colour throughout.

Measured, not inferred. A dump of sub PRG-RAM 0x58000 at frame 500 has
the full welcome screen in it, rendered legibly, while the frame the
VDP is showing is uniform colour 0, the pump's cache at 0xFF7000 is
entirely zero and its heartbeat at REPORT+6 is still zero. Two things
that looked like the cause are not: forcing a full re-sweep every 8
frames instead of every 300 changes nothing, and making
`sub_bus_grab_polite()` never yield changes nothing -- because the pump
was not running at all. `frclock` staying at zero until frame ~650 is
the same fact from the other side: the quiet window withholds INT2, so
EmuTOS's own VBL counter does not advance either.

The window now withholds only the INT2. The pump runs from the first
frame and takes the bus through `sub_bus_grab_polite()`, the same gate
it uses afterwards -- the sub raises the interlock when it must not be
halted -- and the cart proxy inside that window has always grabbed the
bus unconditionally anyway, because B: is mounted there. This is the
one part of the change that alters boot-time bus behaviour and it wants
a console run before it is trusted.

### TITELSNG.PRG does not fit, and cannot be made to

It decrunches a 208166-byte song (the first longword of LIED.DAT).
Layout: packed data read to prog+0x4084, block-copied up by the
unpacked size to prog+0x448a+size, decrunched back down to prog+0x3ffc.
Peak footprint is basepage + 0x6A3D6, about 434 KB. With the basepage
at 0x14676 and phystop at 0x58000 the program has 277 KB, so it runs
158 KB past the end: first through the 32 KB framebuffer -- the rainbow
-- then through the A: ramdisk image at 0x60000 and the parked firmware
above it.

Caught in the emulator with a write-watch on $050000-$05FFFF filtered to
writes made from below $80000 (EmuTOS executes from Word RAM, so
anything below that is a .PRG): 400 hits, all from pc=0x014A2C, which is
TITELSNG text+0x2B6, the `moveb (a0)+,(a1)+` of that block copy. A2 in
EmuTOS's panic dump is 0x4b698, exactly where the decrunch output ends.

Not fixable here. Deleting drive A: entirely would put the RAM top at
0x7F800 and programs would still only reach 0x77800, 29 KB short, and
Word RAM's spare 26 KB is not contiguous with PRG-RAM. The program
would fail the same way on a 512 KB ST.

### The boot drive is A:, and that is not cosmetic

EmuTOS's `DEFAULT_BOOTDEV` is `HARDDISK_BOOTDEV` -- C: -- and
`bios.c:859` takes it when C: exists and falls back to A: when it does
not. On a TOS machine that is simply correct. A: and B: are the floppy
drives and C: is the first hard-disk partition, so C: is where the
system, the applications and the desktop's own INF live, and preferring
it is what a person with a hard disk wants and expects.

This machine's drive letters do not mean that. C: here is an 8 KB save
chip on whatever cartridge is in the slot: it may be absent, it may
hold somebody else's game saves, and FORMATC.PRG exists so it can be
erased. A: is the ROM-resident system disk -- always present, always
ours, unchanged by anything the user does. So the rule is kept and the
answer comes out the other way, because the letters underneath it are
not the ones it was written for.

`bootdev` is not where EmuTOS came from -- it came from the cartridge
before any drive existed, the same way TOS is in ROM on an ST and is
not loaded from C: either. It is a GEMDOS notion: the drive `Dsetdrv()`
makes current, the drive a boot sector is tried on, the drive the AUTO
folder is looked for on, and the drive that becomes `PATH=` in the
default environment through `init_default_environment()`.

It is also where the desktop looks for its own files. `app_rdicon()`
and `read_inf_file()` both adjust their filename by `G.g_stdrv`, which
is `dos_gdrv()` at desktop start, which is whatever `Dsetdrv(bootdev)`
made current. With C: as the boot drive, EMUDESK.INF and EMUICON.RSC
were looked for on the cartridge and not found -- and nothing reports
that. The desktop builds a default INF and uses the built-in icons,
exactly as it would on a machine that had never been given any, which
is why the first attempt at the icons looked like a malformed resource
rather than a file in the wrong place.

Safe by construction rather than by luck: with bootdev below
NUMFLOPPIES, `bootcheck()` calls `flop_boot_read()`, and `flopio()` for
MACHINE_SEGACD dispatches to `segacd_floprw()` -- our own ramdisk
driver, no FDC hardware anywhere near it. The FAT boot sector does not
checksum to 0x1234, so it returns "not a valid boot sector" and the
boot carries on.

One consequence to remember: the AUTO folder is looked for on A: now,
not on the cartridge.

### The drive letters are names

A: and B: are the floppy slots, and this machine has no floppy drive.
It went through two arrangements before the obvious one: originally
A: ramdisk, B: internal RAM, C: cartridge, D: disc, which put fixed
storage on the floppy letters; then A: cartridge, B: disc, C: ramdisk,
D: internal, which follows the TOS convention that A: and B: are what
you can take out. The second is defensible and still says nothing about
what any drive *is*.

So the letters are chosen:

|   | drive | why |
|---|---|---|
| C: | the ramdisk | the system drive, in cartridge ROM |
| D: | the disc | |
| I: | internal backup RAM, 8K | |
| S: | the cartridge's save RAM | |

There is no A: and no B:. `drvbits` has always been a sparse bitmap, so
a machine with no A: is unusual rather than novel, and it is more honest
than naming a compact disc after a floppy so the first letter has an
occupant.

C: keeps its usual meaning, which is the point: `DEFAULT_BOOTDEV` is C:,
unmodified upstream EmuTOS, and it is correct without an override.

The mechanism is one narrowing. `add_partition()` calls
`next_logical()`, which returns the lowest bit still set in
`devices_available` -- so masking that word down to a single bit before
`disk_init_one()` puts a device on exactly the letter wanted, and
nothing else in the block layer has to know. All four are devices 0 to 3
of one `SEGACD_BUS`, dispatched in `segacd_disk_rw()` on the device
index.

**One thing does not reach past P:.** `PUN_MAXUNITS` is 16 -- EmuTOS's
own `blkdev.c:151` says "cannot store info for devices > P:" -- so
PUN_INFO cannot describe S:, and `pun[2+18]` does not fail, it reads
into `partition_start[]` and returns a plausible number. FORMAT.PRG
needs the physical unit for raw access, so it takes this machine's
layout directly: unit = NUMFLOPPIES + DEVICES_PER_BUS*SEGACD_BUS +
device, which is 34 to 37, offset zero because none of them is a
partition inside anything. PUN_INFO is still consulted for any drive it
can describe.

Verified in the emulator: four drives under the new letters with their
own icons, the desktop finding EMUDESK.INF and EMUICON.RSC on C: with no
override, and CDTEST on D: reaching Fopen and reading back 54 68 69 73
-- "This", the opening of the file that is on the disc.

## What the console's own driver does

The probe was written from notes. The Mega CD BIOS is the driver this
hardware was built for, and it is in `vendor/bios/` -- so it was
disassembled, the same way the CDC family was in stage 3.

The BURAM dispatcher is **main-CPU** code, around $0070EE in a US BIOS,
working from a two-entry pointer table at $0070D4: `$600000` for the
data and `$7FFFFF` for the write protect. Both addresses confirmed.

Detection is the routine at $0080B0, the one BRMINIT calls and whose
carry becomes "no RAM":

```
    moveq  #0,d0
    move.b $400001,d0
    btst   #7,d0          ; set -> error, no cart
    move.l #8192,d1
    andi.b #7,d0
  1 asl.l  #1,d1
    dbf    d0,1b          ; d1 = 8192 << ((id&7)+1)
```

Two corrections fall out. **Presence is bit 7**, not the low three bits:
an empty slot floats every line high and a cart pulls bit 7 down to
announce itself. The old gate -- "low three bits between 1 and 6" --
refused a cart reporting 0 or 7 and accepted open bus that happened to
read as 3. And the size is `8192 << (n+1)`: one doubling more than the
notes, and one more than Genesis Plus GX, which reads the same id as
`8192 << n`. Both cannot be right and there is no cart of Sega's own
here to settle it, so the size is measured instead of believed.

The write test at $008120 is our `cart_holds()` exactly -- `$5A` then
`$A5` at $600001, original restored -- which is reassurance that the
rest of this is on the right track. The one difference: the BIOS
write-enables with `bset #0` at $7FFFFF, a read-modify-write, where this
code was storing a whole byte of `0x01` and flattening whatever else
lives in that register. Now it reads first.

None of this is copied. The addresses and the sequence are facts about
the hardware; the code is ours.

## The ID register was never a presence test

The backup RAM cart probe asked $400001 whether a cart was there. That
register does not answer that question. It answers "how big", and only
if something is driving it.

With the slot empty, $400001 is open bus. Genesis Plus GX could not show
this at first -- with no cart file loaded it mirrors $000000-$3FFFFF into
$400000-$7FFFFF, and the SRAM it enables by default at $200000 lands on
$600000 and holds written bytes, so an empty slot answered like a 64K
cart. `GPGX_CARTSIZE=-1` was added to the local core to leave the whole
region unmapped, which is what an empty slot really does. The probe then
read an id of 1 there, and on a rerun a 4: inside the range the code was
calling "a cart is present", and different each time, because open bus is
whatever the bus last carried.

So presence is decided by memory that remembers -- `cart_holds()`, two
different bytes written and read back, restored afterwards. Nothing an
empty slot can fake. The id is consulted only after that, and only for
the size.

And when the id is junk, the size is measured. A cart wires as many
address lines as it has memory; offset 8192<<k folds back onto offset 0
as soon as k passes the last line it has, so a marker at each power of
two finds the fold. `GPGX_CARTID` was added alongside, so a clone that
reports 0x00 or 0xFF over real memory can be stood up here. Verified:

| slot | id reg | result |
|---|---|---|
| empty | open bus | refused, hold 0 |
| 64K | 3 | 128 sectors |
| 512K | 6 | 1024 sectors |
| 512K | 0xFF | 1024 sectors, measured |
| 64K | 0x00 | 128 sectors, measured |
| 128K | 0x00 | 256 sectors, measured |

One consequence had to be paid for. The old probe would only look at the
memory once the id had named a cart, so anything it found was a cart by
construction, and formatting it unasked was safe. It is not any more: a
game cartridge's battery-backed save RAM is also memory, it also lands on
this window in Mode 2, and it is also full of somebody's saves. The
automatic format now requires either a cart that declared itself or a
region that arrives blank.

### And it says so on the screen

Three builds went out reporting nothing, and three times the answer came
back as "no difference", because a cart that is not found looks exactly
like a cart that is not there. `cart_show()` now puts the probe's
working on the screen at boot: both ID bytes, the write-protect register
before and after the enable and at both of its decodes, the data byte
and what came back after `5A` and `A5`, and the two size answers.

It appears only when there is something to explain -- no cart found, and
the slot not reading all-ones, since an empty one floats every line high
and needs no explanation. Start dismisses it; it dismisses itself after
eight seconds so an unattended boot still reaches the desktop.

## Nothing in that window answered at all

The first screen off a real console read:

```
ID.400001-DE ID.500001-01
WP.WAS-01 WP.NOW-01 WP.700001-01
```

`WP.WAS` was read before anything was written. `$400001` and `$500001`
are the same page of the same register on a Sega cart and should read
alike; they do not. Every write read back as itself. That is prefetch
residue from end to end: **nothing decodes $400000-$7FFFFF on that
machine.** There is no ID register and no write-protect register there
to talk to, so no amount of getting Sega's protocol right would have
helped.

What is in the slot, then. A clone built on a generic Mega Drive
save-cart board has none of Sega's cart hardware. It has plain SRAM,
and two things follow:

  - the Sega mapper hides it until bit 0 of **$A130F1** is set. Every
    Mega Drive game with a save does this. The Mode 1 path here has
    always done it. Mode 2 never did.
  - the SRAM may sit on either half of the data bus. Odd bytes is the
    common wiring and what Sega's carts use; even-byte boards exist and
    are invisible to an odd-byte probe.

So the probe now tries four wirings -- odd and even, before and after
the enable -- and uses whichever answers.

Two things had to be fixed to test that.

**The emulator models $A130F1 wrong in Mode 2.** `default_time_w` writes
the mapper's result into `memory_map[0x20]`, which in Mode 2 is Word RAM
-- where EmuTOS lives. On hardware the cart decodes its own internal
$200000, which the CPU reaches at $600000. Genesis Plus GX now does the
Mode 2 thing: a backup RAM cartridge ignores /TIME entirely, and a save
cart's SRAM appears at $600000. `GPGX_CARTSIZE=-2` stands one up.

**A read of address zero is not a read of address zero.** The write test
needs a known value on the bus between the write and the read back, or
an unmapped region hands back the byte just written and passes. The
first version read `$000000` -- the BIOS ROM, certainly present in Mode
2. gcc turns a dereference of a literal null into `__builtin_trap()` at
-O2, so the servant executed an illegal instruction and the console
reset before it drew anything. The flush now reads a variable of our
own.

| slot | result |
|---|---|
| empty | refused, hold 0 |
| save cart behind $A130F1 | found, 32K |
| 64K id 3 | 128 sectors |
| 512K id 6 | 1024 sectors |
| 512K id 0xFF | 1024 sectors, measured |
| 128K id 0x00 | 256 sectors, measured |

### Sweep the window, and never suppress the screen

Four guessed base addresses were not enough. The console reported no
cart at $600000 or $600001, with or without the mapper enable, which
rules out the four places a cart is *supposed* to put its memory and
nothing else. A clone need not use any of them.

`cart_scan()` now asks all of them: the first byte of each 128K of
$400000-$7FFFFF, on both halves of the bus, write-read-restore with the
bus flushed in between. Two 32-bit maps, eight hex digits each, on the
screen. If no byte anywhere in four megabytes keeps a write, there is no
memory in that slot reachable by the main CPU, and that is about as
conclusive as software gets.

And the screen is no longer gated. It used to be suppressed when the
window read all-0xFF, on the theory that an empty slot floats high and
needs no explanation. That cost a whole test cycle: the console that had
been reporting `DE` and `01` came back reading `FF` everywhere after an
unrelated change to the code near the probe, because open bus is the
last thing the bus carried and that depends on which instructions ran,
not on what is in the slot. The screen was hidden on exactly the boot
that needed it. Any Mode 2 boot that finds no cart shows it now.

The screen also carries the build's short hash and which mode branch the
probe took, so a photograph identifies itself.

## I: never mounted, and it was never the hardware

Two bugs, found by going back to the Mode 1 cart and trying to open I:.

**The block layer wants two bytes nobody was writing.** A volume with no
partition table reaches `check_for_no_partitions()` in `bios/disk.c`
only when its root sector ends `55 AA`. `tools/mkfat.py:182` writes
them, which is why C: and the disc image mount. `bram_format()`,
`cart_format()` and `progs/format.c` all wrote a perfectly good FAT12
boot sector and left offsets 510-511 zero, so `atari_partition()`
returned "Non-ATARI root sector" and no drive letter was allocated.

D: and S: got away with it by accident. `internal_inquire()` marks the
disc and the cartridge `XH_TARGET_REMOVABLE`, and `disk_init_one()`
force-adds a partition to a removable unit whatever the root sector
says. The internal backup RAM is soldered down, so nothing covered for
it, and I: simply never appeared. Formatting it and rebooting did not
help: the new volume was written by the same formatter.

All three now write the signature, and a volume that is already ours but
missing it gets the two bytes put in rather than being reformatted --
whatever is on there is the user's and it survives.

**And the guard on the internal memory is gone.** `bram_probe()` used to
look for Sega's `SEGA_CD_ROM` footer, assume game saves, and refuse to
format. Every Mega CD's own BIOS writes that footer before the owner
ever sees the machine, so the rule fired on every console ever built:
I: could not work anywhere, while `EMUDESK.INF` still put an icon on the
desktop that errored when clicked. Narrowing it to "formatted and not
empty" would have been a better guard and still the wrong idea. This is
eight kilobytes on a console somebody has deliberately booted a 1985
desktop on, from a disc they burned to do it. SRAMTOOL.PRG copies the
memory off for anyone who wants it kept.

Verified by reading `drvbits` at $4C2 out of sub PRG RAM, which is now
in the emulator probe next to the 200 Hz counter:

| | before | after |
|---|---|---|
| with a 512K cart | `0004000C` = C D S | `0004010C` = C D I S |
| with no cart | `0000000C` = C D | `0000010C` = C D I |

The icon on the desktop was never evidence -- it comes from
`EMUDESK.INF`, which lists CDIS unconditionally. `drvbits` is.

### And the same guard on the cartridge

`cart_probe()` would format an unannounced cart only if it arrived
blank, the fear being a game cartridge's battery-backed saves. It
protects nothing, for two reasons.

A ROM cartridge grounds /CART, which puts the machine in Mode 1 and
boots the cartridge. If this disc is running at all, whatever is in the
slot is not a game.

And fresh SRAM powers up with garbage, not zeros, so "blank" would have
refused most new carts -- leaving S: mounted, through the removable
fallback, over a filesystem nothing can read. That is the worst of the
three outcomes and the hardest to diagnose from across a room.

It formats now unless our own signature is already there.

### The console's BIOS could see the cart all along

Reported from the machine: the Mega CD's own memory manager said the
**cartridge** needed formatting, and did it. That is decisive. The BIOS
detects the cart with the routine at $0080B0 -- `move.b $400001,d0` and
`btst #7` -- and writes it through $600001 with the enable at $7FFFFF.
Our addresses. So the cart is on the bus, answering, and every "nothing
in the slot responds" reading here was our probe's fault, not the
hardware's.

Two things behind that.

An assumption stated as fact: that a Mega CD's BIOS formats this memory
before the owner ever sees it. That is true of the internal 8K and it is
what justified dropping the guard there. It is **not** true of a
cartridge, which arrives unformatted, and saying it in the same breath
sent the owner looking for a fault that was not there.

And the probe did more than the BIOS does. It wrote the enable bit to
$700001 as well as $7FFFFF, guessing that a cart might decode the
register at the bottom of the page rather than the top. The BIOS reaches
this cartridge through $7FFFFF alone, so the second write buys nothing
-- and $700000 on a clone may be a bank register, where writing 1
selects a bank that is not populated. It reads $700001 for the screen
now and writes only where the BIOS writes.

### The ladder

Every hypothesis about this cartridge had been costing a disc: build,
burn, post, wait, read one number back, guess again. So the guesses go
in the code and it climbs them in one boot. `cart_climb()` sweeps the
whole window after each rung and stops on the first that answers:

| rung | action | why |
|---|---|---|
| 0 | as found | a cart with its memory simply mapped |
| 1 | `$7FFFFF \|= 1` | Sega's write enable, as the BIOS does it |
| 2 | `$A130F1 = 1` | the Mega Drive mapper's SRAM enable — what an ordinary save cart needs, and what the Mode 1 path always did |
| 3 | `$700001 = 0` | if the bottom of that page is a bank register |
| 4 | `$700001 = 1` | ...then neither bank should go untried |
| 5 | `$7FFFFF = 1` | a plain write, in case the read half of the read-modify-write returns junk that sets bits which turn the memory back off |

Least invasive first, so a cart needing nothing is found before anything
has been poked at it, and the rungs stack. Six sweeps of sixty-four
write-read-restore pairs is nothing on a boot that already waits on a CD
drive.

The screen reports all six, one row each, both halves of the bus:

```
STEP       ODD      EVEN
0 ASFOUND  00000000 00000000
1 WP7FFFFF 00000000 00000000
...
```

so a single photograph says which rung woke the cart, or that none did.

## The cartridge carries its own driver

Six rungs over four megabytes found nothing, while the console's own
memory manager formatted the cartridge without difficulty. Both were
true, and the reason is in the BIOS at **$0003F6** -- not in the BURAM
API that was decompiled first, but in the startup code that builds the
main-CPU jump table at $FFFD06:

```
    3ec: move.l #$000070EE,(a0)   ; install _BURAM -> the BIOS's handler
    3f2: lea    %pc@(0x416),a1    ; -> "RAM_CARTRIDG"
    3f6: lea    $400001,a2
    3fc: tst.b  (a2)
    3fe: bpl    done              ; bit 7 clear: an ordinary cartridge
    400: lea    15(a2),a2         ; -> $400010
    404: moveq  #5,d1
    406: cmpm.w (a1)+,(a2)+       ; 12 bytes
    408: dbne   d1,406
    40c: bne    done
    40e: move.l #$00400020,(a0)   ; _BURAM -> the CARTRIDGE's driver
```

A cart that sets **bit 7** of $400001 and carries **"RAM_CARTRIDG"** at
$400010 replaces the BIOS's backup RAM handler with a routine of its own
at $400020. Its memory is wherever that routine says it is, and there is
no reason for any of it to be visible at $600000.

The console reported `ID.400001-DE`. Bit 7 set. And $0080B0 agrees from
the other side: its `btst #7` calls bit 7 set "no RAM", which is correct
and means *no plain RAM at $600000* -- on such a cart there is none to
find. Every sweep this project has run was looking in the one place the
answer could not be.

Two absolute references to $400001 exist in the whole 128K ROM. The
BURAM one was found first and read carefully; this one was three
hundred bytes from the reset vector and went unread for a day, while
five discs went out testing addressing guesses.

The probe now reads $400010 and reports it, and when the signature
matches it calls $400020 with BRMINIT -- after the diagnostic screen has
been drawn, so a cart that hangs the machine still leaves its evidence
on the television. Register use from megadev's `lib/sub/bram.h` (MIT),
which agrees with what $0070EE does with the same arguments.

## EJECT.PRG was reading a register that moved

The symptom arrived as two: on a CD boot, running EJECT.PRG from the
disc hung the machine; run from S: it opened the tray and then ignored
the A button. One bug.

When the CD boot lost a race to `GA_CMD1` carrying both the live pad and
the flags held at power-on, the pad moved to `GA_CMD3`'s top byte.
CDTEST was updated to `0xFF8016`. EJECT.PRG was not, and kept polling
`0xFF8012` for bit 6 -- which that register never sets, since it now
holds `0x5A00 | flags`. So `while (!(PADWORD & PAD_A));` could not
finish, and "the CD driver hangs on eject" was a program waiting for a
button on a dead address. It still works on the Mode 1 branch because
that branch never moved the pad.

Two things, so it cannot happen again in this shape.

The pad's address and bit names live in `bios/scdapi.h` now -- the
header both the driver and the programs already agree on and already
version-check -- rather than as a number copied into each program.
`SCD_PAD_A` is 0x4000; verified against the running servant, which
publishes `cmd3=4002` with A held and `cmd3=8000` with Start, not
against the derivation.

And the wait is bounded. This program opens the tray *before* it waits,
so a wait that cannot end leaves the machine with no disc and no way
back. A minute, then it closes the tray and remounts anyway, and it
takes any face button rather than only A.

Also fixed alongside: CDTEST's log fallback wrote to C: then B:, which
was the old lettering. B: has not existed since the drives were renamed,
so the fallback was a drive that could never answer. S: then C: now --
the cartridge first, because it survives the power going off.

### Eject from S: or I:, and why the answer is on screen now

Reported: EJECT.PRG works from C:, and from S: or I: it did not open the
tray but returned cleanly. Nothing in the program knows which drive it
was loaded from, so the difference is somewhere it cannot see -- and the
leading explanation is dull: a stale copy of EJECT.PRG left on those
drives from an earlier disc, failing the driver version check and
returning without doing anything. S: and I: keep their contents across
a reburn; C: is rebuilt from the ISO every boot.

That was checkable from here, and it is wrong. `SCD_VERSION` is 30 and
the only commit that ever touched it predates every disc that has gone
out, so a stale copy passes the version check and runs -- and the
pre-`befdc4f` copy would open the tray and *then* hang on A. Neither
happened. Something else.

**The VBL rotation could overwrite the command.** `SCD_EJECT` puts its
packet in `cdd_next` and raises `cdd_handover` for the CDD interrupt to
collect. The 32-frame status question writes `cdd_next` too, and it was
suspended only while a sector read was outstanding -- not while a
command sat queued and unsent. Land in that window and the eject is
replaced by a status query and never goes out, with nothing to report:
the caller queued, the interrupt sent, the drive answered. What went out
was simply not what was asked for. The tray stays shut and EJECT.PRG
carries on and closes it again -- no tray, no hang, a clean return,
which is the report exactly. `!cdd_handover` joins the guard.

Whether that is the whole story of the per-drive difference is still
open, so the program also says which:

  - the version check prints both numbers and names the likely cause
  - the drive's own status nibble is printed before and after the eject
    command, so "the command never went out" and "the command went out
    and the drive ignored it" stop looking alike

## Hot-swapping the boot cart on Mode 1

Asked: on the Mode 1 branch, swap the boot cartridge for the backup RAM
cart while running, and use the cart's data. It cannot work as the
servant stands, and the reason is a pin.

The Mega Drive decides its own memory map from **/CART**. A cartridge
that grounds it takes $000000-$3FFFFF and the expansion port -- the Mega
CD -- takes $400000-$7FFFFF. That is Mode 1, and it is why
`PRG_WINDOW_M1` is $420000.

The backup RAM cart does not ground /CART. That is not a guess: with it
inserted, this console boots from CD, which only happens when the Mega
CD holds $000000.

So the moment the boot cartridge leaves the slot the map flips. The Mega
CD moves to $000000-$3FFFFF, and the servant's next frame reads the ST
framebuffer at `prg_window + 0x18000` = $438000, which is now an empty
cartridge slot. The screen stops on that frame. The 68000's vectors move
too, though the servant runs masked at 7 and would survive that part.

What would make it work is a servant that notices. The gate array stays
at $A12000 in both modes, so the flip is detectable -- the boot ROM's
header is at $400000 in Mode 1 and $000000 in Mode 2 -- and PRG RAM is
on the sub side and is not disturbed by the swap. A servant that
re-pointed `prg_window` and the cartridge window on detecting the flip
would carry on through it. That is a real feature and a real risk to the
branch that currently works, and pulling a cartridge out of a powered
console is a decision for the owner, not an assumption for this file.

## Is S: reformatted on every boot?

Yes, whenever the check for our own signature at offsets 2, 3 and 5
fails. That check is the only thing standing between a boot and the
user's data, and on the console it was failing every time.

It is not failing here. Genesis Plus GX starts every run with a blank
cartridge, so every emulated boot looked like the first and the question
could not be asked at all -- `GPGX_CARTFILE` now gives the cart contents
that survive a power cycle. With it: boot one formats and leaves
`60 38 45 6D 75 54 4F 53` and `55 AA` at 510; a marker planted at
offset 0x2000 by hand survives boot two intact. The signature check
works when the memory persists and the window is stable.

So on hardware it is one of those two, and the machine says which now. A
boot that formats S: puts up a three-second notice first:

```
S: WAS NOT OURS, SO IT HAS BEEN FORMATTED
BASE 600001  STEP 0  SECTORS 0400
WAS 00 00 00 00 00 00 00 00
```

`BASE` and `STEP` changing between boots means the ladder settled
somewhere else and we are reading the wrong place. `WAS` reading all 00
or all FF means the cart kept nothing -- which for a cartridge whose own
BIOS driver manages a battery-backed store would suggest the memory the
ladder found is not that store, or that there is no working cell behind
it.

It redraws on every frame it is up, unlike the probe's own screen: this
one runs after the servant has begun painting the ST display, which
covers plane A from the second row down within a frame, and the first
attempt showed its title and nothing else.

## Nothing formats at boot any more

Both drives work, so formatting whatever does not look like ours on
every boot stopped being a convenience and became a standing offer to
delete somebody's game saves.

The only reason it happened there was that a volume with no filesystem
got no drive letter: `atari_partition()` finds nothing, no partition is
added, and the drive cannot be reached from the desktop -- so boot was
the only moment it could ever be formatted. The disc and the cartridge
escaped that only because `internal_inquire()` calls them removable and
`disk_init_one()` force-adds a partition to a removable unit whatever
the root sector says, which is the right behaviour reached by accident.

`disk_init_one()` now force-adds one for **any** SEGACD_BUS device that
got nothing. The four devices always exist; what is on them is the
owner's business. An unformatted drive still fails to open, which is
honest, and FORMATS.PRG and FORMATI.PRG are there for it -- with the
prompt now saying in as many words that Sega game saves go too.

The servant's `cart_format()` is gone with it. The only write either
side makes to a cartridge at boot is the 55 AA repair on a volume that
already carries our own signature.

| | before | after |
|---|---|---|
| blank cart | formatted at boot, letters C D I S | untouched, letters C D I S |
| our volume + a planted marker | kept | kept |

### Storing our data as Sega saves instead

Raised, and worth recording rather than starting. Sega's format is
64-byte blocks with a directory growing backwards from the footer, so a
FAT image could in principle live inside it as one large entry and share
the volume with game saves. Two things make it a project rather than a
change: the BURAM interface is file-level, and on this cartridge it
lives in the cartridge's own ROM, so using it as a block device means
either reading and writing whole files or reimplementing Sega's
allocator well enough to place our data in known blocks and then reach
past the API to those blocks directly. Worth doing if the cartridge is
meant to hold both. Not worth doing by halves.

## One tree

The two boot paths were developed on two branches, and the cost came due
the moment they were put together.

The hot-swap question closed first, and closed clean. A backup RAM cart
pushed into a live slot is erased by the insertion -- the connector has
no ground-first contacts, these carts have no supervisor IC, and the
array arrives blank every time. The whole case, with the console owner's
own controlled experiment (pull and reseat the same cart, power never
cycled, map never moved, volume gone), is in `docs/ramcart-hotswap.md`.
What it settles is not a defeat but a division: Mode 1 owns the
cartridge slot and gets a scratch S:, the CD branch owns persistence
with the cart seated at power-on. Neither has to be explained in terms
of the other, so neither needs its own tree.

Putting them in one tree immediately found what two trees had hidden:
**the CD boot from `mode1-emutos` had never worked at all.** The ISO it
builds runs EmuTOS and spins forever in `segacd_cdd_init` with no drives
mounted. `segacd_cdd_init` waits for the servant to stamp `0x5A` into
the high byte of `GA_CMD1`, the buttons-held-at-power-on word, and
`input_update()` was also publishing the live pad into that same
register. In Mode 1 the cartridge writes the flags and EmuTOS reads them
before the pump starts, so the flags win and nothing looks wrong. On a
CD boot EmuTOS starts after the pump is already running, so the pad word
wipes the stamp first -- the trace shows `5A00` holding for 169 frames
and then gone. The CD branch had found this and fixed it (`50a8203`,
"One comm register was carrying two things, and the CD boot lost the
race"); the Mode 1 line never carried the fix across, and no disc had
been booted on that branch since, so nobody knew. The pad rides in the
top byte of `GA_CMD3` now, beside the mouse buttons.

Two more things the split had left behind, both carried across here: the
`55 AA` boot signature in the servant's own formatter (without it
`atari_partition()` gives a good FAT12 volume no drive letter), and
EJECT.PRG's fixes for the comm register the pad had moved out of.

And a class of bug the merge exposed rather than inherited: the hot-swap
diagnostics -- the alias check, the header dump, the evidence block, the
write-protect matrix, the power-on register forcing -- were running on
every boot of both targets. `cart_check_alias()` reads eight addresses
and writes all eight back, two of them `$020001` and `$200001`, the
PRG-RAM window and Word RAM, with no bus grant. On a CD boot that is
unarbitrated garbage written into memory holding EmuTOS. They ask
questions about a cartridge pushed into a live machine, and they now run
only when one is.

Both targets are built from this tree and both reach the desktop:
`tools/build-iso.sh U` for the CD boot (`drv=0004010C` -- C, D, I, S)
and `tools/build-rom.sh boot/m1emu.S` for the Mode 1 cartridge, with the
swap onto a formatted cart reporting 512 sectors by the read-only path
and onto a blank one 1024 by the ladder.

## Sonic runs on the desktop

The first native payload. Genesis-side code, off a disc, handed the
whole machine by the servant: he is Mega Drive sprites over the ST
screen, the desktop is the terrain, and the act is that same desktop
four screens long through a scrolling name table. docs/sonic.md is the
port; docs/payload.md is the handover it uses.

    tools/build-sonic.sh && tools/build-datadisc.sh U

Verified headlessly on gpgx, end to end: he falls in, stands, runs right
under a held d-pad while the page scrolls, reaches the sign post at the
end of the fourth screen and it spins to his face, and then he runs off
and the payload returns -- and the servant puts the desktop back exactly
as it was. Start ends it at any point; `PAYLOAD_STAT` reads 5, "came
back", either way.

Three things had to be found, and only one of them was ours.

**The documentation was wrong about free memory.** docs/payload.md said
sub `$60000` upwards was 124KB of nothing. It is the **C: ramdisk** --
the boot drive -- and `ADISK_SECTORS` was a constant covering the whole
region whether the image filled it or not. Fifty kilobytes of art landed
on top of it. The ramdisk's length now comes from the image's own boot
sector, and what is left over is published through `SCD_BULK_INFO` and
handed to the payload as a window address. A disc that carries a payload
with bulk data builds a smaller C: -- `tools/build-datadisc.sh` uses
32KB, which leaves 112KB.

**D: refuses every read until the desktop is up.** That is `cd_booting`,
it is deliberate, and it is why a program launched out of `AUTO` gets
-34 -- path not found -- from every name on D:, forever, including the
root. Three theories about GEMDOS path handling and an afternoon went
into that before anyone read `segacd_cd_rw`'s first line. `SCD_BOOT_OVER`
says the boot is over, which is what `deskmain()` would say a moment
later anyway.

**And a latent bug in the engine that GEOS never tripped.** The frame
index arrives in D0 from a `move.b`, so bits 16-31 are whatever was in
D0 before; `sonic_frame_record` adds all thirty-two of them to the index
table's offset. On GEOS those bits happened to be clear. Here the record
pointer came back sixteen megabytes out, the DPLC entry count read as
255 instead of 2, and the first VBlank wrote four thousand tiles from a
wild pointer through VRAM and out the other side -- the screen was noise
from frame one. Every plausible suspect (the window bank, the blob's
address, the VRAM map, the palette) would have looked the same. What
settled it was making the payload write the entry count and the record
pointer into the servant's telemetry block and reading them out of a
WRAM dump. One `andi.l`.

All five of the accessory's sounds work, on both chips: the jump on the
PSG, the skid on two PSG channels, the spindash rev on FM5, its release
on FM5 over white noise, and the signpost fanfare on FM4 and FM5. The
68000 holds the Z80's bus for the whole run, because that is the only
way it may touch the YM2612, and pulses the shared reset line first --
what the Mega CD's boot ROM leaves in the sound chips is written down
nowhere.

Three of those are Sonic 2's, and on the cartridge GEOS-Genesis played
them by queueing sound IDs to Sonic 2's own Z80 driver rather than
reimplementing SMPS, after four goes at reimplementing SMPS produced
four different wrong sounds. That answer does not survive this machine:
the driver reads its scores through the Z80's window onto the 68000's
bus, the only memory there is the Mega CD's PRG-RAM window, and the
payload switches which bank that window shows several hundred times a
frame. Every read would be a coin toss.

So they are played on this side -- and the objection is answered rather
than argued with. Nothing is transcribed: `build_sfx.py` finds each
sound's header in bank 31 of the dump by its shape, insists on exactly
one match, follows its pointers and renders header, script and voice a
tick at a time. What the disassembly is for is knowing what the bytes
mean. The two things that are behaviour rather than data -- the
spindash's pitch climbing a semitone a rev and forgetting after sixty
frames, and the release's noise taking its pitch from channel 2's
register while its volume goes to channel 3's -- are nine instructions
and one comparison.

And a licensing slip caught on the way: `sfx_jump.inc` had been
committed when the port's sources were first dropped in, and it holds
Sonic 1's signpost voice and frequency tables. It is generated into
`build/` now, like the art, and the tree carries none of it.
