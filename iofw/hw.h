/* Genesis + Sega CD hardware access for the main-CPU IOFW. */
#ifndef HW_H
#define HW_H

#include <stdint.h>

#define VU8(a)  (*(volatile uint8_t  *)(a))
#define VU16(a) (*(volatile uint16_t *)(a))
#define VU32(a) (*(volatile uint32_t *)(a))

/* VDP */
#define VDP_DATA   0xC00000u
#define VDP_CTRL   0xC00004u
#define VDP_HVCNT  0xC00008u

#define VDP_ST_VBLANK 0x0008

/* control-port address commands */
static inline uint32_t vdp_vram_w(uint16_t a)
{ return 0x40000000u | ((uint32_t)(a & 0x3FFF) << 16) | (a >> 14); }
static inline uint32_t vdp_cram_w(uint16_t a)
{ return 0xC0000000u | ((uint32_t)(a & 0x3FFF) << 16) | (a >> 14); }
#define VDP_DMA_BIT 0x00000080u   /* CD5 in the second command word */

static inline void vdp_reg(uint8_t r, uint8_t v)
{ VU16(VDP_CTRL) = 0x8000 | ((uint16_t)r << 8) | v; }

/* Gate array (main side) */
#define GA_RESET   0xA12000u   /* IFL2/sub reset control */
#define GA_MEMMODE 0xA12003u   /* BK1:0 in bits 7:6, MODE/DMNA/RET low */
#define GA_COMFLG_M 0xA1200Eu  /* main->sub flag byte */
#define GA_COMFLG_S 0xA1200Fu  /* sub->main flag byte */
#define GA_CMD0    0xA12010u   /* main->sub words */
#define GA_CMD1    0xA12012u
#define GA_CMD2    0xA12014u   /* (dx<<8)|dy for the EmuTOS mouse */
#define GA_CMD3    0xA12016u   /* mouse buttons: bit1 left, bit0 right */
#define GA_STAT0   0xA12020u   /* sub->main words */
#define GA_STAT1   0xA12022u
#define GA_CART_LBA 0xA12024u  /* sub->main: cart request sector */
#define GA_CART_REQ 0xA12026u  /* sub->main: (op<<8)|seq  op 1=rd 2=wr */
#define GA_CART_ACK 0xA12018u  /* main->sub: echoed done seq (low byte) */
#define GA_CART_CNT 0xA1201Au  /* main->sub: B: sector count, 0 = none */

/* Set by a Mode 1 cartridge loader before it jumps here, at an address
 * below IOFW's own relocation target so nothing here overwrites it.
 *
 * On a CD boot the sub CPU is running boot/sp.S and IOFW drives it: it
 * hands over Word RAM and issues command 5 to make the SP load EmuTOS.
 * On a Mode 1 boot there is no SP -- the cartridge has already planted
 * EmuTOS and started the sub on it -- so that handshake has nobody to
 * answer it and IOFW spins in `while (!VU8(GA_COMFLG_S))` forever. */
/* In the loader's own page, 0xFF0900..0xFF09FF, with the marks and
 * STAGE. It was at 0xFF0140 -- inside CDSECT_WRAM, the 2048-byte disc
 * sector buffer at 0xFF0000 -- and this flag is read in the main loop,
 * so a sector landing on it would have changed how the servant behaves
 * halfway through a run. The Mode 1 loader's marks were under the same
 * buffer, which is why they went blank partway through every boot. */
#define M1_FLAG    0xFF09A0u
#define M1_MAGIC   0x4D314F4Bu  /* 'M1OK' */
#define GA_BRAM_CLAIM 0xA1201Eu /* main->sub: 0x0C1A = reformat internal BRAM */
#define GA_KEY     0xA1201Cu  /* main->sub: (atari_scancode<<8)|seq */

/* The cartridge's save RAM, which is in two entirely different places
 * depending on how the machine booted -- and for the same reason
 * PRG_WINDOW is. In Mode 2 the cartridge sits in the Mega CD's own
 * cart slot at 0x400000-0x7FFFFF, behind an official three-register
 * protocol. In Mode 1 the cartridge IS the boot device: it is a plain
 * Genesis cart at 0x000000, its save RAM is the ordinary odd-byte
 * window at 0x200001, and 0x400000-0x7FFFFF is where the Mega CD went.
 *
 * Using the Mode 2 addresses on a Mode 1 boot fails quietly and early.
 * 0x400001 is the second byte of the CD boot ROM's first vector, which
 * is 0xFF on all three regional BIOSes in vendor/bios -- and the size
 * id is the low three bits, so it reads 7, and the probe's first line
 * rejects anything above 6. cart_sectors stayed zero and there was
 * simply no cartridge drive on a Mode 1 boot.
 *
 * Worth writing down because the failure is so tidy: nothing crashed,
 * nothing was corrupted, and B: went on working -- it fell through to
 * the console's internal backup RAM, which is where the diagnostic's
 * log lives, so the machine looked entirely correct. The only symptom
 * was a drive that was never there.
 *
 * (An earlier version of this comment claimed the servant had spent
 * every boot proxying Word RAM as a fake B:, with a sector count. That
 * was wrong -- read off misaligned telemetry, and contradicted by the
 * probe bailing on its first line. 0x600001 is indeed Word RAM, so the
 * addresses had to be fixed either way, but nothing ever reached it.)
 *
 * Mode 2 (backup RAM cart; official protocol, PicoDrive-confirmed) */
#define CART_ID_M2   0x400001u /* odd: size id, size = 8192 << id */
#define CART_DATA_M2 0x600001u /* odd bytes: byte i at CART_DATA + 2*i */
/* Write enable, bit 0. The register answers across the whole
 * $700000-$7FFFFF page -- that is how Genesis Plus GX decodes it,
 * cd_cart.c mapping pages 0x70..0x7F to the same handler -- but every
 * document and every real program uses $7FFFFF, and an emulator's
 * forgiving partial decode is exactly the kind of difference that shows
 * up only on hardware.
 *
 * This branch had $700001 here, which is the emulator-forgiving one,
 * and the cost was not one wrong address: cart_step_apply()'s rungs 1
 * and 5 write CART_WP_M2 while 3 and 4 write $700001 literally, so with
 * both naming the same register the ladder had four distinct rungs
 * instead of six and never wrote $7FFFFF at all. */
#define CART_WP_M2   0x7FFFFFu
/* Mode 1 (ordinary Genesis save RAM; boot/m1tool.S proves both on
 * hardware). Bit 1 of the control register is write PROTECT, not a
 * second enable: 0x01 works and 0x03 silently rejects writes. */
#define CART_DATA_M1 0x200001u
#define CART_CTL_M1  0xA130F1u
/* 63 sectors, not 64. The window this project's own ROM header
 * declares is 32 KB (0x200001..0x20FFFF, odd-byte) and boot/m1emu.S
 * keeps the last 512 bytes for its own boot report -- the only thing
 * that can speak when a boot fails before this servant exists. Written
 * down rather than probed: a plain Genesis cart cannot report its size
 * the way the Mega CD protocol lets one, so the loader read-back tests
 * both ends and publishes the count before the sub can ask. */
#define CART_SECTORS_M1 63u

extern uint32_t cart_data;
#define CART_DATA cart_data

/* cart sector bounce buffer: top of PRG bank 3, past the A: ramdisk */
#define BOUNCE_SUB    0x7F000u  /* sub-side direct address */
#define BOUNCE_WOFF   0x1F000u  /* offset within the 128K window (BK=3) */
#define CART_BANK     3u

/* CD sector capture, in the last 2K of the same bank, with a sixteen-byte
 * report just below it. The comm registers were full, so this is where
 * the sub says what the disc read is doing. */
#define CDREPORT_WOFF 0x1F780u  /* status words, see cdr_report() */
#define CDREPORT_WORDS 36u
#define CDSECT_WOFF   0x1F800u  /* 2048 bytes of sector */
#define CDSECT_BANK   3u
/* report word 7: one bit per sector verified, plus how it went wrong */
#define CDR_V_BAD     0x40u
#define CDR_V_NOREAD  0x80u

/* CDD trace: what the CDBIOS said to the drive during the boot reads,
 * recorded by boot/cddtrace.S inside the SP's image in PRG bank 0 and
 * fetched from there before the BIOS is evicted. Found by its tag, so
 * the two sides need not agree on an address -- only on the layout. */
#define CDTRACE_ENTS  60u
#define CDTRACE_SIZE  32u       /* keep in step with cddtrace.S */
#define CDTRACE_TAG0  0x43445452u       /* "CDTR" */
#define CDTRACE_TAG1  0x41434531u       /* "ACE1" */
#define CDTRACE_SCAN0 0x6000u   /* the SP is linked at sub 0x6000 */
#define CDTRACE_SCAN1 0xC000u
#define CDTRACE_WRAM  0xFFEE00u /* mirror, above the planar cache */

/* PRG-RAM window: 128K, bank selected by BK bits.
 *
 * Where it appears depends on how the console was booted, and that is
 * not a detail. On a CD boot -- Mode 2 -- it is at 0x020000. On a Mode
 * 1 boot, with a cartridge in the slot, everything the Mega CD exposes
 * moves up by 0x400000 and the window is at 0x420000.
 *
 * Getting this wrong is silent and convincing. 0x020000 in Mode 1 is
 * inside the cartridge's own ROM, so reads succeed and return
 * something that changes plausibly from address to address -- in this
 * case the EmuTOS image the ROM carries, which is why the screen filled
 * with 4E75, 4CEE, 4FEF0020 and looked so much like memory corruption.
 * Writes went to ROM and vanished. A whole session of measurements were
 * taken through this window and every one of them described the
 * cartridge rather than the framebuffer. */
/* An all-zero tile, so nothing is drawn and whatever is behind shows
 * through. VRAM is wiped at init, so this tile is blank by having
 * never been written. Both the converter and the on-screen keyboard
 * want it. */
#define TILE_BLANK  1000u

#define PRG_WINDOW_M2 0x020000u
#define PRG_WINDOW_M1 0x420000u
extern uint32_t prg_window;
#define PRG_WINDOW prg_window

/* I/O ports */
#define IO_DATA1   0xA10003u
#define IO_DATA2   0xA10005u
#define IO_CTRL1   0xA10009u
#define IO_CTRL2   0xA1000Bu

/* E1 comm protocol (values in GA_CMD0 / GA_STAT0) */
#define E1_MAGIC      0x1E51   /* sub engine announces itself in STAT0 */
#define E1_CMD_BENCH  0x0001   /* run 100 full-screen conversions */
#define E1_CMD_STREAM 0x0002   /* continuous render+convert */
#define E1_ST_BENCH_DONE 0x2222

/* Layout inside the shared PRG bank 3 (sub 0x60000, main window base) */
#define FB_TILES_OFF  0x0000   /* 25 tile rows x 40 x 32 bytes = 32000 */

#endif
