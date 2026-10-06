# ESC64 phase 0 — iofw feasibility (C64! palette rectangles)

Read-only study of `/home/user/emuTOS-MCD/iofw`. Builds were done in a copy under
a scratch copy.
Line refs are to the unmodified tree.

## 1. WRAM budget (measured)

The build uses `tools/build-iso.sh:65-112` (`cbin`, `-m68000 -O2 -fomit-frame-pointer -ffreestanding`,
`tools/flat.ld`, `-Ttext=0xFF1000`, no `-mshort`). The check is `__bss_end <= 0xFF7000`
(`build-iso.sh:97-109`); a second check caps the binary at 24576 bytes (`build-iso.sh:137-138`,
matching the copy in `crt0.S:20-24`). `/opt/m68k-elf` is not installed, so I built with the
distro `m68k-linux-gnu-gcc 13.3` and the same flags, plus `-fno-pic -fno-stack-protector
-fno-asynchronous-unwind-tables`. It did not ICE, because iofw doesn't use `-mshort`. `osk_font.h` comes from `tools/mkfont.py`.

| item | bytes |
|---|---|
| .text (code 17,728 + rodata 2,026: osk_font 1632, osk rows ~216, uart_keymap 128, misc 50) | 19,760 |
| .data | 56 |
| .bss (`__bss_start` 0xFF5D68 → `__bss_end` 0xFF6F74), of which `tab8` 4096, `tdirty` 125, prn_buf 64, q 64 | 4,619 (+align) |
| **total used from 0xFF1000** | **24,436** |
| limit (0xFF7000 − 0xFF1000) | 24,576 |
| **free** | **140** |

So the "152 bytes free" claim checks out to within one compiler build: the distro gcc 13.3
leaves 140. The 12-byte gap comes from the toolchain, not the source. In practice the margin is about 150 bytes.

Biggest symbols: `main` 8468 (everything inlined, `.text.startup`), `tab8` 4096 (BSS), `cart_probe`
2844, `osk_font` 1632, `input_update` 940, `osk_draw` 790, `vdp_init` 528.

Comparison builds:
- `-Os` for everything: `__bss_end` 0xFF5EB8, **4,424 free** (+4.3 KB). main.o drops 3238, osk 618, input 258, uart 170.
- `-DIOFW_HUD=1` (`main.c:45`): 1,872 over the wall. The HUD can't be built today at all.

Stack: `-fstack-usage` gives `main` 360 (including the 256-byte `keep[]` at `main.c:2543`), `cart_probe` 104,
`mem_holds16` 52, `osk_draw` 52. The worst chain is about 600 B. SP starts at 0xFFFC00 (`crt0.S:25`).

## 2. WRAM map and where space can be recovered

| range | use | ref |
|---|---|---|
| FF0000-FF07FF | CD sector capture (stage-3 harness only, copied only when the sub flags it) | main.c:35, 3413-3440 |
| FF0800-FF0847 | CDSTAT: 36 words (72 B, not 16) | main.c:36, hw.h:118-119 |
| FF0900-FF09FF | loader page: M1_FLAG FF09A0, 'IOFW' mark FF09A4, FF09B2/3 | hw.h:58, crt0.S:18 |
| FF0A00-FF0BFF | one-shot PRG dump at frame 900 (debug) | main.c:3444-3461 |
| FF0C00 | DIAG_BENCH only | main.c:708 |
| FF0D00-FF0D3F | WATCH (cdd_watch), PAYLOAD_STAT at +48 | main.c:742-777, 2043 |
| FF0D40-FF0D4B | PRN_STAT | main.c:191 |
| FF0E00-FF0E41 | boot-trace ring (zeroed FF0E00-FF0FFF at boot) | main.c:2419-2422, 3392-3401 |
| FF0E80-FF0EC1 | UART_TAP | uart.c:78, 233 |
| FF0F00-FF0FFF | REPORT (+6 is the frame heartbeat, used functionally at 3414, 3444), F80 key ring | main.c:33, 2743, 3165 |
| FF1000-FF6F74 | iofw code/data/bss | flat.ld |
| FF6F74-FF6FFF | **140 free** | |
| FF7000-FFECFF | planar cache 32000 (= 0x7D00, so it ends at **FFED00**, not FFEE00); also payload image, brm_work | main.c:34, 866 |
| FFED00-FFEDFF | 256 B gap | |
| FFEE00-FFF57F | CDTRACE_WRAM mirror: **defined but unused in current iofw** (only hw.h:149, tools/dump-cdtrace.py:18) | |
| ~FFF5xx-FFFBFF | stack (≤ ~600 B used) | crt0.S:25 |
| FFFC00-FFFCFF | unused (SP pre-decrements from FFFC00) | |
| FFFD00- | BIOS jump table | crt0.S:26 |

In practice FFED00-FFF9FF, about **3.3 KB**, is free WRAM. It sits outside the linked image, so anything placed there needs a fixed address or a second linker section. The cost: payloads run on the iofw stack (docs/payload.md:41-60) and may scribble there, so anything put there must be rebuilt after `payload_run` returns (main.c:2125-2134).

Recovery options, measured where possible:

| option | gain | notes |
|---|---|---|
| A. Move `tab8` (4096 B BSS, main.c:654) to fixed WRAM, e.g. 2 KB at FFED00 + 2 KB at FF0000 (the sector buffer, if the stage-3 capture is compiled out), or 3 KB at FFED00 + 1 KB elsewhere | **+4096** | No speed cost. convert.S:57-60 needs a second base for a3/a4. Call `tab_init()` again after a payload. |
| A'. One table plus shifts (tab8[p][b] = tab8[0][b] << p) | +3072 | About +24 cycles per row (+10-15 % conversion time). Not recommended. |
| B. `-Os` on cold code (cart_probe/cart_* 2.8 KB+, swap_*, payload_run, init, osk) with hot paths kept at -O2 | ~+1.5-2.5 KB (whole-tree -Os: +4.3 KB) | The hot paths (the diff loop main.c:3050ff, screen_scroll_apply) are in `main`/static, so use `__attribute__((optimize("Os")))`/cold, or split the file. |
| C. Release build with no telemetry: 56 single-line REPORT/WATCH/FF0Exx/FF0Fxx stores | **+834** (measured) | Keep REPORT+6 (used functionally). |
| D. Compile out cdd_watch body, boot-trace ring, CD sector capture, frame-900 bank probe/PRG dump | **+712** (measured) | main.c:744-777, 3392-3461 |
| E. 1bpp OSK font (51 × 8 B = 408 instead of 1632; expand at upload, osk.c:138-149) | ~+1.2 KB | Change in mkfont.py plus about 20 B of code. |
| F. Use FFFC00-FFFCFF (raise SP to FFFD00) | +256 outside the image | Only useful for a fixed-address table. |
| G. Execute cold code from ROM on the cartridge build | not useful | The CD boot needs the same image to fit, and Word RAM is handed back to the sub (crt0.S:6-13), so ROM-resident code would need a second build variant. |
| H. Tables in VRAM | small | osk_font is already converted into VRAM tiles 1024-1074, but vdp_init wipes VRAM after every payload (main.c:505-507, 2125-2127), so the source copy has to stay unless the payload contract changes. About 10 KB of VRAM is free (0x8780-0xAFFF) if wanted. |

A + C + D alone give about 5.6 KB with no behaviour change in the release display path.

## 3. Palette lines and nametables

### CRAM usage today

- **Line 0**: the 16 ST colours. Written at vdp_init (main.c:523-524) and on PAL! (main.c:2886-2897).
- **Line 1**: entry 0 is the backdrop black (vdp_reg 7 = 0x10, main.c:514). The heartbeat ramp also writes it while swap_quiet (main.c:2821-2830). Entry 1 is the diagnostic green (main.c:545, osk.c:364). Entry 2 is the paper, a copy of ST colour 0 (main.c:546, 2896-2897). Entry 3 is black, for the diag glyph background (osk.c:368-369). Entries **4-15 are unused**.
- **Line 2**: the OSK key face. Entry 1 is black ink, entry 3 is face 0x0EEE, and 0 and 2 are written as 0 (osk.c:174-178). Entries 4-15 are unused. Used only by the window-plane nametable (osk.c:200, 215, 236).
- **Line 3**: the OSK selected key. Entry 1 is orange ink, entry 3 is the face (osk.c:179-183). The cursor sprite writes **entries 1 and 2** (main.c:2932-2934, sprite attr 0x6000 at main.c:645).
  - **Existing bug:** entry 1 is shared. After the first CUR! load, the selected key's ink becomes the pointer's border colour, which by default is ST colour 0 (white), on a 0x0EEE face. The highlighted key's glyph then all but disappears. osk_upload_tiles runs only at init or after a payload (main.c:2426, 2127), so nothing restores it.

### Freeing a line (line 2) for C64

Line 2 can be freed with a few bytes of code:

1. Move the normal OSK keys onto line 1. Make entry 1 the ink (black) and entry 3 the face (0x0EEE). Change osk.c:200, 215, 236 from `0x4000` to `0x2000`. Stop osk_diag_init writing entries 1 and 3 (osk.c:362-369) and vdp_init writing green (main.c:545). Cost: the HUD build's diag text would read as keyboard colours, and HUD doesn't fit anyway.
2. Keep line 3 for the selected key, but move the cursor to entries 4 and 5 there (fixing the bug). The cheapest way is EmuTOS-side, in `sprite_build`, `? 2 : 1` → `? 5 : 4` (patches/emutos/0186, about line 131). iofw then writes CRAM at `2*(48+4)`. Zero bytes of iofw code. Alternatively, remap the nibbles in iofw when copying the 64 words, about 30 B.

Result: line 2 holds the 16 C64 colours. The selected-key highlight and the cursor fit together in line 3, or the cursor could even move into line 1 entries 4-5.

**Gotcha (transparency):** pixel value 0 is transparent whatever the line (main.c:532-541). In a C64 cell, ST pixel 0 shows plane B, which is the paper tile (TILE_PAPER, line 1 entry 2 = ST colour 0). So C64 colour 0 (black) needs plane B under C64 cells switched to an opaque black tile. One option is a new `TILE_INK` (e.g. 1002, all pixels 1) in line 1, where entry 1 is black after the merge. Line 2 entries 1-15 then hold C64 colours 1-15. On clear, restore `PAL_OURS|TILE_PAPER` (main.c:572-578).

### Who writes nametables

- **Plane A screen (0xC000)**: written only by `screen_scroll_apply()` (main.c:419-427), rows 0-24 × cols 0-39. It writes a bare `slot*40+col`, so it **clears palette bits**. It is called from SCRL (main.c:2988), BLIT via blit_ring (main.c:485) and blit_flatten (main.c:438), screen_nametab (main.c:496) at init (main.c:553), and after a payload (main.c:2126). screen_nametab itself blanks the whole 64×32 plane (main.c:489-497). The `osk_row` text rows 25-27 are HUD only (osk.c:374-388).
- **Plane B (0xE000)**: written once in vdp_init (main.c:570-578).
- **Window plane (0xB000)**: vdp_init blanks it (main.c:579-580). osk_draw (osk.c:189-245) writes rows 17-27, with palette 0x4000/0x6000.
- Sprite table: cursor_sprite (main.c:639-647).

Because the scroll and blit rings rewrite the whole plane A every time, **the C64 mask must live inside `screen_scroll_apply`**, so the palette bit is ORed at every rewrite. The palette is keyed by *screen position* (row, col). It is not keyed by tile slot. The rings only change which VRAM tile a cell points at, so the palette stays correct under SCRL and BLIT. The converter (`convert_tile`, main.c:683ff) writes patterns, not nametables, so it is unaffected.

### Prototype (`iofw-c64-prototype.diff`)

The prototype adds:
- A 125-byte cell bitmap `c64m` (the same layout as tdirty).
- An OR of `0x4000` in screen_scroll_apply.
- `c64_planeb()`, which writes the plane B ink/paper tiles.
- `c64_block()`, which does this:
  - checks magic "C64!" and a generation;
  - loads 16 CRAM words into line 2;
  - builds the mask from up to 16 rects (col, row, w, h in tiles);
  - calls the two rewrites;
  - treats the block disappearing as a clear.

It also adds the TILE_INK init and a reset of `c64_gen` in payload_run.

Measured cost at -O2: **+564 B text, +2 data, +125 bss, about 690 B total** (at -Os, +532 text). A tighter hand version is probably 350-450 B. Either way it **does not fit in the current 140 B**; it needs option A, C, D or E first.

Block placement: the EmuTOS 32K screen allocation ends at screen_woff + 0x8000 = 0x20000. Current blocks end at BLIT 32216 + 18 = 32234 (main.c:621-623). That leaves 534 B, so for example `C64BLK_OFF 32240` holding 4 + 2 + 2 + 32 (palette) + 16 × 4 (rects) = 104 B fits.

Per-frame cost:
- **Idle:** 2-3 word reads through the PRG window inside the grab the pump already holds, plus compares. That's under ~60 cycles. Same pattern as PAL! (main.c:2882-2905).
- **On change:** the mask build (≤ 1000 bit sets) plus a full plane A rewrite (1000 × slot_of ≈ 100-130 k cycles, about one NTSC frame of the ~128 k available, the same as one console scroll today) plus 1000 plane B words (~30 k).
- **Every later SCRL/BLIT apply:** about +25 cycles × 1000 for the mask test (≈ 0.2 frame).

Restricting the rewrite to rows in old ∪ new rects would cut the change cost.

Caveats:
- While GEM moves the GEOS window, the palette update (C64! gen) and the pixel repaint (diff sweep over up to 8 frames) are not synchronised, so expect a few frames of wrong colours.
- EmuTOS must clip the rect list to visible areas (wind_get rect list), or GEM menus and alerts over the GEOS window will be drawn in C64 colours.
- Rects must be 8-px aligned.
- After a payload, vdp_init wipes CRAM and VRAM, so force re-application (gen reset) and re-create TILE_INK.

## 4. Grid alignment and the CUR! cursor

**Tile grid:** confirmed aligned to ST pixel coordinates.
- Nametable row r, col c is ST pixels (8c..8c+7, 8r..8r+7). The rings move only whole 8-line cell rows (srot, rct in tile rows, main.c:383-415), so this always holds.
- The 12-line letterbox is a VSRAM scroll of −12 applied identically to plane A and plane B (main.c:52-57, 584-594). It is display-only and doesn't change which cell holds which ST tile.
- Sprites aren't scrolled, so cursor_sprite adds SCREEN_YOFF (main.c:639-647).
- Partial-group edges in blit_ring are 16-px groups (main.c:462-463). This affects only which tiles repaint, not palette placement.

**Cursor via CUR!** (main.c:2907-2947; EmuTOS patches/emutos/0186):
- The block carries x, y (hotspot applied), visible, bg_col/fg_col (ST colour **indices**), and 64 words = four finished 8×8 tiles (2×2 sprite, column-major), copied verbatim to VRAM tile 1080 (main.c:2928-2931).
- iofw is shape-agnostic, so any 16×16 two-colour form that a GEOS host sets through vsc_form or graf_mouse(USER_DEF) shows up as the hardware sprite with no iofw change. EmuTOS publishes only in 4-plane mode (`v_planes == 4`).
- **Limits:**
  - 16×16 only. The C64 GEOS pointer is a 24×21 sprite. Larger needs a 3×3 sprite: 9 tiles = 288 B of block and a size word, about 20 B more in iofw. The block area has room only if C64! stays small.
  - Two colours, looked up via `st_palette[]` (main.c:2933-2934). To show C64 colours, the block would need a flag (e.g. bit 15 of bg/fg meaning "raw 0x0RGB" or "C64 index"), about 10-20 B of iofw code.
  - The line-3 entry-1 clash with the OSK above should be fixed at the same time.
