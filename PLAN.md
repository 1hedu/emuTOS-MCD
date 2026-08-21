# EmuTOS on Sega CD — Port Plan (Rev E)

The project: **EmuTOS** (the open-source Atari TOS: BIOS + GEMDOS + VDI +
AES + GEM desktop, C with thin 68000 asm) running on Sega Genesis + Sega CD.

Rev history: A–C were AROS designs (retired — AROS needs a machine this
platform can't be); D was GEOS-on-CD (shelved by choice until the
genesis-only GEOS work completes; see git history); **E is active.**
Upstream: `emutos/` submodule (192K/256K ROM-class images; `bios/amiga.c`
is the precedent machine port; native Alt-RAM support).

## 1. Why EmuTOS fits this hardware exactly

- Same CPU (68000), written for 512K-class machines, OS in "ROM" costs no
  working RAM, GEM's native habitat is 320×200×16 — the Genesis video class.
- The Sega CD supplies what a bare Genesis lacks: **RAM** (512K PRG + 256K
  Word), a boot medium, and a freed cart slot for the **512K backup RAM
  cart as the writable disk**.
- The port pattern (new machine layer, stock OS above) is exactly what the
  Amiga port already did.

## 2. D1 — EmuTOS runs on the **sub** CPU (the ST-ABI decision)

The deciding constraint: **the Atari ST memory ABI wants RAM at address
zero** — writable exception vectors at `$0`, system variables at
`$400–$5FF`, and EmuTOS's own BIOS data above them. On the Genesis main
bus, address 0 is ROM in every mode (Sega CD BIOS or cartridge) — a
main-CPU port would need EmuTOS's low-memory model patched and would break
the sysvar ABI. **The sub CPU's address 0 is PRG-RAM.** EmuTOS runs there
with its memory model byte-authentic — plus a 12.5 MHz clock (vs 7.67; the
ST was 8) and the larger contiguous RAM.

The main CPU runs the servant firmware (**IOFW** — the design Revs A/C
already worked out, SGDK-derived): VDP blit pump, pad + Sega-mouse
sampling, backup-cart block proxy, hardware-sprite GEM pointer, vblank
heartbeat. Comm asymmetry as ever: main INT2s the sub; main polls.

## 3. Memory map (sub = the "ST")

| Range | Size | Role |
|---|---|---|
| `$000000–$0003FF` | 1 K | 68000 vectors (RAM — authentic; fully EmuTOS-owned after the CDBIOS eviction, per audit E-A1) |
| `$000400–$0005FF` | 0.5 K | **ST sysvars at their real addresses** |
| `$000600–$05FFFF` | ~382 K | **ST-RAM**: EmuTOS BSS, 32 K ST-low screen buffer, TPA (~330 K for GEM apps) |
| `$060000–$07FFFF` | 128 K | Reserved PRG bank (pinned under main's window): tile framebuffer + comm rings + staging |
| `$080000–$0BFFFF` | 256 K | Word RAM, 2M mode, **parked with sub forever**: **Alt-RAM** pool + the EmuTOS image itself (192 K-class, linked high; main-side PRG write-protect register can harden the OS pages "ROM"-style) |
| `$FE0000+` | 8 K | Internal BRAM (Sega format kept) — NVRAM/clock substitute |
| `$FF0000+` / `$FF8000+` | — | PCM (bell/audio later) / gate array, timer, CDC/CDD |

Main CPU: work RAM 64 K = IOFW; window `$020000` pinned to the reserved
PRG bank; `$400000+` = backup RAM cart (odd bytes, WP register); VDP.

Net for applications: **~330 K TPA + 60–200 K Alt-RAM** (depending on
final OS placement) — a healthy 520ST. EmuTOS's `altram_init` handles the
split pool natively.

## 4. Video: stock VDI, zero graphics porting

The whole VDI stays untouched by running the screen in **authentic ST-low
format**: 320×200, 4 interleaved bitplanes, 32 K buffer in ST-RAM. The
servant pipeline makes it visible:

```
VDI (stock) renders ST-low planar into the screen buffer (sub RAM)
sub: converts dirty regions planar→VDP tiles into the FB bank (12.5 MHz)
main @ vblank: DMAs dirty cells from the window → VRAM (letterboxed in 320×224)
```

- 16 colors from the ST palette mapped to CRAM by the servant (`Setcolor`
  hooks the port's palette path).
- GEM mouse pointer = VDP hardware sprite, positioned by IOFW at 60 Hz.
- The planar→tile converter is the one new performance-critical routine.
  **E1 measured (C, -O2): 19.7 frames per full 32 KB screen** on both
  cores exactly (1952 vblanks / 99 conversions). Full-screen repaints
  need the planned asm core (target 4–6 frames); typical GEM dirty
  rects are 1/20th of a screen or less. Optimization is an open E1.5
  work item, measured before E3 depends on it.
- **Hardware rule discovered at E1: the PRG window only connects while
  the sub CPU is off the bus.** Every main-side window access — CPU or
  DMA source — brackets with SBRQ (grab/release); the sub resumes where
  it was. Cost ≈ the transfer duration per vblank.
- **Audit E-A2 (open, needs real hardware):** GPGX and PicoDrive
  disagree about VDP DMA sourced from the PRG window (GPGX clean with
  the bulletin recipe — program source+2, rewrite first word; PicoDrive
  garbage under every recipe tried). Default transport is CPU copy
  under SBRQ, correct on both cores; the DMA path ships behind
  USE_VDP_DMA for the hardware session to arbitrate.
- PAL: 320×200 letterboxes even more comfortably; VBL rate handled by
  EmuTOS's existing 50/60 Hz machinery.

## 5. Input

- IOFW samples pads + **Sega mouse (US Mega Mouse and JP/EU variant —
  same TH/TR nibble protocol, opposite Y polarity, pref-selectable;
  `SGDK/src/joy.c` remains the reference)** each vblank → comm regs →
  INT2.
- Sub-side glue injects them as IKBD-shaped events (mouse packets,
  keyboard scancodes) into EmuTOS's existing input path — the same trick
  the Amiga port uses for its non-IKBD keyboard.
- Keyboard: GEM is mouse-first; an on-screen keyboard accessory covers
  text entry at E3 (the GEOS project's window-keyboard proves the pattern
  on this hardware); pad chords for common keys.

## 6. Timing

- Gate-array timer (INT3) programmed to 200 Hz → `hz_200`, the ST's
  heartbeat, plus scheduler ticks.
- INT2-from-main = VBL (`_vblqueue`, cursor, screen bookkeeping).
- EmuTOS clock backed by BRAM-stored date/time (no RTC: prompt-on-boot or
  last-saved, like a TT without a keeper battery).

## 7. Storage (GEMDOS drives)

| Drive | Backing | Notes |
|---|---|---|
| `A:` | Boot ramdisk: disk image preloaded from CD into Alt-RAM by the boot loader | System/desktop files; read-only-ish, cheap v1 |
| `C:` | **Best writable store found by probe**, FAT12 via IOFW block proxy | Tiering (user owns no backup cart — an MD V3 PRO flashcart with 32 K save SRAM instead): **(1)** emulators provide the official RAM cart interface (GPGX `cart.brm`, 512 K) — CI and development target; **(2)** on real hardware, probe the Mode 2 cart window `$400000+` for SRAM (a flashcart in CD-passthrough mode may expose its 32 K at `$600001` odd, `$A130F1`-gated); **(3)** guaranteed fallback: internal 8 K BRAM (sub-side, direct) holds `DESKTOP.INF` + prefs on any console. Driver geometry-parametric 8 K–512 K, odd-byte packing, WP handling; the 512 K story returns with the D10 custom cart |
| `D:` | CD-ROM, read-only | Arrives per audit E-A1's outcome (below): either CDBIOS-backed from E4, or after the native driver lands |

## 7b. Hardware run #1 — it boots

The GEM desktop came up on the user's real JP Mega CD: CD boot chain,
CDBIOS eviction, cross-CPU display pipeline, input and the A: RAM disk
all confirmed on silicon.

The one hardware gap was storage, and it is structural rather than a
bug: **a cartridge cannot be present during a CD boot** — a cart at
power-on puts the machine in Mode 1 and it boots the cart instead of
the disc (and hot-plugging one hangs the 68000). The user's MD V3 PRO
could never have served as backup storage anyway: a backup RAM cart is
three pieces of dumb glue (size ID at `$400001`, SRAM at `$600001`, WP
latch at `$700001`) that a ROM cart does not decode.

So `B:` now falls back to the **internal 8 KB backup RAM** at
`$FE0000` — sub-side, direct, present on every console, persistent
across power cycles. Opt-in by holding C at power-on, because it
usually holds Sega game saves; Sega-formatted BRAM is detected and left
strictly alone by default. Verified on PicoDrive (no emulated cart, so
it exercises the BRAM path) with the cart path still green on GPGX.

## 7c. E4.5 — the text smear, and what it actually was

**Symptom on hardware:** opening README.TXT or running EmuCON filled the
*entire* display with one repeated character — 'E' from "EmuTOS…", 'W'
from "Welcome to EmuCON" — over the top of a page that was otherwise
rendering. An empty file was clean. It cleared the instant the viewer
closed. Two plausible-sounding fixes (CRLF line endings; the servant
following EmuTOS's live screen base instead of a hardcoded one) were
shipped and neither moved it.

**Reproduced headlessly.** The path the desktop never exercises is an
all-background page with text on it, and driving EmuDesk's "Show file"
from a script means steering the pointer through two double-clicks. So
EmuTOS grew a console soak (`CONF_SEGACD_CONSOLE_SOAK=<lines>`) that
prints a text page from `biosmain` before the desktop starts. The bug
appeared on the first run, on both cores, with no input at all.

**What it was.** Colour 0 is *transparent* on this VDP, and the ST
background **is** colour 0 — so every sheet of paper in a text page is a
hole straight through plane A. Plane B's nametable was never
initialised: a nametable of zeroes means every one of its cells draws
**tile 0**, and tile 0 is the screen's top-left character cell. The
lower plane was therefore tiling the whole display with the page's first
letter and showing it through the paper. The GEM desktop hid this
completely — its background is a solid colour 2 dither, opaque
everywhere — which is why five hardware sessions never saw it.

Fix: wipe VRAM at init (the Sega CD BIOS leaves fonts and logo tiles
behind) and point plane B *and* the window plane at the blank tile, so
the backdrop — ST colour 0 — shows through as the paper it should be.

Two real defects were found and fixed alongside it, both of which would
have bitten later: the dirty-group list was capped at 512 entries per
25-line chunk while a fully-changed chunk needs 500 *groups* recorded
per-long, i.e. 1000, so heavy repaints silently starved the bottom half
of every band; and the window-plane registers (17/18) were only ever
initialised as a side effect of the on-screen keyboard's tile upload.

Method note for the next one of these: the harness's `--dump-wram` is
**word-byteswapped**. Reading it big-endian makes every 16-bit value and
every ST character-cell pair look transposed, which cost an hour of
chasing a nonexistent `cell_addr` bug. `E2OK` reading back as `2EKO` is
the tell.

## 7d. E4 closed — writes were being failed by a verify pass that did not exist

**Symptom:** B: mounted, listed, and read fine, but every write was
reported as failed — "Your output device is not receiving data" —
*while actually landing*. Folders survived a power cycle after the
desktop said the copy had failed; a dragged README.TXT appeared on B:
with a directory entry and no contents. Deleting gave "cannot delete
directory". A: was affected too, which is why Save Desktop never stuck.

**Cause:** `fverify` defaults on, so EmuTOS follows every floppy write
with `flopver()`. That function has a per-machine body, and the
fallback arm is `rc = EUNDEV`. There was no Sega CD arm, so:

    flopio()   -> 0      (the write really happened)
    flopver()  -> -15    (EUNDEV, "unknown device")

GEMDOS reported the write as a failure on a device that had just
serviced it. −15 is outside EmuDesk's error table, which is what turned
it into the "output device" alert rather than something legible.

**Fix:** a Sega CD arm for `flopver()` that does what the XBIOS contract
asks — read each written sector back and list any that will not read.

**How it was found:** `CONF_SEGACD_BTEST`, a boot-time diagnostic that
runs the same Fcreate/Fwrite/Fclose/Fopen/Fread/Dcreate/Dfree sequence
EmuDesk uses and prints every return code to the console. Dragging a
file between two desktop windows cannot be scripted reliably; this
needs no input at all. It now prints, on both drives:

    Fcreate = 6   Fwrite = 400   Fclose = 0   Fopen = 6   Fread = 400
    verified = 400 of 400        Dcreate = 0
    A: free 117 of 120           B: free 9 of 11

**Also fixed:** the `BP` (getbpb verdict) telemetry shared comm word
`$FF8026` with the cart request register and was never seeded, so an
untouched gate-array register read back as a plausible cluster count
(`0105`) and sent us looking for a geometry bug that was not there. It
now lives on `$FF8028` and is seeded with `B0FF` = "never asked".

## 7e. E5 — polish, not a CD driver

`D:` was cut deliberately. The CDBIOS is evicted (E-A1), so a live disc
volume means a from-scratch CDD + CDC driver, and the payoff is
catalogue rather than capability: the machine's software library is the
124 K `A:` ramdisk, baked at build time. Weighed against sound, speed
and a release, the driver lost. The CDD half remains the prerequisite
for CD-DA if that changes.

**Display: 2.6x faster repaints.** Profiling first: input is free, idle
is bound by the framebuffer copy, and under load the conversion
dominates. The conversion cost was VDP traffic, not arithmetic —
per-scanline conversion needed an address command for every row of
every tile, two thirds of the words on the bus. Converting a tile at a
time makes its 32 bytes one contiguous VRAM write behind a single
address command (32 words per tile -> 18), and indexing the tables by a
whole source byte replaces sixteen lookups per tile row with four.
Measured on both cores: **63.0 -> 23.8 frames per full screen**, so a
complete repaint drops from about a second to about four tenths.

The dirty set became a bitmap of tiles, which cannot overflow. Worth
recording: scanning that bitmap a bit at a time cost enough to push the
servant loop past a vblank, and the frame rate fell from 1.00
iterations/frame to *exactly* 0.50 — the signature of a loop that just
missed its deadline, not of a loop that got gradually slower.

**Where the remaining 23.8 frames go — measured, and not where I
guessed.** Three builds of the same benchmark (`DIAG_BENCH`, plus
`DIAG_BENCH_BLANK` for display-off and `DIAG_BENCH_NOVDP` for the
arithmetic with no VDP writes at all), identical on both cores:

| | frames/screen |
|---|---|
| display on | 23.8 |
| display blanked | 23.8 |
| no VDP writes at all | 22.0 |

So the VDP is **1.8 frames of 23.8 — about 7%** — and the conversion is
almost entirely CPU-bound. Blanking changes nothing, which means
neither core charges for VDP slot contention during active display;
real hardware does, so 7% is a floor for the VDP share, not an estimate
of it. I had reasoned the split was about half and half from
instruction timings, and that was simply wrong.

**The assembler converter (`iofw/convert.S`): 23.8 -> 18.4.** Table
bases pinned in a1-a4 for the whole tile, eight rows unrolled behind
fixed displacements so the source pointer never moves, one index
register and one accumulator (both call-clobbered, so only a2-a4 need
saving). Identical on both cores.

That is **1.29x, against the ~1.8x I predicted from instruction
timings** — the second time in this milestone that a cycle count from
the manual came out optimistic against the measurement, so treat such
estimates here as a reason to run the benchmark, not as a result.

Where the remaining 18.4 frames sit: ~2350 cycles per tile, ~294 per
tile row. The floor for this algorithm is **32 table lookups per tile**
— one per plane per row — and each costs a byte load, a zero-extend,
a doubling twice (the 68000 has no scaled indexing) and an indexed long
access, about 40 cycles however it is written. Beating it needs a
different algorithm, not better code: a bit-transpose in registers, or
wider table entries, neither obviously cheaper. Cumulative for the
milestone: **63.0 -> 18.4, 3.4x**.

**Sound: RF5C164 PCM, no BIOS.** Note the trigger problem: EmuTOS only
rings the bell on a BEL character reaching the VT52 driver, and nothing
in GEM, EmuDesk or EmuCON ever emits one (real TOS alerts are silent,
so beeping them would be a deviation, not a fix). Hence a power-on
chime in `segacd_sound_init()` — without it there is no way to find out
whether the machine can make a sound. Keyclick is the other path, from
`ikbd.c` where a scancode becomes a key event, so it fires for injected
OSK keys too.

First hardware report was silence from the OSK, so the status line now
carries `SND` and a `CLK` keyclick counter, packed into the spare bits
of the drive status comm word (no comm register was free). The chain
is verified rising under both cores.

Silence on hardware was the driver, twice over. I twice guessed at
audio routing instead -- the Sega CD returns its audio to the console
over the expansion connector, so a standard A/V lead has always carried
it and the CD unit's RCA jacks are a quality option, not a requirement.
Two rounds of speculation about cabling that a single read of the
vendored driver would have skipped.

**CTRL bits inverted.** Bit 7 is the chip's master enable; bit 6
chooses whether the low bits address a wave RAM bank or a channel. I
had them the other way round, so channel programming ran with the chip
switched off -- every register written to the right place, nothing
sounding, and nothing visible to say so. megadev settles it in two
lines: `pcm_clear_ram` walks CTRL through 0x80..0x8F for sixteen 4K
banks, and `CHANNEL(ch)` is `0xC0 | (ch-1)`; bit 7 set in both.

Worth recording how long this cost: the emulators reported the channel
address register as 0x0000 and not advancing, which was **true and
correct** -- the chip really was off. I dismissed it as unimplemented
reads and went looking elsewhere. After the fix the same probe reads
0x0800, exactly the loop point, meaning the channel ran the whole
one-shot. A diagnostic that disagrees with the theory is data, not
noise.

**Verified against MAME's `rf5c68.cpp`** rather than left on
inference from megadev's usage, after the driver went three rounds
without ever making a sound. Every field checks out: CTRL bit 7 is the
enable and bit 6 the bank/channel mode; the on/off register at 0x08 is
inverted, 0 enabling; ST is the start address shifted `8 + 11`, i.e.
the high byte of a 16-bit address with 11 fractional bits below it;
FD is added straight to that counter, so 0x0800 is exactly one sample
per tick and our 32-sample cycle at 32552 Hz is a 1017 Hz tone;
samples are sign-magnitude with bit 7 set for positive; and 0xFF is
the end marker, which loops to LS and **halts if LS also holds 0xFF**
-- which is precisely the self-terminating one-shot idiom we chose, so
that part was right by luck as much as judgement.

The read turned up two more bugs neither emulator nor console would
have made obvious:

- **A note cannot be retriggered by writing "on" twice.** MAME reloads
  the address counter from ST *only* on the disable transition:
  `if (!m_chan[i].enable) m_chan[i].addr = m_chan[i].start << (8+11);`
  Writing "on" to an already-on channel does nothing, so it carries on
  from wherever it was -- and once past the end marker, that is
  silence. Typing quickly would have given one tone and then nothing,
  because every keypress refreshed the countdown that would otherwise
  have switched the channel off. `pcm_beep` now writes off then on.
- **Channels were not silenced before the wave RAM load.** megadev's
  `pcm_clear_ram` disables all eight first and we did not, leaving
  whatever the BIOS had running to play through our sample as we wrote
  it. Now disabled first.

With those in, the emulated channel address finally *moves*:
`0000 017a 02f4`, advancing on both cores. Three rounds of shipping an
audio driver on the strength of "the code looks right"; the probe that
would have caught it on day one was sitting there reading 0x0000.

**Still silent on hardware after all of the above**, with every field
verified against MAME and the emulated channel demonstrably playing.
Rather than keep arguing about cabling from secondhand sources -- the
hookup guides say the CD unit's audio does not traverse the expansion
connector, while the JP BIOS boot music is *pure* RF5C164, so hearing
it would prove the opposite -- the status line now reports the chip's
own address counter as `PCM ab`. `a` is how far the counter ever got
while a note sounded, and it moves only if the chip is really playing.
`8` says the fault is downstream of every line of code we control;
`0` says it is not. Both cores read `83`.

**Hardware result: `PCM 40`, rising to `41` on a keypress.** Nonzero,
so the counter advances on real silicon and the chip really is
playing. (4 rather than 8 because the keyclick's two-frame countdown
switches the channel off around halfway through the 63 ms sample.) The
driver is correct end to end and the silence is entirely in the
analogue path -- consistent with the hookup guides, and with a US 1.10
BIOS whose boot music is FM *plus* PCM, so the audible startup jingle
was the YM2612 half all along.

**Confirmed audible** on headphones plugged into the CD unit's own
output: the keyclick is there. Sound is done and verified on silicon.

Which was the first thing I guessed, then abandoned under pushback,
then re-guessed, then abandoned again. The lesson is not that the
guess was right -- it would still have been silent, because the chip
was genuinely disabled by the inverted CTRL bit until three builds
ago. The lesson is that four rounds of argument moved nothing and one
instrument settled it in a single boot. Build the probe first.

**No settling time between register writes.** Found — `pcm_config_channel` pauses four
NOPs after **every** control-register write, because the chip drops
back-to-back writes on real silicon. We wrote nine of them in a row.
No emulator models the settling time, hence a path that verified
perfectly in emulation and a console that said nothing.

Third instance of the same hardware-only pattern in this port, after
the internal BRAM's bulk writes and the servant's ACK byte: **when
emulation is clean and silicon is not, suspect a write that needed
time**. Vendored reference drivers are worth reading before writing
our own, not after. Wave RAM and the register block sit on
the sub CPU's own bus, odd bytes only, like the internal BRAM — so the
eviction costs us nothing here. `bell_hook` and `kcl_hook` drive a
decaying triangle one-shot on channel 0. The sample terminates itself
(the end marker loops onto another end marker) rather than relying on
the VBL to switch the channel off, so it cannot drone if interrupts
ever stop. Verified in emulation only as far as the cores allow: wave
RAM reads back byte-exact, but both cores stub the channel address
registers, so **audibility is unverified until someone listens**.

**PAL: works, no code needed.** A region-E disc boots to the desktop on
an emulated European Mega CD at 49.7 Hz on both cores. The servant's
vblank wait is rate-agnostic and the 200 Hz system tick comes from the
gate-array timer, not from video, so GEMDOS timing is unaffected. The
only difference is a 50 Hz VBL: cursor blink and the bell's backstop
countdown are a sixth longer.

## 7f. B: had no way to be reformatted

Files on `B:` deleted fine; folders would not, with "cannot delete
directory". GEMDOS was innocent -- headlessly, `Ddelete` returns 0 on
an empty folder and -36 on a full one, and EmuDesk recurses into a
folder's contents before removing it, so both paths are correct.

The volume was damaged. Those folders date from the builds where
`flopver` failed every write that had in fact landed, so their
directory clusters never got their `.` and `..` entries and the
desktop cannot empty something it cannot enumerate.

The real bug is that there was **no way to recover**: `bram_probe()`
returned `BRAM_OURS` as soon as it recognised our own boot sector,
before it ever looked at the hold-C claim flag. So the documented
"hold C at power-on to reformat" only worked on a volume that was not
already ours -- exactly the case where you would never need it. The
claim flag is now checked first and reformats unconditionally.

Worth noting as a class of bug in its own right: every earlier storage
fault left debris, and for several builds there was no way to clear
it. A destructive recovery path is not a luxury on a filesystem this
small.

**The first FORMAT.PRG did not work on hardware**, and the reason is
worth more than the fix. It used logical sector I/O, which needs the
volume to be loggable -- and the only volume anyone ever formats is one
that is not. On a corrupt disk `Getbpb` fails, the media-change flag
never clears, and every logical access answers `E_CHNG` for ever: the
tool refused to run on exactly the disk it existed to repair. My
headless test formatted a *healthy* volume, so it tested the one case
that was never in question.

Switching to physical mode exposed a second, older bug underneath.
`flop_add_drive()` defaults a drive to a 3.5" floppy's 9 sectors per
track, and `getbpb` only corrects that once it has read a boot sector.
Our driver assumes 16. So with no valid BPB, `floppy_rw` split requests
on a boundary the driver does not use and every sector landed
somewhere else -- reads inside the volume returned "sector not found".
Sega CD drives now declare 16 sectors per track at detection, before
any BPB is read. The regression test corrupts `B:` first and then
formats it, because testing a repair tool on a healthy volume tests
nothing.

**And hold-C is a poor way to offer one** — you cannot see what you are
about to erase and you have to reboot to reach it. So `progs/format.c`
is now a real TOS program on the ramdisk: open `DISK A`, double-click
`FORMAT.PRG`, confirm. It probes the drive's capacity by looking for
its last readable sector, so it fits the internal BRAM or a cart
without being told which. Hold-C stays as the recovery of last resort.

Three things this cost, all worth recording:

- **libgcc cannot be linked into a `-mpcrel` program.** Its helpers
  call each other absolutely -- `__umodsi3` reaches `__udivsi3` with
  `jsr 39c.l` -- and with no relocation table that jumps into low
  memory the moment TOS loads the program anywhere but its link
  address. Symptom: an illegal instruction at a suspiciously *small*
  PC. `putnum` now does decimal by repeated subtraction, and
  `tools/mkprg.py` rejects any absolute `jsr`/`jmp` so this cannot
  ship quietly again.
- **The first raw access to a drive answers E_CHNG**, not data. A
  format utility has to call `Getbpb` and ask again; ours retries.
- **Writing logical sector 0 deliberately raises a media change**, so
  its return code says nothing about whether the bytes landed. The
  program reads the boot sector back and checks it instead.

## 7a. E4 storage findings

- **Emulated carts differ**: GPGX emulates a 512 K cart (id 6) but
  *reformats* `cart.brm` to Sega BRAM layout at load, wiping any
  filesystem seeded from the host; PicoDrive's libretro core reports no
  cart at all unless `POPT_EN_MCD_RAMCART` is set (the harness doesn't).
  So the servant **formats the cartridge itself** when it finds no
  EmuTOS signature — which is also what a real, blank cart needs.
- **The servant must service cart requests during EmuTOS's boot window**,
  before its own steady-state loop starts: GEMDOS mounts drives early
  and a timed-out first access marks the drive bad for the session.
- **Byte-lane bug worth remembering**: the servant wrote the completion
  ACK as a byte at an even comm address (the word's *high* byte) while
  the sub read the word's *low* byte. Every transfer therefore timed
  out after exactly one sector, which looked like "GEM refuses to mount
  B:" — the mount was fine, the multi-sector read was not. Comm-register
  byte lanes are now always word writes.
- Bounce buffer sits at sub `$7F000` (top of PRG bank 3, past the
  248-sector A: image); the servant reaches it at window offset
  `$1F000` with BK=3, bracketed by SBRQ like every other window access.

## 7g. E6 — the CD drive, staged

The CDBIOS is gone, so a live `D:` means driving the drive and the
decoder directly. That is the largest single piece of work in the
project, and the failure modes are all invisible from outside, so it
goes in stages that each end in something observable rather than in one
big bring-up.

**How the hardware talks.** The CDD exchanges fixed 10-nibble packets
with the host through the gate array: ten status nibbles at `$FF8038`,
ten command nibbles at `$FF8042`, each in the low four bits of its own
byte, the tenth of each being a checksum — the low nibble of `~(sum of
the other nine)`. Setting HOCK (`$FF8037` bit 2) makes the drive
publish its status every 1/75 s unprompted.

The CDC is a separate chip behind its own registers. `$FF8005` selects
one of its sixteen internal registers and `$FF8007` is that register's
data port, auto-incrementing the selection after every access; decoded
sector words are read from `$FF8008`. `$FF8004` is the gate array's own
byte rather than a CDC register: bit 7 is end-of-transfer, bit 6 is
data-ready, and the low three bits choose the transfer's destination,
of which 3 is "the sub CPU reads it".

That paragraph used to say something different and wrong -- that
`$FF8004` did the selecting and `$FF8006` the data -- because it was
inferred from an emulator's source rather than read. All of it now
comes from the console's own CDBIOS, with the derivation in
`docs/cdd-bios-notes.md`.

| Stage | Ends in | State |
|---|---|---|
| 1. CDD link | a well-formed status packet, checksum verified | **done**: `CDD Y 100` on GPGX, `CDD Y 110` on PicoDrive |
| 2. CDD commands | a command round-trip: the drive answers the *specific* question asked | **done on hardware**: `Y` on the console — two different sub-commands echoed back with valid checksums |
| 3. CDC capture | one known sector of our own ISO read into PRG-RAM and compared byte-for-byte | **done in emulation**: LBA 16 and LBA 1225 captured with no BIOS, both byte-exact against the image, both cores. Unconfirmed on hardware |
| 4. `D:` | a FAT image on the disc, mounted read-only through the same block layer as A: and B: | |
| 5. CD-DA | audio tracks play; the CDD half is already the prerequisite | |

### Working rules for stage 3 onward

Stage 2 cost the user twelve CD-Rs. The protocol was solved somewhere
around the fourth; the other eight went on defects I introduced while
integrating it, and on instruments of mine that lied. Two rules, set by
the person who burned the discs:

1. **Read the code before editing it.** Every one of the late failures
   came from applying a scripted string-replacement to `segacd.c`
   without opening it: a line that cleared the bus-interlock flag was
   deleted and never noticed for three builds; `.bss` offsets were read
   from a build that no longer existed and a bug reported that did not
   exist. Open the region, edit it, read it back. No blind patches.

2. **No hardware iteration on unverified changes.** A disc is for
   confirming something already established as far as it can be without
   the console — not for choosing between hypotheses. When a question
   has two possible answers, build the instrument that distinguishes
   them, or build a controlled test like `boot/cddtest.S`, which
   partitioned the whole problem in a single burn after eight had
   failed to.

A corollary worth keeping: **an instrument that cannot report its own
failure is worse than none.** A score written only by the interrupt it
watched, a counter that saturated and read as a heartbeat, a `q` the
font could not draw — each of them turned a working link into an
apparent failure and sent the next disc after nothing.

Stage 1 costs one control bit and proves the link before any command is
sent, which is exactly the kind of early instrument this port has
repeatedly wished it had.

Stage 2 sends command `0x02` sub-command `0x00`, "report the current
absolute position". Chosen because it moves nothing: a command
round-trip can be proven on real hardware without asking the drive to
do anything it might refuse. The reply carries the command echo, then
MM:SS:FF in BCD across status nibbles 2-7. GPGX answers 00:04:44 —
about LBA 194, which is where the drive would have been left after
loading `EMUTOS.IMG`, so the number is not merely well-formed but
right. The two cores disagree on the status nibble (`1` versus `C`),
which is emulator divergence rather than something we depend on.

Packet layout confirmed against GPGX's `cdd_process()`: the command
sub-command sits in nibble 3, and play/seek take MM:SS:FF in nibbles
2-7, tens in the high nibble of each pair.

**Hardware disagreed**, and usefully: polling alone checksummed clean,
but sending a command every VBL gave `CD N`. The drive publishes status
every 1/75 s and we were writing at 60 Hz, so sooner or later a command
lands in the middle of an exchange. Emulators do not model the race.
The boolean became a **score**: how many of the last sixteen packets
checksummed, `F` clean, `0` dead, anything between a race. A boolean
cannot tell a broken link from a flaky one, which is the whole question
here.

(The score itself shipped broken once: sixteen valid packets out of
sixteen does not fit in the nibble it is reported in, so a perfect link
displayed as `0`. Clamped.)

**Rate-limiting did not help**, and the reason it did not is the useful
part. The user watched the boot: the link starts at `CD F F ...` and
drops to `CD 0` the moment the first command goes out. One command is
enough, so the problem was never how often. Two guesses followed —
word-width register writes, then interrupt timing — and only research
separated them:

- Genesis Plus GX has no byte-write cases at all for `$FF8036`-`$FF804B`;
  the trigger register is commented *"CDD command 9 (controlled by BIOS,
  **word access only?**)"*. Commands are now five 16-bit writes.
- The decisive one: **the real BIOS writes CDD commands inside the CDD
  interrupt.** The gate array runs the exchange on its own clock, so
  the registers are a window onto a transfer already in progress, not a
  mailbox; a write at any other moment sends half of one packet and
  half of another. All CDD work moved into a level-4 handler
  (`VEC_LEVEL4`, mask bit `0x10` at `$FF8033`), 75 Hz: read status,
  checksum it, then post any queued command. Nothing else touches
  those registers.

**Proving a round-trip rather than assuming one.** A reported position
proves nothing, because the drive volunteers one whether or not anyone
asked. So the request now alternates between sub-command `0` (absolute
clock) and `1` (relative), and the reply names which it answered in
status nibble 1. Under both cores that nibble tracks the command
counter exactly — `5`→1, `6`→0, `7`→1. An answer that changes in step
with the question can only have come from the question.

**The escape hatch, and the bug in it.** Holding A at power-on keeps
the link passive, so a wrong guess costs a power cycle instead of a
disc. It silently did nothing at first: the servant published the
button to a comm register *after* `cart_probe()`, and the sub read it
before then. A comm register reads zero before anyone writes it, so
"not held" and "not asked yet" were the same value. Fixed twice over —
the servant samples the pad before anything slow, and stamps `0x5A` in
the high byte so the sub can tell an answer from silence and wait
(bounded) for one. Both ends now print what they concluded (`.C` /
`A.`), because this switch existing but not working is worse than it
not existing.

**Hardware round two: the instrument was lying.** The console reported
a perfect link score with a frozen status packet, and I read the score
and called stage 2 done. It was not: `CDP` sat at `1`, so exactly one
command had ever gone out, and everything from the sub had stopped. The
score only looked healthy because **it was only ever written by the CDD
interrupt** — when that interrupt stopped, the last good value stayed
on screen for ever. An instrument that goes quiet when the thing it
measures fails is worse than no instrument, because it reads as a pass.

Worse, I had also been reading the servant's frame counter as proof of
life. It runs on the *Genesis*, so it keeps climbing with the sub
dead, and the status line is drawn by the servant too: a completely
dead sub still produces a moving, healthy-looking display.

Three fixes, all of which are really the same fix — put each instrument
somewhere the failure it watches for cannot reach:

- The score is now watched from the VBL, outside the interrupt, and
  forced to `0` when the interrupt goes quiet for eight frames.
- The sub publishes its **own** heartbeat nibble beside the drive's, so
  the two together name which CPU stopped.
- A runaway level 4 masks itself off and latches a flag rather than
  hanging the console, because only the storming handler is in a
  position to notice — nothing below level 4 ever runs again.

That last one immediately caught itself out: set at sixteen arrivals
between VBLs (a fifth of a second) it tripped during boot, where EmuTOS
legitimately runs with interrupts off for longer, and switched the
drive off before a single command was sent. The two cases are nowhere
near each other — a storm passes a thousand in milliseconds, an honest
gap would need thirteen seconds — so the threshold is 1000. It cost
nothing because the numbers came out of emulation, which is the whole
argument for having the harness read telemetry as numbers rather than
me reading a bitmap font out of a screenshot. The servant now mirrors
the sub's telemetry words into the WRAM report block for exactly that.

**What the drive actually said**: `RS1 = 0xF`, the documented "invalid
/ not ready", to both clock queries — and it kept reporting a position
(LBA 47, right where the head would be after loading `EMUTOS.IMG`)
alongside the refusal, so it is not the "not ready" case that zeroes
nibbles 2-8. The single alternating query has become a **sweep of
seven**, including `0x00` get-status, which asks for nothing and cannot
be out of range, and `0x02`/`0x06`, which asks the drive to describe
its own last error. Both cores echo all seven exactly.

**Hardware round three: it was never being asked.** With honest
instruments the console reported both heartbeats spinning, the probe
sweep cycling, commands leaving — and `RS1 = 0xF`, "invalid", to all
seven questions including `0x00` get-status, which asks for nothing and
cannot be out of range. A drive that refuses even that is not refusing
the question. It never heard one.

The answer was in `vendor/megadev`'s register map, sitting in this
repository the whole time. `$FF8037` is not one bit:

| Bit | Name | Meaning |
|---|---|---|
| 0 | DTS | data transferring **to** the CDD; write 0 to abort |
| 1 | DRS | data transferring **from** the CDD; write 0 to abort |
| 2 | HOCK | "**starts** communication with the CDD" |

Two things follow, and both were wrong here:

- `*CDD_CTRL |= CDD_HOCK` is a read-modify-write on a register whose
  low two bits are the gate array's own in-flight transfer state and
  are documented write-0-only. It reads whatever the hardware is busy
  doing and hands it straight back. It is now a plain word write.
- HOCK reads as a **strobe**, not a level: it starts a communication.
  Set once at boot it is enough for the drive's unprompted 75 Hz
  reports, but a command may need its own start. It is now pulsed
  after every command.

The claim is testable rather than argued: DTS is sampled and latched,
so the disc reports whether a command was *ever* transferred at all,
as distinct from written into registers. Both emulators hard-wire DTS
and DRS to zero — Genesis Plus GX's word handler for `$FF8036` forces
bits 1:0 — so this is a hardware-only question, and the driver never
blocks on the bits for more than eight interrupts in case silicon
holds one high.

**And the measurement had a hole in it.** Hardware answered `8`/`A`:
status arriving, none going out, ever. But the register was sampled at
the *top* of the interrupt -- before the write meant to start a
transfer, a whole packet period after the last one -- so a bit that
rises and falls between interrupts was never observable. "Never seen"
meant nothing. It is now polled immediately after the write.

The HOCK "pulse" was also not one: the register already held the bit,
and writing a set bit again produces no edge. A real strobe needs it
taken low first, which also writes 0 to the two abort bits, so it is
only safe when neither transfer is running -- and it sits behind
holding B, because clearing the bit that started the link is the one
action here that could end it. One disc, two boots, two experiments.

Worth naming the pattern: four rounds running, the failure was that
something I believed was happening was not being checked. The command
was assumed sent; the score was assumed live; the button was assumed
read; the pulse was assumed to be a pulse. Each time the fix was not
cleverness but an observation -- and twice the thing that needed
fixing first was the instrument, not the driver.

**Then the user asked the right question: why guess at what the BIOS
does instead of reading it?**

Because I had not thought of it, and it ended the search in one pass.
The sub-CPU CDBIOS is compressed inside the BIOS ROM -- the header
string is visibly garbled in the dump -- so there is nothing to
disassemble there. But the boot process decompresses it into PRG-RAM
`$0000-$5FFF`, which is *exactly* the region the E-A1 audit payload
already snapshots into work RAM for `--dump-wram`. The machinery to
read Sega's own CDD driver had been sitting in this repository since
the eviction audit.

It uses absolute-short addressing (`$8042.w` sign-extends to `$FF8042`
on a 24-bit bus), which is why searching for absolute-long references
found nothing. The routine:

```
btst   #1,$FF8037     wait for DRS: the status packet is ready
lea    $FF8038,a1     read ten status bytes  (long, long, word)
lea    $FF8042,a1     write ten command bytes (long, long, word)
move.b #4,$FF8037     hand it over and re-arm
```

**There is no trigger among the command registers.** Four discs went
looking for one. The exchange is handed over by writing the control
register back to HOCK-only, which clears DTS and DRS -- and this
driver never wrote that register again after boot, so the gate array
waited for an acknowledgement that never came. No command was ever
carried; the drive reported its own status and answered "invalid" to
questions it had never been asked. Every symptom, exactly.

The wait was backwards as well: DRS set means the packet has *arrived*,
not that the bus is busy, so the driver was declining to send in
precisely the window the BIOS sends in.

That fixed the missing acknowledgement but not the drive, because
reading two routines is not the same as reading the code that calls
them. The caller (`$1334`) holds the rest:

```
bsr  $1390     wait DRS low, read ten status bytes, ack with #4
...            sum all ten, invert, mask: must be zero
bsr  $17B6     build the next command
bsr  $13FE     write ten command bytes
```

Three more corrections came out of it, one of them mine from the same
hour:

- **The wait is for DRS to go low.** `dbeq` exits when its condition is
  true and `btst` sets Z when the bit is *zero*, so the BIOS spins
  until the incoming packet has finished. I had just "corrected" the
  driver to wait for the opposite, on a glance rather than a reading.
- **The acknowledgement precedes the command**, not follows it.
- **A command goes out on every exchange** -- 75 a second, a NOP when
  there is nothing to ask. This driver sent one twice a second and let
  seventy-odd exchanges pass untouched.

The checksum convention is confirmed by the same code: sum all ten
status bytes, invert, mask to a nibble, require zero -- our test
written the other way round.

The lesson is not subtle: **the authoritative implementation was on the
user's own console the whole time, and four discs were spent not
reading it.** The follow-on lesson is that reading it in fragments
reproduced the same failure in miniature.

The six-way search below was written just before that and is now
retired, but the reasoning behind it stands and its judge was kept:

**Stop guessing one disc at a time.** Four discs bought four
refuted hypotheses, which is a bad rate when each one costs the user a
CD-R and a boot. The search moves into the firmware: six ways of
sending a command (nibbles as five words or ten bytes, crossed with
nothing / set HOCK / edge HOCK), eight attempts each, and **the drive
is the judge** -- a strategy counts only when the reply names the
sub-command it was asked for, twice, for two different sub-commands,
since one match could be a value already in the register. The winner
latches and the seven-question sweep resumes on top of it.

The negative result is worth as much as the positive one here: six
failures rule out the whole family of "the trigger is a write we are
doing in the wrong order or the wrong width", which is every theory
tried so far.

Tested by forcing the answer detection off, so the search demonstrably
cycles 0-5 and wraps on both cores without the score dropping or the
machine hanging -- including the two strategies that take HOCK low,
the only ones that could plausibly end a working link. Shipping an
untested search loop would have been the same mistake one level up.

**Two process notes**, both of which cost real time here:

- `tools/build-iso.sh` consumes a prebuilt `emutos-segacd.img`; it does
  not build EmuTOS. Every sub-side change needs
  `cd emutos && make segacd ELF=1` first. A stale image looked exactly
  like a logic bug — telemetry that could not be produced by the source
  in front of me — and I chased it as one.
- The top nibble of the sound telemetry word used to watch the PCM
  chip's address counter. That question is settled, so it counts CD
  commands now. Retiring a spent instrument beats adding a field.

### Stage 3 — the CDC, and reading the whole thing first

Stage 3 was done in the order stage 2 was not: the entire path was read
out of the console's own CDBIOS before a line of driver was written, and
the result went green on both cores at the first attempt, with no disc
burned.

What made the reading tractable was the **dispatch table**. `CDBIOS` is
`$2E34`; it splits on bit 7 of the function number and sends `$80` and
up through twenty-five `bra.w` entries at `$2E60`. Those are the
documented API in function-number order, and two of them verify
themselves against their own code -- `$80` is nothing but "is the drive
busy" and `$81` fills the status block. Naming them turned the
decompiler's output from anonymous `FUN_0000xxxx` into something that
could be checked, and `CDCSTART`, `CDCSTOP`, `CDCSTAT`, `CDCREAD`,
`CDCTRN` and `CDCACK` fell out at once. `tools/cdc-seed.py` does the
seeding, `tools/cdc-analyse.py` finds every function that touches a
gate-array register, `tools/cdd-msf.py` decompiles the ones the shift
chains made unreadable by hand.

The one thing that could not have been guessed is which CDD command
starts a data read. It is not in the CDC code at all: `$F70` is the
BIOS's single "ask the drive to do something" entry, it splits the low
byte of a request code into command and sub-command nibbles, and
`ROMREAD` sets that request code to `$30`. **Command 3, sub-command 0,
target MSF in nibbles 2 through 7.**

The driver differs from Sega's in four places, each deliberate and each
written down in `docs/cdd-bios-notes.md`: no sector ring, the 2 KB
transfer in the VBL rather than the interrupt, level 4 masking level 5
(the drive exchange is the thing with a deadline), and two target
sectors instead of one.

**Two sectors, because one proves less than it looks like it does.**
LBA 16 is the ISO 9660 volume descriptor -- recognisable, and therefore
also the likeliest thing to be lying in a buffer already. LBA 1225 is
over a thousand sectors further in, needs a real seek, and is the
result worth quoting. Both are byte-exact against the image the disc
was built from, on Genesis Plus GX and on PicoDrive, and the CDD link
still scores `F` after seven thousand frames.

The check is `tools/check-cdread.py`: the sub hashes each captured
sector, the servant copies the report and the last sector into work
RAM, the harness dumps work RAM, and the tool diffs it against the ISO.
"Did we read sector N" is a byte comparison. That is the whole
difference between this stage and the last one.

**Where the telemetry went.** All eight sub-to-main comm registers were
already spoken for, and the block layer owns the two that looked free
-- writing to them would have broken the storage status line to report
on the disc. So the CD report lives in PRG-RAM at `$7F7E0`, sixteen
words below the captured sector, and one spare bit of `$FF8028` tells
the servant there is something worth a bank switch to fetch.

Unverified on hardware, all of it. Emulators model the CDC's registers
and its buffer, but neither models the transfer window that cost stage
2 eight discs, and neither charges for the pacing `nop`s that are in
the burst loop precisely because Sega put them there.

## 8a. E2 hardware truths (each found by bisection, each fatal alone)

1. **The CDBIOS write-protects low PRG-RAM** (WP bits in `$A12002`,
   main-side only). EmuTOS's first vector write silently no-ops and the
   next exception vectors into the half-dead BIOS. IOFW clears WP before
   commanding the boot. (E-A1 addendum: eviction requires this.)
2. **The 68000 `RESET` instruction is forbidden on the sub** — it resets
   the gate array; Word RAM assignment reverts mid-instruction.
   `CONF_WITH_RESET 0`.
3. **The Atari cartridge probe** (`0xFA0000`) bus-errors on the sub bus.
   `CONF_WITH_CARTRIDGE 0`.
4. **`STOP`-instruction idling starves SBRQ grants** under both emulator
   cores. `USE_STOP_INSN_TO_FREE_HOST_CPU 0` (like MACHINE_LISA). Real
   hardware may be more forgiving — recheck at the hardware gate.
5. **Gate-array interrupt sources survive the eviction enabled** (INT2,
   CDD, CDC). Mask all at `_main`; enable per-level with its handler.

## 8. Audit E-A1 — RESOLVED: evict the CDBIOS after boot

Measured on the user's own BIOS image (JP MCD1 1.00) — see
`docs/E-A1-AUDIT.md`. The BIOS's live variables are compact ($5800–$5FFF),
but its *code* occupies low PRG-RAM including `$400–$5FF` (jump-table
dispatch stubs at $206/$20C/$20E/$5E8/$5FA), exactly where ST sysvars must
live. Sharing is impossible; the ladder resolves to **evict**:

- Boot loader (under the BIOS) stages EmuTOS + the `A:` ramdisk image in
  one pass, then EmuTOS takes the whole sub CPU — vectors, `$400+`, all of
  it. 24 KB of former BIOS space returns to ST-RAM.
- No live CD between takeover and the native CDD/CDC driver (E5). Interim
  drives (`A:` ramdisk, `C:` backup cart via servant) are BIOS-free paths.

## 9. Deliverables & source layout

```
AROS-md/                      (rename candidate: emutos-md)
├── emutos/        # submodule; port lands upstream-style as bios/segacd.*
│                  #   + a target in the Makefile (goal: upstreamable)
├── iofw/          # main-CPU servant (IP): VDP pump, input, cart proxy
├── boot/          # SP boot loader (CDBIOS era): loads OS + A: image
├── tools/         # ISO builder + region security blocks; FAT image tool
└── SGDK/          # submodule, reference only (joy.c, VDP init idioms)
```

AROS submodule: retired, removal on request. GEOS-genesis: untouched,
separate project. EmuTOS is GPL-2.0 — the whole port stays open, and a
clean `bios/segacd.c` machine layer is a credible upstream contribution
(precedent: Amiga, Lisa, ColdFire ports in-tree).

## 10. Milestones

| # | Deliverable | Proves |
|---|---|---|
| E0 | Boot skeleton on emu + HW: IP/SP up, comm echo, **E-A1 measured** (dump BIOS `$400–$5FFF` usage post-boot), EmuTOS cross-builds with a stub `segacd` target | toolchain, boot chain, the one unknown |
| E1 | IOFW v1: planar→tile pipeline **benchmarked**, window→VRAM DMA with erratum workaround, mouse/pad→comm→INT2, sprite pointer | the whole transport layer |
| E2 | ~~EmuCON on screen~~ **DONE — exceeded: the full GEM desktop** (EmuDesk: menu bar, mouse pointer, Trash/Printer icons) renders on both emulator cores. EmuTOS boots on the sub CPU with the ST ABI byte-authentic | the OS lives |
| E3 | ~~GEM desktop + mouse~~ **DONE (core)**: pointer moves, menus drop ("Desktop info…"), verified on both cores via pad-as-mouse; GEM state self-heals after packet floods. Sega Mouse driver ported from SGDK (US/JP polarity switch) — **hardware validation on the user's mouse pending**. On-screen keyboard **DONE (E3.5)** | the headline |

**E3.5 — on-screen keyboard (VDP Window plane):** a centred, staggered
QWERTY overlay on the hardware Window plane (rows 18-27), toggled by
Start, navigated by the d-pad, A presses a key. Real 8x8 glyphs from a
generated font (`tools/mkfont.py`); Atari scancodes ride GA_KEY to the
sub, which injects them with `call_ikbdraw` + a caps-lock Shift latch.
Verified end to end on GPGX: key '1' -> scancode 0x02, d-pad down x2 ->
'A' -> 0x1E, with the selection highlight tracking. Two VDP bugs found
and fixed en route: tile data must reach the data port as 16-bit words
(byte writes garble every glyph), and the Window plane must be forced
off at init. Remaining: GUI-dialog typing pass and real-hardware keys.
| E4 | **DONE (closed)** — writes work on both drives: `flopver()` had no Sega CD arm and was failing every successful write with EUNDEV (§7d). Full Fcreate/Fwrite/Fread round trip verified byte-exact on A: and B: headlessly. **A: DONE** — FAT12 ramdisk from CD in PRG bank 3, drive icon opens, README.TXT listed. **B: proxy built**: cart probe (official `$400001`/`$600001`/`$700001` protocol), format-on-demand, sector bounce buffer, GEMDOS reads the BPB across CPUs correctly (verified byte-exact). **B: DONE** — `B:\*.*` opens on the desktop; GEMDOS reads its 7-sector BPB/FAT/root set through the proxy, and a servant write/read-back self-test passes byte-exact (0 mismatches over 256 bytes) | daily-usable system |
| E4.5 | **DONE** — the hardware text smear: plane B tiling the display with tile 0 through the transparent ST background (§7c). Also fixed: the dirty-group cap that starved heavy repaints, and uninitialised window-plane registers. Verified on both cores with a new headless console-soak build | text output, i.e. half of what a TOS is for |
| E5 | **DONE, all of it confirmed on hardware (7e)** — display, storage, speed, PAL and sound. Scope changed: `D:` deliberately cut — the payoff is catalogue, not capability, and it costs a from-scratch CDD+CDC driver. Delivered instead: **3.4x faster repaints** (63.0 -> 18.4 frames/screen, measured on both cores), **PCM bell and keyclick** with no BIOS, **PAL verified** at 49.7 Hz, tri-region discs | a system worth using |
| E6 | **In progress (7g)**: staged CD bring-up. Stages 1-2 done on hardware — the CDD link works with no BIOS and answers the specific question asked. Stage 3 done in emulation — sectors read off the disc through the CDC, byte-exact. Stages 4-5: `D:`, CD-DA | the disc becomes a disk |

## 11. Risks

| Risk | Sev | Mitigation |
|---|---|---|
| E-A1 lands on "evict" and v1 has no live CD drive | LOW | Ladder step 2 is fully workable (A: preload + C: cart); step 3 restores D: later |
| Planar→tile conversion too slow for GEM feel | MED | 12.5 MHz + dirty rects + 32 K worst case; measured at E1 before dependencies form |
| ST app compatibility expectations | — | Scope honestly: **clean GEM apps** (trap calls only). Anything poking Shifter/IKBD hardware was never going to run; low-memory ABI *is* authentic here, which is more than a main-CPU port could say |
| EmuTOS build/internals learning curve | LOW | Active upstream, good docs, three in-tree machine ports to crib from |
| Word-RAM/PRG DMA erratum, ring races | LOW | Same discipline as every prior rev; single-owner rules, vblank-only handoffs — and Word RAM never changes hands at all |

## 12. Open questions

1. Console + Sega CD models/region for the E0 hardware loop (NTSC assumed).
2. Backup cart brand — confirm 512 K and WP behavior on real hardware at E4.
3. Mouse variant on your desk (US 3-button vs JP/EU 2-button) for E1.
4. Rename this repo (`AROS-md` → `emutos-md`?) now that the OS changed — cosmetic, your call.
