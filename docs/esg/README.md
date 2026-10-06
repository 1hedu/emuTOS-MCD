# ESG — the EmuTOS Subsystem for GEOS

GEOS-Genesis (`1hedu/GEOS-genesis`) as a subsystem of EmuTOS, the way
Linux is one of Windows under WSL. EmuTOS is the host; GEOS is the
guest. It lives here, in this repository.

There are two shapes, and both get built, cartridge first:

* **Cartridge: WSL2.** GEOS runs as itself, a second kernel on its own
  CPU: GEOS-Genesis, unported, on the Genesis main 68000, while EmuTOS
  runs on the Mega CD's. Its screen is shown in a scrollable EmuTOS
  window — WSLg — composited by the VDP, not copied.
* **Disc: WSL1**, afterwards. The GEOS kernal assembled as a TOS
  program on the sub 68000, its calls answered through GEMDOS, AES and
  VDI. On a disc boot there is no cartridge ROM for GEOS to run from,
  so this is the only shape a disc can have.

## Cartridge (WSL2)

| WSL2 | ESG, cartridge |
|---|---|
| the Linux kernel in its VM | GEOS-Genesis on the Genesis 68000, with its own VDP presenter, pads, sound and sprites |
| the Windows host | EmuTOS on the Mega CD 68000 |
| WSLg | a GEM window whose work area is VDP tiles of GEOS's screen, on a C64 palette line; the sliders scroll a view of the 320×200 screen |
| `/mnt/c`, `\\wsl$` | file exchange through the existing sector proxy, as S: is served today |

**The constraint** is the Genesis's 64 KB of work RAM: GEOS uses all of
it as the C64's memory, and iofw uses all of it too (code, the 32 KB
planar cache, `tab8`). So iofw becomes almost RAM-free:

* its code runs from cartridge ROM;
* the change detection and planar-to-tile conversion move to the sub
  CPU, into the 64 KB bulk arena at `$6C000` that the 48 KB C: now
  leaves (32 KB previous frame, 32 KB converted tiles); the main CPU
  only copies finished tiles to VRAM;
* the few hundred bytes of state left go in Z80 RAM or GEOS's 2 KB
  stack band.

**The window.** VRAM has room for about 680 GEOS tiles beside the ST
screen's 1000 — a view of about 256×160, which is what the scrollable
window shows. EmuTOS tells the Genesis side where the work area is and
which part of GEOS's screen it views; plane A's cells there point at
GEOS's tiles with the C64 palette line. Scrolling re-presents only the
cells that come into view.

**Input.** The mouse is on the Genesis, so the main side routes it.
Ordinary movement goes to EmuTOS as today and never to GEOS. A press
inside the window goes to GEOS, in GEOS coordinates, and so does the
position while the button is held, until the release: clicks and drags
only. So GEOS menus that close when the pointer leaves them close on
the next click instead, and the pointer is always EmuTOS's. The
keyboard goes to whichever window has focus.

**Costs.** Cartridge only; GEOS, EmuTOS, its images and the romdisk in
one ROM of at most 4 MB (GEOS's disk images alone are ~1.8 MB); the
Genesis CPU runs GEOS and the ST screen's tile copies both; one GEOS
application at a time, because GEOS is single-tasking.

**First measurement:** whether sub-side conversion plus main-side
copying under SBRQ keeps up with the screen.

## Disc (WSL1), afterwards

GEOS-Genesis running inside EmuTOS the way Linux runs inside Windows
under WSL1: no second machine, no second kernel image on its own
hardware. The GEOS kernal is assembled as a TOS program on the Mega CD
sub 68000, and its calls are answered through GEMDOS, AES and VDI. The
Genesis main CPU stays iofw's.

| WSL | ESG |
|---|---|
| lxcore.sys | GEOSCORE: `src/kernal/` reassembled, `src/genesis/` replaced by a TOS layer |
| Linux syscalls | the native API gate, fixed at `$020000` |
| a Linux process | a GEOS instance: one 64 KB C64 image, kernal code shared, switched by a5/a6/SP |
| bash | C64 BASIC (`src/kernal/basic/`) in a GEM window — `LOAD"$",8` / `LIST` is `ls` |
| WSLg | each GEOS application in its own scrollable GEM window, C64 colours through a VDP palette line |
| `/mnt/c` | a GEOS drive that is a GEMDOS folder |
| `\\wsl$` | `G:`, a FAT view of `.D64`/`.D81` images |
| `wsl.exe` | `GEOSRUN.TTP`, asking `GEOS.ACC` to open an application |

Phases:

1. Headless console: `GEOS.PRG`, BASIC in a window, `LOAD"$",8` over a GEMDOS folder.
2. One GUI application (geoWrite) in a scrolling window.
3. Colour: the `C64!` block and a freed palette line.
4. Two instances.
5. `GEOS.ACC` and `GEOSRUN.TTP`.
6. `G:`, writes both ways, file names, clipboard.

Phase 0, below, was done for this shape; most of it carries over.

## Phase 0 results

Three studies, each with its own report:

* `phase0/geos-kernel.md` — the GEOS-Genesis kernal and applications:
  size, every hardware touch, every EXT slot, position independence,
  the interrupt model, BASIC. Built with the distribution's
  `m68k-linux-gnu` binutils from an unmodified checkout.
  `phase0/tools/` are the two disassembly scanners it used;
  `pic_table.md` and `ext_users.txt` are their output.
* `phase0/emutos-memory.md` — the sub CPU's memory map under this
  EmuTOS, upstream `a084e52d` plus patches 0001–0190, built and mapped.
* `phase0/iofw-palette.md` — iofw's work-RAM budget, measured, and the
  `C64!` palette feature, prototyped (`phase0/iofw-c64-prototype.diff`,
  applies to `iofw/main.c`; not built into anything shipped).

### Verdict: feasible, with five things to settle first

**1. The gate at `$020000` can be kept, but not for free.** Nothing
fixed lives there: it is ordinary TPA (re-measured on the dev branch:
`$15834`–`$55FFF`, 264,140 bytes). Claiming it — Malloc a pad up to
`$20000`, Malloc `$1000`, which then lands there, free the pad — costs
4 KB of memory but splits the TPA in two, 42,956 bytes below and
217,088 above, and Pexec takes the largest block: the biggest program
drops from ~264 KB to ~212 KB (NEOchrome needs 204 KB). So the claim
happens only when GEOS is wanted — `GEOS.PRG` making it itself if it
was loaded below `$20000`, or an `AUTO` program on a GEOS disc — not in
every boot. Raising `membot` instead would cost ~46 KB; don't.

**2. Most application binaries run unmodified; seven do not.**
Applications are base-relative throughout — data `label(a5)`, GEOS
variables `(x-$8000)(a6)`, code `bsr`/`(pc)`. geoWrite has zero
absolute references to its own addresses. The absolute operands are
the gate calls (fine, given 1) and:

* 23 references to the data blobs at `$21000`–`$28800` in geoPaint,
  geoWrite, geoSpell, geoProgrammer, PaintView and TopDesk, plus
  PaintView's blob at `$218000`, past the end of the sub CPU's 512 KB.
  Reserving `$20000`–`$28800` would cost 34 KB; rebuilding these six to
  reach their blobs through a pointer is the better trade.
* TEXT GRABBER (a `.long` handler table and `tg_unwind` jumping to
  `$00000xxx`) and Preferences (three absolute `lea` into cartridge
  ROM). The audit reads these as wrong on the Genesis as well; that is
  from the disassembly, not confirmed on hardware.

The kernal's own 35 numeric `jsr $200xx` are harmless — it is rebuilt.

**3. The kernal has no RAM outside the 64 KB image,** so instances can
share one code copy. Its hardware use is small: 7 sites (one VDP
register write in `prefs.S`, six drive-B SRAM accesses), 27 absolute RAM
references (22 in the debugger), and 20 privileged instructions — 7
`stop #$2000`, 11 SR writes, 2 `rte`. Of 89 EXT slots, 61 are portable,
26 need a TOS implementation, and 2 are Genesis-only (Sonic, the
non-GEOS video restore).

**4. Scheduling is a coroutine, not a callback.** `geos_runtime_step`
is already one main-loop iteration, but DoDlgBox, move-frame, BASIC and
desk accessories wait in loops of their own, and several applications
spin on the frame counter. So each instance runs on its own stack
(inside its image), `stop` becomes a yield back to the GEM event loop,
and a VBL-queue routine loads the instance's a5/a6 and runs the
`_InterruptMain` half. EmuTOS's VBL is iofw's 60 Hz INT2, but it is
lost while the CDBIOS is visited (interrupts masked up to ~5 s), so
GEOS must tolerate gaps.

**5. Memory is the hard limit, and it is tighter than the plan said.**
The TPA is about 264 KB (`$15820`–`$55FFF`), not 384: EmuTOS's own BSS
runs to `$14FD0`, the screen sits at `$58000`.

| | KB |
|---|---|
| TPA | 264 |
| gate reservation | −4 |
| GEOSCORE, lean (debugger an overlay, ROM mirrors read from files) | −122 |
| left for instances and GEM | 138 |
| one instance (64 KB image, + 8 KB scratch only for geoWrite/geoPaint) | 64–72 |

One instance fits with room for the desktop; two (console + one
application, or two applications) do not. The lever is the C: ramdisk
above `phystop` (112 KB, `$60000`–`$7C000`): giving 64 KB of it back to
ST-RAM leaves ~200 KB after GEOSCORE — two instances and ~55 KB for GEM.
Moving the screen low to win a few KB was tried in the analysis and
runs into an unexplained Mode 1 write past the top of memory; not
recommended.

### iofw

Measured on the dev branch with the project's flags: 24,508 of 24,576
bytes used, **68 free**. The `C64!` handler prototyped here
(`phase0/iofw-c64-prototype.diff`, regenerated for this tree) costs
~724 bytes, so the build is 656 over until space is recovered:

| | bytes |
|---|---|
| telemetry stores out of a release build | ~+1,014 |
| `cdd_watch`, boot-trace ring, CD sector capture, frame-900 probe | ~+700 (measured on `main`, code unchanged) |
| whole-tree `-Os` (much of the cold code already is) | ~+2,600 |
| `tab8` (4 KB) moved above the planar cache | only if split in four or the raster event lists halved |

The raster palette now holds `$FFED00`–`$FFF53F` and the line
interrupt, and the stack reaches about `$FFF940`, so the 3.3 KB that
was free above the cache on `main` is down to about 1 KB.

Palette lines: the raster palette rewrites only line 0 and line 1
entry 2 mid-frame, and the cursor already moved to line 3 entries 4–5.
What is left to free line 2 for the C64 is moving the ordinary keyboard
keys to line 1 (`osk.c`, `0x4000` → `0x2000`). Plane A's screen
nametable is written only by `screen_scroll_apply`, so the palette bit
goes in there, keyed by screen cell, and survives both the scroll and
blit rings; its VDP writes must run with interrupts off, as that
function's now do, because the raster handler moves the VDP address.
The block goes at offset 32256 — `RST!` holds 32240. C64 colour 0 is
transparent in a tile, so cells under a GEOS window get an opaque black
plane-B tile. The tile grid is aligned to ST coordinates; the −12-line
letterbox is display-only.

### Found along the way

* **iofw:** the selected on-screen-keyboard key and the cursor shared
  line 3 entry 1 on `main`. Already fixed on the dev branch this is
  now based on.
* **EmuTOS, latent:** `bios/segacd.c` puts the timeshare read buffer at
  `$7D000` with `CD_TS_SECTORS` 4 — four 2048-byte sectors, 8 KB, to
  `$7F000` — and the BRAM work area at `$7E000`. The guard meant to
  catch exactly that multiplies by 512, so it can never fire. The two
  do overlap, but nothing shipped is hurt by it: the work area is only
  live between a `BRMINIT` and the end of the same BRAM call sequence,
  every sequence in the driver (`bfs_init`, called first by `bfs_find`
  and `bfs_create`) starts with its own `BRMINIT`, data access after it
  is through the pointer `BRMSERCH` returns into backup RAM itself, and
  no CD read can run in the middle of a sequence. The one way to reach
  it is a program using the `SCD_BRAM_CALL` pass-through that calls
  `BRM_INIT`, then reads from `D:`, then makes another BRAM call
  without re-initialising — `progs/bramrw.c` does not. Not run in an
  emulator: the timeshare exists only on a disc boot, which needs the
  Sega CD BIOS images, and none are in this environment.
* **iofw:** the CD trace mirror at `$FFEE00` (`hw.h:149`) is defined and
  never used.

None of these are fixed here.

### Decided

1. The cartridge (WSL2) first, the disc (WSL1) after.
2. C: is 48 KB by default and carries only `EMUICON.RSC` and
   `EMUDESK.INF`; the programs and README are on D: and R:. That frees
   64 KB at `$6C000` — the sub-side screen buffers for the cartridge,
   a second GEOS instance for the disc.
3. ESG lives in this repository.
4. For the disc: the gate at `$20000` is claimed by `GEOS.PRG` itself,
   at no cost to a boot that never runs GEOS.
