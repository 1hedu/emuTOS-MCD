# ESG phase 0: GEOS-Genesis kernel feasibility audit (read-only)

Source: `/home/user/geos-genesis` (not modified). Build copy: a scratch copy
(the `.git` and `.toolchain/` directories were left out). Tools used: `m68k-linux-gnu-{as,ld,objdump,nm}` 2.38.

**Build result:** `make CROSS=m68k-linux-gnu- build/GEOS-Genesis.bin` **succeeded**. It wrote
`build/GEOS-Genesis.bin`, 4 MiB, "GEOS GENESIS 35A905EE BASIC **PARTIAL**". PARTIAL means the
Sonic 2 dump and s2disasm were missing, so `.sonicdata` was 76 B and `.sonicsfx` was 0 B. No network
fetch was needed or tried. All 69 app ELFs were built. The 25 module ELFs that `make` deletes as
intermediates were rebuilt on purpose so they could be disassembled. Helper scripts are in
`tools/` here (`relclass.py`: relocation classifier; `absscan.py`: scan for
absolute operands). Raw outputs: `objsizes.txt`, `relclass_all.txt`, `absscan.txt`, `pic_table.md`,
`ext_users.txt` here.

---

## 1. SIZE

### 1.1 Final link (`build/geos.elf`, release, BOOT=basic)

| section | VMA | size (B) | contents |
|---|---|---:|---|
| .vectors + .header | $000000 | 512 | Genesis vectors and cartridge header (Genesis only) |
| .text | $000200 | 98,598 ($18126) | all kernel and Genesis code |
| .rodata | $018326 | 3,082 ($C0A) | |
| .data | – | **0** | linker map `build/geos.map:688` |
| .bss | – | **0** | `build/geos.map:1137`. The kernel has **no** static RAM outside the 64K image |
| .native_api | $020000 | 952 | 157 x 6-byte `jmp` stubs (GEOS_CALL gate) |
| .native_ext | $020400 | 534 | 89 x 6-byte `jmp` stubs (GEOS_EXT_CALL gate, slots 0..88) |
| .geopaint_art | $021000 | 1,354 | fixed-address app data |
| .geowrite_doc | $022000 | 4,984 | fixed-address app data |
| .geowrite_fonts | $024000 | 8,834 | fixed-address app data |
| .geowrite_art | $027000 | 584 | fixed-address app data |
| .topdesk_font | $028000 | 502 | fixed-address app data |
| .geospell_data | $028800 | 498 | fixed-address app data |
| .blobdata | $0289F2 | 41,978 | BSW kernal mirror 16,192; C64 BASIC+KERNAL ROM 16,384; chargen 4,096; on-screen keyboard tiles 3,812; Z80 driver/PWM 1,494 |
| .panicdata | $032DEC | 2,048 | panic-screen font tiles (Genesis) |
| .sonicdata | $0335EC | 76 (partial) | Sonic tile pool and frames. A full build adds roughly 90 KB (ArtUnc_Sonic is 82,720 B, plus dust and signpost) |
| .romvol | $033638 | 1,813,248 | drive A 1581 volume 819,200 + CBM .d64 174,848 + CBM .d81 819,200 |
| .sonicsfx | $210000 | 0 (partial) | Sonic 2 SFX bank (32 KiB aligned, at most 32 KiB) |
| .paintview_art | $218000 | 882 | fixed-address app data |
| .basicdisk | $218372 | 25,052 | geoBASIC language code (geob_lang 14,884) + 1541 drive emulation code (cbm_disk 10,168) |
| .drivedos | $21E54E | 16,384 | 1541-II DOS ROM, verbatim (read only by `M-R`) |

### 1.2 Split by source tree (from per-object `objdump -h`)

| group | code (.text+.rodata) | other sections | total |
|---|---:|---|---:|
| **(a) src/kernal/\*\*** | **84,091** (text 83,259 + rodata 832) | gates 1,486; .blobdata 36,672; .basicdisk 25,052; .drivedos 16,384; app-data blobs 17,638 | **181,323** |
| **(b) src/genesis/\*\*** | **17,514** (text 15,268 + rodata 2,246) | vectors/header 512; .blobdata 5,306; .panicdata 2,048; .sonicdata 76+ | **25,456** (partial build) |
| **(c) src/storage** | 0 | .romvol 1,813,248 | 1,813,248 |

The kernel code in (a), broken down by subsystem:

| subsystem | B | | subsystem | B |
|---|---:|---|---|---:|
| basic/ (cbm_\* + basic.S, .text/.rodata) | 26,210 | | graph/ | 5,204 |
| asm/ (asm68k, asm6502, link68k, debug68k) | 16,490 | | fonts/ | 4,328 |
| files/ (storage_api + storage_backend) | 10,595 | | dlgbox/ | 4,234 |
| math/ (incl. cbmfloat 1,662) | 2,472 | | paint/ (paint/gif/dither code) | 2,434 |
| load/ (application.S) | 2,439 | | menu/ | 2,162 |
| conio/ | 1,274 | | input/ | 1,000 |
| drag/ | 908 | | mouse/ | 880 |
| process/ | 814 | | icon/ | 558 |
| compat/, memory/, time/, mainloop/, panic/, prefs/, vars/, jumptab rodata | ~1,958 | | | |

The Genesis layer in (b), by file (.text, plus rodata/blobs): sonic.S 4,194 (+76); vdp.S 1,850
(+2,048 LUT rodata); input.S 2,206; window_keyboard.S 1,878 (+3,812 tiles); print_screen.S 1,226;
start.S 970 (+512); sound_hal.S 970; panic_screen.S 710 (+2,048 font); sound.S 648 (+1,494);
ext_serial.S 616.

### 1.3 Blobs: what the kernel needs at runtime, and what is Genesis-only

| blob | B | needed by a TOS build? |
|---|---:|---|
| BSW GEOS kernal mirror (`nucleus_init.S:94`, copied to logical $BF40..$FE7F by `install_reference_kernel_mirror`, `nucleus_init.S:85-92`) | 16,192 | **Yes, at instance init.** It holds the dialog and mouse assets, the $C000 header and the $C100 jump table bytes, all read at runtime from the image copy. The ROM copy can live in a file and be read once per instance |
| C64 BASIC 901226-01 + KERNAL 901227-03 (`cbm_rom.S:66,68`) | 16,384 | Yes for the BASIC console: banner and message strings via `cbm_rom_ptr`, and PEEK of $A000-$BFFF/$E000-$FFFF |
| C64 chargen 901225-01 (`cbm_text.S:128`) | 4,096 | Yes (BASIC console glyphs) |
| .basicdisk: geob_lang code + cbm_disk code | 25,052 | Yes. This is **code**. cbm_interp/cbm_screen/cbm_eval reference `gb_*` symbols in geob_lang, so the console cannot be linked without it |
| .drivedos (1541 DOS ROM) | 16,384 | Optional. Only `M-R` reads it, so it can be loaded on demand from a file |
| App data blobs $21000-$28800 + PaintView $218000 | 17,638 | Needed by geoPaint, geoWrite, geoSpell, geoProg, TopDesk and PaintView. Apps reach them by **absolute address** (see section 4) |
| On-screen keyboard tiles | 3,812 | No (Genesis pad UI) |
| Z80 sound driver + PWM blob | 1,494 | No |
| Panic font tiles | 2,048 | No. A TOS panic screen can use the GEOS system font |
| Sonic tile pool and frames (+ SFX bank + Z80 driver 6,673) | about 90-130 KB in a full build | No |
| QA `.qatext`/`.qadata` | not in the release link | No |
| ROM disk volumes (`rom_volume.S`) | 1,813,248 | No. They become disk-image **files** (1581 .d81 for drive A, .d64, .d81). All reads go through one choke point, `st_block_read_idx` (`storage_backend.S:260`) / `st_block_write_idx` (`:541`) |

### 1.4 RAM outside the 64K image (Genesis today)

* Work RAM: the Genesis has exactly 64 KB of WRAM ($FF0000-$FFFFFF), and that **is** the GEOS image.
  The 68000 stack is **inside** the image: `GEOS_STACK_TOP=$FFFFFC`, `GEOS_STACK_FLOOR=$FFF820`
  (`geos_map.inc:91-92`), 2,012 B. No .bss and no .data.
* Z80 RAM: the scratch window. The doc says 6,656 B (`native_api.inc:249`), but
  `geos_z80_scratch_claim` takes the whole 8 KB (`sound.S:461` `Z80_SCRATCH_LEN=$2000`). geoWrite and
  geoPaint use it, and so does the geoPaint colour backup (EXT 66).
* VRAM/CRAM/VSRAM: display only (tiles built from the $A000 bitmap, sprites, keyboard).
* Cartridge SRAM: drive B, 32 KB (`ST_BLOCK_COUNT=128` x 256, `storage_constants.inc:3`), accessed
  through `$A130F1`/`$200001`.

### 1.5 Estimated resident size of a TOS build

| item | full | lean |
|---|---:|---:|
| kernal code (.text+.rodata) | 84,091 | 67,601 (asm/link/debug, 16,490 B, made a loadable overlay) |
| gate tables | 1,486 | 1,486 |
| BSW mirror source | 16,192 | 0 (read from a file at instance init) |
| C64 ROMs + chargen | 20,480 | 20,480 |
| .basicdisk code | 25,052 | 25,052 |
| 1541 DOS ROM | 16,384 | 0 (read from a file on `M-R`) |
| app data blobs | 17,638 | 0 (become files or app-loaded data; needs app changes) |
| new TOS layer (presenter, input, VBL glue, GEMDOS block I/O, stubs) | ~10,000 (estimate) | ~8,000 |
| **resident code+data** | **~191 KB** | **~122 KB** |
| per instance: 64 KB image + 8 KB scratch + ~1 KB host-side state | 73 KB | 73 KB (or 65 KB without scratch) |
| **one instance total** | **~264 KB** | **~195 KB** |

Against about 384 KB of ST-RAM, the full build leaves roughly 120 KB for EmuTOS, AES buffers and the
screen. That is very tight, so the lean build is the practical target. A second instance costs
another 65-73 KB. Drive B as a file can be uncached (256-byte block I/O through GEMDOS).

---

## 2. HARDWARE ACCESS INVENTORY (src/kernal/\*\* and apps/\*.S)

Method: grep for every 24-bit absolute equate and literal in both trees, every `%sr`/`%usp`/
`stop`/`reset`/`rte`/`trap`, and every use of the physical-address equates (`GEOS_BASE`,
`GEOS_ANCHOR`, `GEOS_STACK_*`, `LNK_ROM_*`, `ST_SRAM_*`). This was cross-checked against a
relocation-level classification of every app object (section 4). Note that `move.w %sr,<ea>`
(move **from** SR) is **not** privileged on a plain 68000, so it is listed separately.

### 2.1 src/kernal: direct hardware

| file:line | access | purpose | count |
|---|---|---|---:|
| `prefs/prefs.S:43-44,90` | `move.w d0,$C00004` (VDP reg 7) | Preferences border colour | 1 write |
| `files/storage_backend.S:222,235,242,255` | `move.b #0/1,$A130F1` | SRAM enable/disable (drive B) | 4 |
| `files/storage_backend.S:225,245` (+ byte loops 228-232, 248-252) | `movea.l #$200001,a1` and odd-byte copy | drive B data | 2 (+2 loops) |
| `asm/link68k.S:750,796-797,887-888,1857` | `LNK_ROM_STACK/RAMBASE` emitted as **data** | geoLinker `.rom` output (writes a Genesis cartridge header). Genesis-specific output format, not a runtime access | 6 |
| **total real hardware touches in src/kernal** | | | **7 sites (1 VDP, 6 SRAM)** |

There is no VDP data port access, no $A1xxxx I/O, no Z80 RAM access and no TMSS in src/kernal. All of
those are in src/genesis (start.S:5-7,176-185,197-200; sound.S; input.S; ext_serial.S; vdp.S).

### 2.2 src/kernal: absolute RAM that bypasses a6/a5

| file:line | reference | purpose | count |
|---|---|---|---:|
| `asm/debug68k.S:312-514` | `(GEOS_ANCHOR+GEN_DBG_*).l` | debugger state, read through the absolute anchor because "a watched program need not have left A6 alone" | 22 |
| `load/application.S:596,675` | `move.l #GEOS_STACK_TOP,sp` | stack reset in StartAppl/EnterDeskTop | 2 |
| `basic/cbm_boot.S:191,218` | `movea.l #GEOS_STACK_TOP,sp` | stack reset when entering BASIC or cold start | 2 |
| `process/process3c.S:49` | `subi.l #GEOS_BASE,d3` | `_Sleep` turns the caller's PC into a logical address (should be `sub.l a5`) | 1 |
| **total** | | | **27** |

### 2.3 src/kernal: privileged instructions (all are user-mode traps under TOS)

| file:line | instruction | purpose |
|---|---|---|
| `process/process3c.S:16,47` | `ori.w #$0700,sr` | mask IRQ around timer/delay table edits |
| `process/process3c.S:43,66,70` | `move.w dN,sr` | restore |
| `load/application.S:652,1113` | `move.w #$2000,sr` | unmask before entering the runtime loop |
| `load/application.S:807` | `move.w #$2700,sr` | halt on desktop load failure |
| `load/application.S:1115` | `stop #$2000` | **runtime loop: wait for VBlank** |
| `mainloop/mainloop1.S:43` | `stop #$2000` | `_MainLoop` (only reached via NJT_MainLoop, `jumptab.S:364`) |
| `dlgbox/dialog.S:1027` | `stop #$2000` | **DoDlgBox's own nested wait loop** |
| `drag/moveframe.S:205` | `stop #$2000` | `geos_move_frame_wait` nested loop |
| `basic/cbm_interp.S:3445` | `stop #$2000` | 2-second hold before LOAD"GEOS" |
| `basic/cbm_boot.S:187,214,228` | `move.w #$2x00,sr` | BASIC entry and READY loop |
| `basic/cbm_boot.S:230,293` | `stop #$2000` | **BASIC READY loop and `cb_pump`** (INPUT) |
| `asm/debug68k.S:219,258` | `rte` (and building SR with the S/T bits) | debugger enter/resume of the watched program; uses the trace and illegal vectors in start.S |
| **total privileged** | | **20** (7 `stop`, 11 SR writes, 2 `rte`) |

Moves **from** SR, which are user-legal on a 68000: `graph/point.S:47,80`, `process3c.S:15,46`,
`debug68k.S:210` (5 sites).

### 2.4 apps/\*.S

| file:line | what | class |
|---|---|---|
| `nongeos.S:40-41` + 27 accesses (lines 186-304) | VDP ctrl/data writes and reads (palette, SAT, scroll) | **Genesis-only app** (a "takeover" demo) |
| `nongeos.S:362,373` | `move.w #$2700,sr` / `move.w (sp)+,sr` | privileged |
| `topdesk.S:2885,2896` | `ori.w #$700,sr` / `move.w (sp)+,sr` around DoMenu (masks the mouse IRQ) | privileged |
| `topdesk.S:4232-4236` | `move.w #$2700,sr`, then jump through reset vector 0 ("Special → Reset") | privileged + Genesis cold boot |
| `desktop.S:522,534` | `move.w (sp)+,sr` in `dt_enter`/`dt_leave` (should be `,ccr`) | privileged |
| `paintview.S:342` | `move.l #$218000` (PAINTVIEW_ART_BASE) | absolute ROM blob |
| moves from SR (user-legal) | textgrabber.S:2759,2808,5022,5028; desktop.S:503,525; scrapit.S:1523; topdesk.S:2884; nongeos.S:361 | OK |
| `iconedit.S:1296`, `geopublish.S:5574` | `andi.l #$00FFFFFF` | address mask, harmless |
| **apps total** | 29 VDP lines (nongeos), **7 privileged SR writes** (3 apps), 1 reset-vector jump | |

There is no I/O ($A1xxxx), Z80, SRAM, TMSS, `stop`, `trap`, `reset` or USP in any app. No app
reads or writes $FFxxxx RAM directly. Every app reaches the image through a5/a6.

---

## 3. EXT CALL SLOTS (`include/native_api.inc:167-393`, table `src/kernal/jumptab/native_ext.S`)

Classes: **P** = portable kernal code, a6-only. **T** = needs a TOS reimplementation (lives in
src/genesis or touches hardware). **G** = Genesis-only, stub it.

| # | slot | impl | class | users |
|---:|---|---|---|---|
| 0-2 | GET_1ST/NXT_DIR_ENTRY, SET_DIR_NUM | files/storage_api.S | P | iconedit headedit topdesk textgrabber topdesk_mod9 geogif geodebug |
| 3,4,59 | SOUND_BOOT/WRITE/STOP | genesis/sound.S | T (or stub) | synth, topdesk_mod3 |
| 5 | UNDELETE_FILE | storage_api.S | P | desktop_mod1 |
| 6 | SET_ALARM | time/time1.S | P | – |
| 7 | SET_PROTECTION | storage_backend.S | P | – |
| 8 | PRINT_SCREEN | genesis/print_screen.S | T | selftest only |
| 9 | BUILD_COMPOSITE | storage_backend.S | P | topdesk, autoexec_test |
| 10-14 | RS232_OPEN/CLOSE/GET/PUT/AVAIL | genesis/ext_serial.S | T (Bconin/Bconout AUX) | terminal, serialkbd |
| 15 | BASIC_RUN | basic/basic.S | P | basic |
| 16-19, 28 | ASM_ASSEMBLE/LINK/WRITE_REL, LINK_MODULES, LINK_COMMAND | asm/asm68k.S, link68k.S | P (the `.rom` output writes a Genesis header) | geoprog (+selftests) |
| 20-27, 29-31 | DBG_\* | asm/debug68k.S | T (rte/trace/vectors → Setexc + Supexec; absolute anchor) | geodebug |
| 32 | VIDEO_RESTORE | genesis/vdp.S | G (on TOS = full repaint) | nongeos |
| 33-35 | PAINT_UNPACK/PACK/UNIFORM | paint/paint68k.S | P | geopaint, geogif |
| 36-38 | GIF_OPEN/ROW/DITHER_ROW | paint/gif68k.S, dither68k.S | P | geogif |
| 39-45 | FP_\* | math/cbmfloat.S | P | calc |
| 46 | SONIC_RUN | genesis/sonic.S | **G** | sonic DA |
| 47-48 | START_MOVE_FRAME, MOVE_FRAME_WAIT | drag/moveframe.S | P, but 48 has a `stop` wait loop (yield point) | topdesk_m8.inc, ruler |
| 49-50 | INV_FRAME, SPEED_FRAME | graph/frame.S | P | topdesk_m8.inc |
| 51-53 | MAX/SET/RESTORE_TEXT_WIN | graph/textwin.S | P | topdesk |
| 54 | APPLY_PREFS | prefs/prefs.S | P except one VDP write (`prefs.S:90`) | topdesk, prefs |
| 55-58 | Z80_SCRATCH_OUT/IN/CLAIM/RELEASE | genesis/sound.S | T (easy: a per-instance 8 KB buffer) | **geowrite** (all 4), **geopaint** (claim/release) |
| 60 | TEXT_PAINT | basic/cbm_text.S | P | basic |
| 61 | BASIC_ENTER | basic/cbm_boot.S | P/T (SR writes, `stop`, absolute stack) | topdesk, desktop_mod4 |
| 62-65 | GBDBG_LOAD/CLEAR_BRK, GB_ADD/SET_OBJECTS | basic/geob_lang.S | P | basic |
| 66-69, 86 | COLOUR_BACKUP/WRITE/READ/SPAN, COLOUR_ADDR | genesis/vdp.S | T (colour cards for the presenter; 66 uses the Z80 scratch) | geopaint (all), geoclock (67, 68) |
| 70-78 | PRINT_PAGE_\*, PRINT_DRIVER_LOAD, PRINT_SCREEN_PAGE(_X2), PRINT_TEXT_\* | genesis/print_screen.S | T (73 is mostly storage; others → Cprnout/parallel) | printdrv, printit, topdesk, geowrite_mod7, scrapit_out_printer |
| 79-83 | FONT_LOAD/SCAN/FIND/SIZES/IDS_SLOT | fonts/font_basic.S | P | geowrite, fontswap |
| 84-85 | FOLLOW_CHAIN, ALLOC_ONE_BLOCK | storage_api.S | P | combiner; iconedit desktop_mod3 topdesk_mod9 terminal desktop_ops.inc |
| 87 | POP_MOUSE_EDGE | genesis/input.S | T (small; logic is a6 queue) | scrapit |
| 88 | SPRITE_COLOUR | mouse/mouse.S | P (sprite rendering is T) | photomgr |

Counts: P = 61 slots, T = 26, G = 2 (SONIC_RUN, VIDEO_RESTORE). Sound (3 slots) can also be stubbed.

**Genesis-only features and the apps that use them:**

* **Sprites** (API 66/69/70/71 → `mouse.S` → `m3_sprite_upload_n`, window_keyboard.S): ruler, desktop,
  terminal, photomgr, geowrite, geopaint (+geopublish uses DrawSprite). The TOS layer needs a software
  sprite or pointer compositor for 8 sprites.
* **Sound:** synth, topdesk_mod3. geoBASIC SOUND/TIMBRE also calls `geos_snd_*` directly from
  `geob_lang`.
* **Sonic:** sonic DA.
* **Serial:** terminal, serialkbd (+ `geos_rs232_service` is called from `application.S`'s runtime step).
* **VDP video restore:** nongeos.
* **BASIC enter:** topdesk, desktop_mod4.
* **Z80 scratch:** geowrite, geopaint.
* **Colour matrix:** geopaint, geoclock.

---

## 4. POSITION INDEPENDENCE

### 4.1 How an app linked at $0400 runs at $FF0400

* `apps/native_app.ld` links `.text+.rodata+.data` at $0400 and `.bss` (NOLOAD) after it, all
  below $6000. Desk accessories link at $1000 (`native_deskacc.ld`), overlay modules at $6000
  (`native_module.ld`, `--just-symbols` against the main app), the printer driver at $7900, and
  ScrapIt modules at $2600/$2E00.
* The kernel sets **a5 = $FF0000** (image base) and **a6 = $FF8000** (`start.S:146-147`). Every
  app reference to its own data is written `label(%a5)`. The 16-bit displacement equals the
  link-time logical address, so the run-time address is a5+label. GEOS variables are
  `(var-$8000)(%a6)`.
* Code-to-code is `bsr/bra` (resolved at assembly time, no relocation), or `lea x(%pc)`.
  Cross-section references are `R_68K_PC16`.
* Code addresses handed to the kernel (appMain, vectors, menu/icon routines) are stored as
  **16-bit logical** values and called through `geos_call_logical` = `jsr (a5+logical)`
  (`mainloop1.S:12-20`). Pointers passed in r0..r15 are logical too (`LOGICAL_TO_A`,
  `geos_macros.inc:56-59`).
* Run-time code addresses are made with `lea label(%a5)` (for example `desktop.S:513` dt_leave).
  `tools/verify_app_dispatch_tables.py` enforces offset or `jsr 0(a5,dN)` dispatch tables.
* The **only** absolute operands are the kernel gates: `jsr $20000+6n` (GEOS_CALL,
  `native_api.inc:4-6`) and `jsr $20400+6n` (GEOS_EXT_CALL, `:395-397`), plus the fixed data blobs
  ($21000..$28800, $218000).

### 4.2 Verification (relocation classes per object, then a check of the linked ELF disassembly)

geoWrite and geoPaint, `objdump -dr`:

| app | a5-disp16 | PC-rel | `#imm16` (logical ptr) | data words (logical ptrs in tables) | **abs32 to own address** | `jsr` API gate | `jsr` EXT gate | fixed-blob refs |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| geowrite | 774 | 71 | 147 | 90 | **0** | 151 | 10 | 6 (GEOWRITE_ART/DOC_BASE, `geowrite.S:552,562,567,1576,1591,1705`) |
| geopaint | 535 | 6 | 114 | 26 | **0** | 100 | 16 | 7 (`lea GEOPAINT_ART_BASE`, `geopaint.S:1142,1201,1208,1446,5553,5698,6945`) |

A grep for absolute-short `label.w` operands in apps/\*.S found none. Absolute operands in the
linked ELFs are only the gates ($200xx/$204xx) and the blob bases. There are no $FFxxxx operands
(the two `#$00FFFFFF` hits are masks) and no VDP/IO/Z80/SRAM operands outside nongeos. All 69 apps
are tabulated in `pic_table.md`. Over every app: 7,699 a5-disp, 1,140
PC-rel, 2,161 imm16, 181 data-table words, **116 abs32**, about 1,839 API-gate and 144 EXT-gate
`jsr` (1,748 + 135 macro sites in source).

The 116 abs32 relocations:

| app | sites | verdict |
|---|---:|---|
| geoprog (`.gben_suffix`, `gp_open_error_file`) | 2 | `lea label,a0` then `adda.l a5,a0`: base-relative, **OK** |
| basic, basicgrabber, bigclipper, filecopy, topdesk | 5 | `adda.l #label` / `addi.l #label` / `subi.l #label` on an a5-derived value: **OK** |
| **textgrabber** | **106** | `tg_handler_map` (`textgrabber.S:5757-5967`, 105 `.long handler`) is dereferenced by `movea.l d3,a2; jsr (a2)` (`:5737-5745`). Also `move.l #tg_to_desktop,tg_unwind(a5)` (`:449`), then `jmp (a0)` (`:930-931`). **These jump to $00000xxx, which is cartridge ROM on the Genesis, so they are latent bugs there too.** Fix: offset table + `lea` table(pc), or `lea tg_to_desktop(a5)` |
| **prefs** | **3** | `lea pf_row_name_tab,a0` (`prefs.S:340`), `lea pf_speed_name_tab,a1` (`:420`), and `lea pf_arrow,a1` (`:869`). All read $00000xxx (ROM). **Latent bug; also wrong under TOS.** |

### 4.3 What breaks if an app is loaded at an arbitrary address with a6 elsewhere

Assume a5 = image base, a6 = a5+$8000, and app bytes copied to a5+link address. Then:

1. **Every app** breaks unless the gate stays at absolute $20000/$20400. The sites are absolute
   `jsr` with no relocation entry in the `.bin`. Options:
   * (a) reserve $20000-$207FF (plus $21000-$289FF for blobs) at a fixed physical address in the
     EmuTOS sub-CPU memory map;
   * (b) **reassemble the apps** with GEOS_CALL/GEOS_EXT_CALL redefined, for example
     `jsr (slot*6)(%aN)` with a dedicated register, or a trampoline table inside the image;
   * (c) build-time fixup lists from `ld --emit-relocs` (the gate is an absolute symbol, so a
     list of the `4EB9 0002 xxxx` sites must be generated by the build, not scanned).

   The kernel itself contains 35 numeric `jsr $200xx` (GEOS_CALL in asm68k.S x12, link68k.S x20,
   debug68k.S x3). These are **not** relocated by a TOS PRG fixup and must be changed to `NJT_*`
   symbols.
2. **geopaint, geowrite, geospell, geoprog, paintview, topdesk:** absolute blob bases (23 source
   sites). PaintView's $218000 is outside a 512 KB sub-CPU map entirely.
3. **textgrabber, prefs:** absolute own-address bugs (4.2). They are already broken on those paths
   on the Genesis.
4. **nongeos:** VDP. Genesis-only, so drop it.
5. **topdesk, desktop:** privileged SR writes (2.4). Fix with `move.w (sp)+,ccr` in desktop. TopDesk
   needs a host call for IRQ masking, and "Reset" needs a TOS-appropriate action.

No app depends on the image being at $FF0000, and none needs a 64K-aligned base. Physical↔logical
conversion everywhere uses `sub.l a5` / `adda.l a5` (145 sites). A heuristic scan for truncating
`move.w aN` conversions found no real cases. The one kernel exception is `_Sleep`'s
`subi.l #GEOS_BASE` (`process3c.S:49`).

---

## 5. KERNEL ENTRY / INTERRUPT MODEL

### 5.1 Boot (`src/genesis/start.S:135-336`)

1. IRQ off. Clear the 64K WRAM. SP=$FFFFFC, a5=$FF0000, a6=$FF8000.
2. Park the Z80 (`:176-185`). `_FirstInit` (`nucleus_init.S`: copies the BSW mirror, zeroes the
   process/timer state).
3. TMSS (`:197-200`). Video Hz from VDP status. `vdp_init`, `_GraphicsInit`. `geos_input_init`,
   `geos_storage_init`, `geos_m4_init`.
4. `move #$2000,sr`, then either `jmp cbm_basic_enter` (BOOT=basic) or `jmp _EnterDeskTop`.
   LOAD"GEOS" re-enters via `geos_boot_from_basic` (`:352-361`), which clears WRAM and repeats.

### 5.2 Foreground (`src/kernal/load/application.S:1087-1123`)

```
geos_runtime_loop:  move #$2000,sr
  wait: stop #$2000 ; tst.b GEN_FRAME_TICK ; beq wait ; clr.b GEN_FRAME_TICK
        bsr geos_runtime_step ; bra wait
geos_runtime_step:  _DoCheckButtons ; geos_rs232_service ; _ExecuteProcesses ;
                    _DoCheckDelays ; _DoUpdateTime ; appMain via geos_call_logical ; rts
```

`_MainLoop` (`mainloop1.S:27-44`) is the same body. Apps only reach it through the jump table.

### 5.3 VBlank (`start.S:363-492`, `vblank_isr`, level 6)

1. Stack-floor check (`STAK` panic). It reads `GEN_DBG_ACTIVE` through the absolute anchor.
2. Save d0-d7/a0-a4. **a5/a6 are assumed, not loaded.**
3. Save r0..r15 (32 B), returnAddress and CallRLo/Hi, as irq.s does.
4. Acknowledge the VDP. Set `GEN_FRAME_TICK=1`. Poll the UART if open. `GEN_M1_FRAME_COUNT++`.
5. Call `_InterruptMain` k times, normalised to 60 Hz through `GEN_M1_TICK_ACCUM`/`GEN_M1_VIDEO_HZ`.
   `_InterruptMain` (`mainloop3.S`) does: intTopVector → dblClickCount-- → alarmWarnFlag-- →
   `_ProcessMouse` (calls `genesis_input_update` **and** the sprite/pointer upload) →
   `_ProcessTimers` → `_ProcessDelays` → `_GeosTickTime` → `_GetRandom` → `ProcessCursor` →
   ticks++ → intBotVector.
6. `geos_vdp_present_dirty`: dirty cards from the **$A000 bitmap** (`GEN_M2_DIRTY_BITS` at $8980,
   1000 bits) plus the colour matrix (`GEN_COLOR_PTR`/`DIRTY_LO/HI`) go into VRAM.
7. `geos_sonic_vblank`. Restore. `rte`.

HBlank (`:509-522`) is only used for serial RX.

### 5.4 Driving one iteration from GEM `evnt_multi` (timer)

`geos_runtime_step` is already one iteration. The blocker is **nested wait loops** that never return
to the top loop:

* DoDlgBox (`dialog.S:1020-1040`)
* `geos_move_frame_wait` (`moveframe.S:200-212`)
* the BASIC READY loop and INPUT (`cbm_boot.S:226-245`, `:290-310`)
* `cbm_interp.S:3440-3450`
* desk accessories, which run nested from the app's dispatch
* app **busy-waits on `GEN_M1_FRAME_COUNT`**: `topdesk.S:3302-3305`, `topdesk_mod3.S:265-268`,
  `fontswap.S:519-521`, `geopaint.S:5723`

Recommended model: run each instance as a **coroutine**. Its stack is already inside its own image
($F820-$FFFC). Replace each of the 7 kernel `stop #$2000` sites with `jsr geos_yield`, which saves
the registers and SP into the image and switches to the host stack. The GEM loop then resumes the
instance on each timer event after setting GEN_FRAME_TICK. Busy-waits in apps still need the
counter to advance asynchronously. So keep a **VBL-queue routine** (the "interrupt half") that, for
each live instance:

* loads a5/a6;
* saves and restores r0-r15, returnAddress and CallR (port the start.S code into the TOS layer);
* bumps FRAME_COUNT and sets FRAME_TICK;
* runs `_InterruptMain` k times.

The VBL runs on the supervisor stack, so it does not eat the 2 KB image stack.

Requirements for that VBL routine:

* Replace the kernel's critical sections (`ori #$700,sr` in `process3c.S`, plus TopDesk's) with a
  per-instance "in kernel critical section" flag. If the flag is set, the VBL routine defers to the
  next tick.
* Feed input from a TOS-layer ring (IKBD/mouse via `Kbdvbase` hooks, or from evnt_multi when the
  window has focus) into `geos_enqueue_key` / `mouseXPos`/`mouseYPos` / the button-edge queue.
  Today that is `genesis_input_update`.
* Do the presenter (dirty cards → screen) in the foreground after a step, or in the VBL.
* intTop/intBot vectors run app code at interrupt level, as on the Genesis.

The alternative is to run the whole GEOS side in supervisor mode (`Super`). The SR writes then work
unchanged, `stop` waits for the TOS VBL/200 Hz interrupt, and only GEN_FRAME_TICK and k-times
InterruptMain come from the VBL queue. That is the smallest port for a single instance, but it is
incompatible with GEM multitasking.

### 5.5 State outside the a6 image (determines code sharing across instances)

* Kernel `.data` and `.bss` are both **zero bytes**. There is no ROM-resident writable data (the
  kernel ran from cartridge ROM, so a write there would have been dropped).
* All kernel and app state, including the stack, is in the 64K image.

Exceptions to fix for multi-instance sharing:

| item | where | fix |
|---|---|---|
| absolute anchor `$FF8000` | `debug68k.S` x22, `start.S:386` | per-instance pointer (keep in the VBL glue) |
| absolute stack top | `application.S:596,675`; `cbm_boot.S:191,218` | `lea (STACK_TOP-$FF8000)(a6),sp` |
| `_Sleep` | `process3c.S:49` (`subi.l #GEOS_BASE`) | `sub.l a5` |
| Z80 scratch (one per machine) | sound.S | per-instance buffer |
| drive B SRAM | storage_backend.S | per-instance or shared file with locking |
| VRAM, sprites, border | vdp.S etc. | per-window presenter. The bitmap and dirty bits are already per image |
| physical pointers stored **inside** the image | `GEN_M4_DACC_SAVED_SP`, `GEN_DBG_*`, `GEN_M2UI_BITMAP_BASE/FONT_BASE`, `GEN_COLOR_PTR`, app-saved pointers such as `fp@(3096)` and `tg_stack` | fine as long as an image never moves after creation. Snapshot/relocate of an instance would break |

**Conclusion:** yes. Multiple instances can share one kernel code copy by switching a5/a6/SP, once the
listed fixes are made. Interrupts must not run one instance's InterruptMain while another instance's
critical-section flag is set **for that instance**. A flag per image is enough because there is no
shared mutable kernel state.

---

## 6. BASIC FIRMWARE (src/kernal/basic) as the console

* **What it is:** a 68000 re-implementation, routine for routine, of C64 BASIC V2 and the KERNAL
  screen editor. The 6502 is never executed. It includes `cbm_screen.S` ($E518-$EA75 editor),
  `cbm_interp.S`, `cbm_eval.S`, `cbm_fac.S` (+ `math/cbmfloat.S`), `cbm_mem.S`, and `cbm_disk.S`
  (a 1541 on top of the storage drives, + the DOS ROM for `M-R`). It shares tokenizer and statement
  tables with geoBASIC (`geob_lang.S`, the `DIA_CBM/DIA_GEOB` dialect switch, `geos_map.inc:322-324`).
* **Memory:** the BASIC instance uses the whole 64K image as C64 RAM.
  * `CBM_R0=$0400` holds C64 $0000-$7BFF, and `CBM_R1=$BF40` holds C64 $7C00-$9FFF
    (`geos_map.inc:243-260`), so 38,911 bytes free.
  * Colour RAM, attribute and shadow matrices and VIC registers sit at $E340-$F327, with scratch at
    $F328.
  * `cbm_basic_enter` (`cbm_boot.S:184-222`) tears GEOS down ("GEOS does not come back"). LOAD"GEOS"
    re-inits through `geos_boot_from_basic` (start.S).
  * So a console is **its own instance image**, not a window inside a GEOS instance.
* **Rendering:**
  1. The editor writes screen codes to C64 $0400 (image $0800) and ink to C64 $D800 (CBM_COLOR).
  2. `cb_present` (`cbm_screen.S:~1111`) builds the (ink<<4)|paper attribute matrix and publishes it
     through `GEN_COLOR_PTR`/`GEN_COLOR_DIRTY_LO/HI`.
  3. It then diffs screen codes against `CBM_SHADOW` and calls `cbm_text_paint` (`cbm_text.S:53`,
     also EXT 60).
  4. `cbm_text_paint` copies 8 glyph bytes from the chargen (bank 0/1 per $D018 bit 1) into the
     **GEOS bitmap at SCREEN_BASE $A000** (`row*320+col*8`) and marks dirty tiles
     (`graph_mark_tile_index`).
  5. The Genesis presenter uploads those in VBlank.

  On TOS it therefore needs the same presenter as GEOS (1bpp 320x200 bitmap + per-card colours + a
  dirty map). The border is `geos_vdp_set_border` (`cbm_boot.S:208`, `cbm_mem.S`).
* **Keys:** `_GetNextChar` (`kernal/input/keyboard.S:57`, the GEOS keyboard queue) → `cb_petscii`
  → `cb_key`, polled once per frame in `cb_main_loop` / `cb_pump` (`cbm_boot.S:226-310`). The queue
  is filled by `geos_enqueue_key` from genesis input.S (pad, Saturn keyboard, on-screen keyboard).
  On TOS: IKBD scancode → GEOS/PETSCII key → `geos_enqueue_key`. There is no CIA (the CTRL
  slow-scroll at $E938 is omitted).
* **Timing:** each loop turn waits with `stop #$2000` and `GEN_FRAME_TICK`. The cursor blink is
  `cb_blink`, per frame. These are 2 yield points.
* **Dependencies (undefined symbols per object):**
  * `cbm_boot`: `_GetNextChar`, `geos_vdp_set_border`\*, `m3_pointer_sat_update`\*
  * `cbm_mem`: `geos_vdp_set_border`\*
  * `cbm_interp`: `_GetNextChar`, `gb_*` tables, `geos_boot_from_basic`\*
  * `cbm_eval`: `gb_do_*`, `geos_fp_add/cmp/mul`
  * `cbm_screen`: `gb_*_reset`
  * `cbm_text`: `graph_mark_tile_index`
  * `cbm_disk`: 13 `st_*` storage backend routines (drive 8/9 = GEOS storage drives, disk images on TOS)
  * `geob_lang`: `geos_snd_*`\* (SOUND), `geos_print_*`\*

  \* = Genesis layer. Data: chargen 4 KB, C64 ROMs 16 KB, optionally the DOS ROM 16 KB.
* **Size:** cbm_\* code 20,382 + rodata 288; geob_lang 14,884; cbm_disk 10,168; basic.S (geoBASIC
  runner) 5,540; blobs 20,480 (+16,384). The console subsystem is about 51 KB of code + 20 KB of ROM
  data. It is coupled to the kernel's storage, keyboard queue, float and graphics-dirty code, but not
  to the GEOS UI.

---

## Blockers and risks (summary)

1. **Fixed absolute gate and blob addresses.** 1,883 app call sites and 23 blob refs; 35 numeric
   `jsr $200xx` inside the kernel. Either reserve physical $20000-$289FF in the EmuTOS sub-CPU map
   (and move PaintView's $218000 blob), or rebuild the apps with a register- or image-relative gate.
2. **`stop` and SR use (20 privileged kernel sites, 7 in apps).** User-mode TOS requires replacing
   `stop` with yield and the IRQ masks with an instance lock. Supervisor-mode TOS avoids this but
   rules out GEM cooperative scheduling.
3. **Nested blocking loops and app busy-waits.** A coroutine per instance plus a VBL-queue interrupt
   half is required. A plain "call one iteration per evnt_multi" is not enough.
4. **Latent PIC bugs** in textgrabber (106 abs32) and prefs (3 abs32). They are wrong on the Genesis
   too.
5. **Memory:** about 191 KB resident for the full build. Realistically you need the lean build
   (~122 KB) plus 65-73 KB per instance in ~384 KB.
6. **Unknowns on the EmuTOS/Mega-CD side:** whether there is a VBL source on the sub-CPU (the
   Genesis VBlank is relayed as a level-2 interrupt), display ownership (the sub-CPU has no direct
   video, so the presenter must hand cards to the main CPU or Word RAM), and IKBD equivalents.
