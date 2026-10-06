# ESC64 — the EmuTOS Subsystem for Commodore 64

GEOS-Genesis (`1hedu/GEOS-genesis`) running inside EmuTOS the way Linux
runs inside Windows under WSL1: no second machine, no second kernel
image on its own hardware. The GEOS kernal is assembled as a TOS
program on the Mega CD sub 68000, and its calls are answered through
GEMDOS, AES and VDI. The Genesis main CPU stays iofw's.

| WSL | ESC64 |
|---|---|
| lxcore.sys | GEOSCORE: `src/kernal/` reassembled, `src/genesis/` replaced by a TOS layer |
| Linux syscalls | the native API gate, fixed at `$020000` |
| a Linux process | a GEOS instance: one 64 KB C64 image, kernal code shared, switched by a5/a6/SP |
| bash | C64 BASIC (`src/kernal/basic/`) in a GEM window — `LOAD"$",8` / `LIST` is `ls` |
| WSLg | each GEOS application in its own scrollable GEM window, C64 colours through a VDP palette line |
| `/mnt/c` | a GEOS drive that is a GEMDOS folder |
| `\\wsl$` | `G:`, a FAT view of `.D64`/`.D81` images |
| `wsl.exe` | `GEOSRUN.TTP`, asking `GEOS.ACC` to open an application |

## Phases

0. Feasibility — this directory, `phase0/`. Done.
1. Headless console: `GEOS.PRG`, BASIC in a window, `LOAD"$",8` over a GEMDOS folder.
2. One GUI application (geoWrite) in a scrolling window.
3. Colour: the `C64!` block and a freed palette line.
4. Two instances.
5. `GEOS.ACC` and `GEOSRUN.TTP`.
6. `G:`, writes both ways, file names, clipboard.

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

**1. The gate at `$020000` can be kept.** Nothing fixed lives there:
it is ordinary TPA. The recommended reservation is a few lines in
`bios/bios.c` before `autoexec()` — Malloc a pad up to `$20000`,
Malloc `$1000` (which then lands at `$20000`), free the pad — owned by
the initial process, costing exactly 4 KB. A resident `AUTO` program
doing the same is the fallback if EmuTOS should not change. Raising
`membot` instead would cost ~46 KB; don't.

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

Measured with the project's flags: 24,436 of 24,576 bytes used, **140
free**. The `C64!` handler prototyped here costs ~690 bytes (350–450
tightened). Space to recover, measured:

| | bytes |
|---|---|
| `tab8` (4 KB conversion table) moved to fixed WRAM above the planar cache, which ends at `$FFED00`; `$FFED00`–`$FFF9FF` is unused | +4,096 |
| telemetry stores out of a release build | +834 |
| `cdd_watch`, boot-trace ring, CD sector capture, frame-900 probe | +712 |

Freeing a palette line: move the ordinary keyboard keys to line 1
(`osk.c:200,215,236`, `0x4000` → `0x2000`) and the cursor to line 3
entries 4–5 (one line in EmuTOS's `sprite_build`). Line 2 becomes the
C64's. Plane A's screen nametable is written only by
`screen_scroll_apply` (`main.c:419`), so the palette bit goes in there,
keyed by screen cell, and survives both the scroll and blit rings.
C64 colour 0 is transparent in a tile, so cells under a GEOS window get
an opaque black plane-B tile. The tile grid is aligned to ST
coordinates; the −12-line letterbox is display-only.

### Found along the way

* **iofw:** the selected on-screen-keyboard key and the cursor share
  line 3 entry 1. After the first `CUR!`, the selected key's ink is the
  pointer's border colour, white by default, on a light grey face. The
  palette move above fixes it.
* **EmuTOS:** `bios/segacd.c` puts the timeshare read buffer at
  `$7D000` with `CD_TS_SECTORS` 4 — four 2048-byte sectors, 8 KB, to
  `$7F000` — and the BRAM work area at `$7E000`. The guard meant to
  catch exactly that multiplies by 512, so it can never fire. A read
  that fills all four sectors runs over the BRAM work page.
* **iofw:** the CD trace mirror at `$FFEE00` (`hw.h:149`) is defined and
  never used.

None of these are fixed here.

### Decisions needed before phase 1

1. Gate reservation: the EmuTOS patch (recommended), or `AUTO\GEOSRES.PRG`.
2. Shrink the C: ramdisk by 64 KB for two instances, or settle for one.
3. Where GEOSCORE's source lives: a new directory in GEOS-Genesis
   (recommended — it reuses `src/kernal/` directly), or its own repo.
