# ESC64 Phase 0 — can sub $020000-$020FFF be kept free under emuTOS-MCD?

Short answer: **yes.** Nothing fixed lives in $20000-$20FFF. That range is
ordinary GEMDOS TPA, and the address can be reserved for certain with a few
lines of code, either in EmuTOS or in an AUTO-folder TSR. The reservation
uses 4 KB, but it also splits the TPA, so the largest single block shrinks
by about 46 KB (see "Re-measured on the dev branch").

## Re-measured on the dev branch (supersedes the numbers below)

* **Tree:** a084e52d with all 196 current `patches/emutos/*.patch`
  (0001-0196, through "the romdisk is R:, and D: is the disc on both
  boots"). They applied cleanly; HEAD is `585140e0`.
* **Build:** same toolchain and workarounds as §0 (`-fno-ivopts
  -fno-tree-slsr`, `bios/kprint.c` compiled by hand). The image is 236,614
  bytes.
* **What changed since the old series:** only 9 files differ, all from
  patches 0191-0196: `segacd.c`, `segacd.h`, `scdapi.h`, `disk.c`,
  `machine.h`, `screen.c` (Setcolor only), `geminit.c`, `deskapp.c` and
  `vdi_mouse.c`. `biosmem.c`, `fsbuf.c`, the screen allocation and
  `bios.c` are unchanged.

### The new numbers

| Item | Old series | Dev branch |
|---|---|---|
| `.bss` | $2140-$14FCF | $2140-$14FE3. segacd.o is now 17,266 B (+18): `scd_pal[16]`, raster and romdisk state. |
| `__endvdibss` | $CBF8 | $CC0A |
| MEMBOT (`end_os`) | $14FD0 | **$14FE4** (+20 B). RAM used: 85,988 B. |
| GEMDOS buffers | to $1581F | $14FE4-$15833 (2,128 B, unchanged) |
| TPA | $15820-$55FFF, 264,160 B | **$15834-$55FFF, 264,140 B**. `docs/ports.md` measures 264,154 B from AUTO and 263,608 B from the desktop on the real build. |
| Screen | $58000 (+8 KB slack below) | unchanged: $58000-$5FFFF |
| phystop | $60000 | unchanged |
| C: ramdisk | $60000 + image length | Unchanged, on both boots. `boot/m1emu.S:394-402` plants it at $60000 in Mode 1; `sp.S` does the same on a disc boot. The default is $1C000, ending at $7C000. |
| $7C000-$7FFFF | timeshare, CD buffer, BURAM, bounce, CDSECT | unchanged |

### Correction to §1

§1 said a launched GEM app gets "about 235 KB" because the AES `gl_tmp`
buffer sits below it. The measurement in `docs/ports.md` (263,608 B from
the desktop) shows that the desktop costs almost nothing.

### Romdisk R:

* **Where the data lives:** cartridge ROM, from cart offset $080000 up to
  $200000 (`ROMDISK_MAX_SECTORS` 3072). It is not in sub RAM.
* **How it is read:** the servant copies one sector at a time into the
  existing bounce buffer at sub $7F000 (cart op 11, shared with S:). The
  sub-side state is a `romdisk_sectors` word in BSS.
* **Mode 1:** R: exists when the ROM carries an "EmuT" FAT image.
* **Disc boot:** the servant refuses op 11, so R: does not exist.
  D: is the disc on both boots.
* **Effect on memory:** R: adds nothing above phystop.

### New users of $20000-$20FFF

* **Fixed users: none.** I grepped every added line in the whole series
  for `0x2xxxx`. The only hit is the comment `0x080000..0x200000`, which
  is a cart ROM address.
* **New dynamic users (patches 0192, 0193, 0195):**
  * **What they write:** the kernel writes the palette block (`PAL!`) at
    `Physbase()+32000` and the raster block (`RST!`) at `+32240`. The
    servant reads the pointer, scroll and blit blocks beside the shown
    screen, and it reads an `SCD_RASTER` table wherever the program keeps
    it.
  * **Why they could touch the range:** all of these follow the
    *physical* screen. A program that `Setscreen`s its own buffer, such as
    Cyber Paint or the NEOchrome launcher, gets these writes up to
    32,768 B past the buffer start.
  * **When they hit $20000-$20FFF:** only if a TOS program puts its own
    screen in roughly $18000-$20FFF. GEOS itself never does, and neither
    does the desktop or any AUTO program.
  * **Bank rule:** a screen still may not straddle a 128 KB bank, and
    $20000 is a bank boundary.

### Does the bios.c reservation still hold?

* **Where:** yes. `autoexec()` is still at **`bios/bios.c:1135`**. It now
  comes right after the `#ifdef MACHINE_SEGACD` dirty-framebuffer probe
  at lines 1128-1134, so insert the reservation between line 1134 and
  line 1135.
* **Margin:** MEMBOT moved by only 20 B, leaving about 42 KB of margin
  below $20000.
* **The cost is larger than the 4 KB itself.** Pexec takes the *largest*
  free block. A reservation at $20000 leaves two blocks:
  $15834-$1FFFF (42,956 B) and $21000-$55FFF (217,088 B).
  * So the largest program drops from about 264 KB to **about 212 KB**
    (−46 KB), not −4 KB.
  * NEOchrome is 204 KB with its BSS, and its launcher also needs to space
    its screens. It would be on the edge.
* **Programs parked at the memory limit (`docs/ports.md`):**
  * **Cyber Paint** needs about 400 KB: 185 KB of image and BSS plus five
    32 KB screens and a 64 KB frame pair. It has 79 KB of pool in a
    264 KB TPA.
  * **Dungeon Master** needs about 225 KB of heap, plus its 166 KB image
    and an 8 KB stack, and gets 89 KB of heap plus the 80 KB bulk arena.
    It is about 135 KB short.
  * Both are already far beyond reach, so the reservation does not change
    their status. It does move them further from any future fix.
* **Recommendation, revised:**
  * Keep the mechanism (c1): Malloc with a pad from `initial_basepage` at
    `bios.c:1134/1135`.
  * Make it conditional, for example on a GEOS boot flag or file. The
    general-purpose boot would then keep its single 264 KB block.
  * Alternatively, rely on (b) or (c2): an AUTO TSR shipped only on the
    GEOS disc, or GEOS.PRG claiming the range itself.

## 0. How this was established (old series 0001-0190; superseded by the section above)

* Upstream EmuTOS was cloned to a scratch checkout of upstream EmuTOS, and the
  branch `segacd` was checked out at base commit
  `a084e52da9556baf1470b2ffc865af2377923d37`. That commit comes from
  `patches/emutos/README.md:16`; the superproject does not record a
  submodule gitlink.
* `git am patches/emutos/0*.patch` applied all 190 patches cleanly. HEAD is
  `75230e6c "Sega CD: there is no warm boot here..."`.
* **Build: succeeded** with the distro `m68k-linux-gnu-gcc 13.3`, using
  three workarounds:
  `make segacd ELF=1 TOOLCHAIN_PREFIX=m68k-linux-gnu- OPTFLAGS="-Os -fno-ivopts -fno-tree-slsr -Wno-error=format"`,
  plus a hand-compiled `bios/kprint.c` without `-Werror=format`.
  * The ICEs (`immed_wide_int_const_1`) occur in the ivopts and slsr
    passes. Disabling those two passes avoids them.
  * The kprint error comes from a ptrdiff_t/`%lx` mismatch under `-mshort`.
  * Code size is not representative of the real m68k-elf build: this
    build's image is 235754 bytes, while the shipped release was 230570
    bytes at patch 0181. **BSS layout does not depend on code generation**,
    so the RAM map below is reliable.
  * Do not flash this image: its libgcc is 68020 code.
  * The map file is `emutos.map` of that build (not kept).
  * The build reports:
    `TEXT=0x00080000 STKBOT=0x00000800 LOWSTRAM=0x00001000 BSS=0x00002140 MEMBOT=0x00014fd0` (RAM used: 85968 bytes).
  * These numbers agree with the project docs ("its .bss starts at 0x2140",
    `docs/bram-filesystem.md`).

File:line references below are into the patched tree
(a scratch checkout of upstream EmuTOS) unless prefixed with the project path.

## 1. Sub-CPU memory map at runtime (release build, no SCD_DIAG)

*Old series. On the dev branch, `.bss` ends at $14FE3, MEMBOT is $14FE4
and the TPA is $15834-$55FFF (264,140 B); everything from the screen up is
unchanged. See the top section.*

| Range | What | Source |
|---|---|---|
| $000000-$0003FF | 68000 vectors (EmuTOS-owned; the CDBIOS is evicted) | `emutos.ld:75-81`, PLAN.md §3 |
| $000400-$0007FF | TOS sysvars at their real addresses: `phystop`=$42E, `membot`=$432, `memtop`=$436, `v_bas_ad`=$44E, `nvbls`=$454, `vblqueue`=$456, `hdv_bpb`=$472, `hdv_rw`=$476, `hdv_mediach`=$47E, `themd`=$48E, `drvbits`=$4C2, `vbl_list[8]`=$4CE, `end_os`=$4FA | `tosvars.ld:42-91`, `emutos.ld:79` |
| $000800-$000FFF | BIOS supervisor stack, 2 KB (`_stkbot`/`_stktop`) | `emutos.ld:94-99` |
| $001000-$00213F | `.low_stram`: lowstram.o ($D38) and lineavars.o ($408) | `emutos.ld:106-115`, map:460-466 |
| $002140-$014FCF | `.bss` (77.7 KB). The largest contributors are listed after this table. | map:2162-2524 |
| $014FD0 | `__end_os_stram` → `end_os` = `membot` (static) | `bios/biosmem.c:66-67` |
| $014FD0-$01581F | BDOS sector buffers: `balloc_stram(2*NUMBUFS*(20+512))` = 2128 bytes, from the bottom. `max_sect_siz` = 512, because every volume is 512 B/sector (`tools/mkfat.py:23`). | `bdos/fsbuf.c:56-62` |
| **~$015820** | **runtime `membot` = `themd.m_start` = start of TPA** | `bios/biosmem.c:145` |
| $015820-$055FFF | **GEMDOS TPA, 264,160 bytes, a single free block** | `bios/biosmem.c:143-150` |
| $056000-$057FFF | 8 KB "slack" below the screen. This is a probe for an unidentified writer that overruns `memtop` on Mode 1 boots. | `bios/screen.c:653-661` |
| $058000-$05FFFF | Screen: `balloc_stram(32K+8K, top)`. It holds 32000 bytes of ST-low bitmap, followed by the servant's palette, scroll, blit and cursor blocks. The servant follows `v_bas_ad` every VBL (`bios/segacd.c:4528`) but requires that the screen not straddle a 128 KB bank (`iofw/main.c:2882`). | `bios/screen.c:785-792`, `bios/segacd.c:52-56` |
| $060000 | `phystop` (`CONF_STRAM_SIZE` 384K) | `include/config.h` (segacd block), `bios/memory.S:219` |
| $060000-(+len) | C: ramdisk. Its length is read from its own boot sector; the default `ADISK_SIZE` of $1C000 makes it end at exactly $7C000. | `bios/segacd.c:4535-4560`, `tools/build-iso.sh:454` |
| ramdisk end-$7BFFF | "Bulk arena" (`SCD_BULK_INFO`). It is **0 bytes with the default ramdisk** and 80 KB with the 32 KB datadisc ramdisk. Its start address varies. | `bios/segacd.c:3806,3950`, `docs/payload.md:149-160` |
| $07C000-$07C06F | Timeshare state block (a5), plus the CDD packet at $7C060 | `bios/segacd2.S:385-413,491` |
| ..-$07C8FF | Scratch stack for CDBIOS visits (top at $7C900) | `bios/segacd2.S:388,490` |
| $07C900-$07CFFF | Apparently unused (about 1.75 KB) | — |
| $07D000-$07EFFF | CDBIOS ROMREADN buffer: `CD_TS_SECTORS`=4 × 2048 = **8 KB** | `bios/segacd.c:2882-2920` |
| $07E000-$07E6FF | BURAM manager work area, names and call block (`BRM_SCRATCH`). **This overlaps the CD buffer above** (see §5). | `bios/segacd.c:4681-4697` |
| $07F000-$07F77F | Sector bounce buffer for the backup RAM and cart proxy. The servant reaches it at window offset $1F000 with bank 3. | PLAN.md:500 |
| $07F780 | CD report block (`CDREPORT`) | `bios/segacd.c:1954` |
| $07F800-$07FFFF | Captured CD sector (`CDSECT`) | `bios/segacd.c:1173-1178` |
| $080000-~$0B8800 | EmuTOS image in Word RAM (`ROM_ORIGIN`). It must end below $BA000, a limit of 237568 bytes. | `emutos.ld:23-24`, `docs/bram-filesystem.md:236` |
| $0B9F00↓ | Scratch stack for BIOS visits. It grows down into the image headroom. | `bios/segacd2.S:177,238` |
| $0BA000-$0BFFFF | Parked Sega CDBIOS. The 24 KB at $0-$5FFF is exchanged with it on each visit. | `bios/segacd2.S:153-268` |

The largest `.bss` contributors are:

| Object | Address | Size |
|---|---|---|
| segacd.o | $35F8 | 17,248 bytes |
| geminit.o (AES global `D`) | $DA58 | 16,108 bytes |
| osmem.o (GEMDOS OS pool) | $8114 | 7,828 bytes |
| vdi_fill.o | $B2DE | 6,160 bytes |
| cmdasm.o | $13CAE | 4,112 bytes |

The VDI BSS ends at `__endvdibss` $CBF8.

Other fixed allocations:

* **There is no static allocation for the AES or the desktop.** Everything
  the AES and desktop allocate at runtime comes from the TPA via
  `dos_alloc_*` (Malloc).
* **The data segment is in Word RAM.** EmuTOS's `.data` is supposed to be
  empty (`emutos.ld:138-143`).
* **`balloc_stram` makes no other calls on this machine.** The FRB cookie is
  off because `CONF_ATARI_HARDWARE=0` (`include/config.h:775`). The screen
  and fsbuf are the only two allocations it makes.

What sits in the TPA at runtime is dynamic. Every program is loaded with
Pexec, which takes the largest free block (`bdos/proc.c:460-476`, `ffit(-1)`).
Malloc is first-fit, lowest address first (`bdos/iumem.c:88-97`).

* **Boot order.** The order is:
  1. AUTO programs, run from the initial process `initial_basepage` (static)
     (`bios/bios.c:1135`).
  2. The AES process. Its basepage is shrunk to 256 bytes
     (`aes/gemstart.S:57-68`).
  3. The accdesk process. Its basepage is shrunk to 256 bytes
     (`aes/gemstart.S:139-152`).
  4. Accessories (`aes/geminit.c:687`).
  5. `gsx_malloc` for the menu and alert save buffer, 640 chars × 32 B =
     **20,480 bytes** (`aes/gemgsxif.c:97-113`, `aes/gemshlib.c:236`).
  6. The desktop, as its own process (`aes/gemshlib.c:600-610`).
* **When a GEM app is launched,** the desktop process terminates and its
  memory is freed (`desk/deskmain.c:2146-2181`, `aes/gemshlib.c:645`).
* **Where the app lands.** With no AUTO TSRs and no ACCs, a GEM .PRG gets a
  basepage at roughly **$1AB00** *(superseded: ports.md measures 263,608 B of TPA from the desktop, so `gl_tmp` is not below the app)* (membot + about $300 for the env and
  basepages + 20 KB `gl_tmp`). Its TPA runs to about $56000, about 235 KB.
  A TOS or TTP program lands about 20 KB lower, because `sh_toalpha` frees
  `gl_tmp` (`aes/gemshlib.c:258`).
* **$20000 therefore falls about 21 KB inside the launched app's own TPA**
  in the default configuration. Accessories, AUTO TSRs or a Malloc-ing
  shell can move that boundary above $20000.

## 2. Is $20000-$20FFF used by anything fixed?

**No.** The checks behind that answer:

* **Fixed addresses in the code.** I grepped segacd.c, segacd2.S and
  segacd.h for every fixed address. Below phystop they use only the
  vectors, the sysvars and the 24 KB exchange window at $0-$5FFF. All other
  fixed scratch is at $7C000 or above, or in Word RAM.
* **The CDBIOS.** The CDBIOS visits exchange only $0-$5FFF, and they read
  into $7D000.
* **The main CPU.** The servant only touches sub PRG-RAM through the
  $020000 *main-side* window. It uses bank 3 for the ramdisk, bounce buffer
  and bulk arena, and the screen's bank (2) for the screen. Nothing uses
  bank 1 (sub $20000-$3FFFF) except as the screen's bank, and only if
  someone `Setscreen`s there. Note that the main-side window address
  $020000 has nothing to do with sub address $20000.
* **The boot loaders.** They leave nothing resident there.
  `memory_reused` clears `_warm_magic` (patch 0190), and there is no warm
  boot.
* **The OS's static end.** It is about **$15820** in the current release
  build. That leaves 42 KB of margin below $20000; SCD_DIAG adds a few
  hundred bytes of BSS.
  * **Guard needed:** this is a build-dependent number, so any scheme
    should assert it, for example `membot < 0x20000` at boot.

## 3. Ways to reserve $20000-$20FFF

### (a) Raise membot to $21000 at boot (EmuTOS patch)

* **Where:** `bios/biosmem.c:66-67` (`bmem_init`), or a
  `balloc_stram(0x21000-membot, FALSE)` after `bufl_init`
  (`bdos/bdosmain.c:328`).
* **Cost:** $21000 − $15820 = **47,072 bytes of TPA lost** (about 18% of the
  TPA).
* **Cheaper variant:** move the 40 KB screen allocation to the bottom, in
  `screen_init_address`, `bios/screen.c:661`, using `balloc_stram(..., FALSE)`.
  The screen would then sit at about $15820-$1F81F. Raise membot to $21000
  and set memtop = phystop = $60000.
  * **Cost:** about 6 KB (a 2 KB gap plus the 4 KB table).
  * **Fragile:** the BSS end has to stay below $20000 − 40 KB.
  * **Risky:** it moves the target of the unexplained Mode-1 "writer past
    memtop" (`bios/screen.c:653-660`) into live TPA. It also puts the
    screen in bank 0, which the servant supports
    (`iofw/main.c:2801-2802`) but has never run.
* **Verdict:** not recommended.

### (b) TSR or ACC that Mallocs a block

* **Naive version:** this does not guarantee the address.
* **Fix:** use first-fit to steer the allocation. EmuTOS's Ptermres keeps
  **every block the TSR owns**, not just its TPA (`bdos/proc.c:72-89,624-633`).
* **The AUTO TSR (`GEOSRES.PRG`) sequence:**
  1. `Mshrink` itself.
  2. `p = Malloc(16); Mfree(p)` to find the lowest free address.
  3. `pad = Malloc(0x20000 - p)`.
  4. `jt = Malloc(0x1000)` and check that it **== $20000**.
  5. `Mfree(pad)`.
  6. `Ptermres(own size)`.
* **Why it is deterministic:** at AUTO time the free list is a single block
  starting just above membot (about $15900), so the addresses are known.
* **Cost:** about 4 KB, plus the TSR's basepage and code. *(Superseded: the largest free block also drops to about 212 KB; see the top section.)*
* **Side effect:** the free list splits into a lower block of about 42 KB
  (below $20000) and an upper block of about 212 KB (from $21000 to $56000).
  AES `gl_tmp` and other small Mallocs fall into the lower block. Pexec
  (ACCs, desktop, GEOS) takes the upper one, so little is lost in practice.
* **Failure modes:** the TSR must run before any other resident AUTO
  program, and it should refuse loudly if `jt != 0x20000`.
* **An ACC is worse:** ACCs load after the AES has allocated memory and run
  as the accdesk child process.

### (c) Other options

* **(c1) In-kernel version of (b) — recommended if an EmuTOS patch is
  acceptable.**
  * **Where:** in `bios/bios.c` just before `autoexec()` (line 1135),
    under `#ifdef MACHINE_SEGACD`.
  * **What:** the same pad/Malloc/Mfree sequence, run from
    `initial_basepage`. That process never terminates, so the 4 KB block
    is owned forever.
  * **Robustness:** it uses only public GEMDOS calls and does no MD
    surgery. It works whatever is in AUTO, and costs exactly 4 KB.
  * **Size:** about 40 bytes of image. The Word RAM headroom is tight but
    this fits.
  * **Rejected alternative:** a second static free MD in `getmpb()`
    (`bios/biosmem.c:143-150`). It also works, but a static MD must be
    followed by a word of −1, or `xmfremd()` (`bdos/osmem.c:235-239`) will
    treat it as pool-owned.
* **(c2) No patch and no TSR: the GEOS program claims the range itself.**
  * When GEOS.PRG starts, check that `basepage + 256 + text + data + bss <
    $20000` and that `p_hitpa >= $21000`. Write the table at $20000 and
    `Mshrink(basepage, $21000 - basepage)`, so anything it Pexecs lands
    above $21000.
  * It works in the default configuration, where the app loads at about
    $1AB00. It breaks once ACCs or TSRs push the load point past about
    $1E000.
  * **Assessment:** good as a fallback check, not as the primary mechanism.

### Alternative homes for a fixed 4 KB

These only matter if the GEOS ABI address could move.

* **Word RAM between the image end and $BA000:** about 5-7 KB at best. It is
  shared with the visit stack at $B9F00, and each EmuTOS feature eats into
  it; the docs record 168 bytes at one point. **Not viable.**
* **$7C000-$7FFFF:** fully assigned (see the table). The only free piece,
  $7C900-$7CFFF, is about 1.75 KB. **Not viable.**
* **Bulk arena:** 0 bytes with the default ramdisk. With a fixed smaller
  `ADISK_SIZE`, its tail just below $7C000 (for example $7B000-$7BFFF)
  would be a fixed block outside GEMDOS. That works, but it couples the
  GEOS ABI to the ISO build, and NATIVE.PRG payloads also claim the arena.

## 4. Supervisor mode, VBL queue, BIOS disk vectors, drive G:

* **Super/Supexec:** standard GEMDOS and XBIOS. There is no MMU, and the
  sub CPU has no user-mode bus error below $800. A user-mode program can
  write anywhere, including the gate array at $FF8000. Nothing in the port
  restricts it.
* **VBL queue:** standard. `nvbls`=8 and `vblqueue`→`vbl_list` at $4CE
  (`bios/bios.c:383-389`). `_int_vbl` walks it (`bios/vectors.S:436-460`)
  and returns with `rts`, because there is no Atari video
  (`bios/vectors.S:493-497`).
  * It is called at the end of the INT2 C handler
    (`bios/segacd.c:4296..4530`), raised by the main CPU once per frame and
    entered at **IPL 3** (`bios/segacd2.S:25-35`). That means level 4 (the
    CDD) can preempt a VBL routine.
  * `screenpt` and `colorptr` are not processed (that code is
    `CONF_WITH_ATARI_VIDEO`).
  * **Caveat:** CDBIOS visits (D: reads and BRAM calls) run with
    interrupts masked for up to about 5 s (watchdog), so VBL ticks are lost.
    A GEOS VBL client must tolerate gaps.
  * **Caveat:** the INT2 handler also carries mouse, keyboard and the
    screen-address publish, so keep queue routines short.
* **hdv_rw / hdv_bpb / hdv_mediach:** standard sysvars ($476/$472/$47E),
  reached through `protect_*` wrappers (`bios/bios.c:1342,1417,1481`). BDOS
  is unpatched by the project (`git diff --stat` touches no `bdos/` file).
  The usual chain-to-old-vector hook works.
* **Drive letters and patch 0164:** the narrowing in `disk_init_all`
  (`bios/disk.c:350-390`) only steers which bit `next_logical()` hands
  each **built-in** device at boot: C ramdisk, D disc, I internal BRAM,
  S cart SRAM.
  * Nothing masks `drvbits` afterwards. `blkdev_init` zeroes it once
    (`bios/blkdev.c:181`), and `disk_rescan` only touches its own unit's
    bits (`bios/disk.c:505-525`).
  * **A new letter is not filtered out.** A TSR or ACC that sets bit 6 of
    `_drvbits` and hooks the three vectors gets a working **G:**
    (`BLKDEVNUM`=26, `include/sysconf.h:43`).
* **G: constraints:**
  1. **BPB `recsiz` must be ≤ 512.** The BDOS buffers were sized at boot
     from `max_sect_siz`=512 (`bdos/fsbuf.c:61-62`, `bios/blkdev.c:159`),
     and a larger sector would overrun them.
  2. **The G: icon depends on when the TSR runs and on EMUDESK.INF.**
     * An AUTO TSR sets the bit before the desktop starts.
     * An ACC also sets it before the desktop starts, because the AES waits
       for ACCs to initialise first (`aes/geminit.c:740`).
     * With no INF, the desktop builds icons from `drvbits` and G: gets
       `IG_HARD` (`app_drive_icon` default).
     * Once `I:\EMUDESK.INF` exists, icons come from it, so the user must
       do "Install devices" and then "Save desktop" once.
  3. **Do not do I/O from a VBL routine while a CD visit could be in
     progress.**

## 5. Side findings, not in scope

**CD buffer overlaps the BURAM work area.**
* The CD read buffer is `CD_TS_SECTORS(4) × CDC_DATA_BYTES(2048)` = 8 KB at
  $7D000, so it covers $7E000-$7EFFF (`bios/segacd.c:2882,2917`).
* That range overlaps `BRM_SCRATCH` = $7E000.
* The guard is
  `#if CD_TS_SECTORS * 512 > (BRM_SCRATCH - 0x7D000UL)`
  (`bios/segacd.c:4686`). It multiplies by 512 instead of 2048, so it never
  fires.
* This is harmless if the BURAM work area is rebuilt on every call. It is a
  bug if the BURAM manager expects its work RAM to persist between calls.

**Mode 1 overrun.** The unexplained Mode-1 overrun past memtop into the
screen (`bios/screen.c:653-660`) is still open. It has nothing to do with
$20000, but any scheme that moves the screen inherits it.

## Recommendation

1. **Use (c1).** A small EmuTOS patch, `0191`, carves $20000-$20FFF out of
   the TPA with Malloc in `bios.c` just before `autoexec()`, owned by the
   never-exiting initial process. It should check the result and fall back
   with a visible event or panic if `membot >= $20000`. Cost is 4 KB, and
   the reservation is deterministic, independent of AUTO and ACC contents,
   and survives desktop and AES restarts.
2. **If EmuTOS must not change, use (b)** as the first AUTO program
   (`AUTO\GEOSRES.PRG`, which sorts first alphabetically). The steps are
   the same; cost is about 4.3 KB.
3. **In either case, GEOS.PRG should also verify at startup** that $20000
   holds its reserved block, for example with a magic number written by
   the reserver, and refuse to run otherwise.
4. **Do not raise membot** (−46 KB). **Do not use Word RAM or $7C000+**
   (no room).
5. **G: via an ACC or TSR hooking hdv_* is feasible.** Use 512-byte
   sectors, and remember that the desktop icon needs the INF to be updated.
