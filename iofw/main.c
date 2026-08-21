/* IOFW v2 — the EmuTOS display/input servant (runs from work RAM).
 *
 * The sub CPU runs stock EmuTOS, drawing an authentic ST-low planar
 * screen at 0x58000 (its standard top-of-RAM allocation, inside PRG
 * bank 2). Each vblank this firmware:
 *   1. raises INT2 on the sub (EmuTOS's VBL),
 *   2. bus-requests the sub and copies+diffs one chunk of the planar
 *      screen through the window into a work-RAM cache (SBRQ held only
 *      for the copy),
 *   3. converts the changed 16px groups from the cache to VDP tiles
 *      (no bus contention — cache is local),
 *   4. samples the pad into the comm registers (input path, E3).
 * Full-screen sweep every 8 vblanks; text-sized updates land in one.
 */
#include "hw.h"

/* TILE_BLANK moved to hw.h: osk.c needs it too, for the row the
 * television cuts off. */
/* An opaque cell of ST colour 0, on plane B, under the whole 200-line
 * screen. See vdp_init: it is what lets the backdrop be black without
 * the desktop's paper going black with it. Pixel value 2, palette line
 * 1, so no ST colour index is involved and nothing on the ST side can
 * reach it. Tiles 0..999 are the screen and 1024+ are the keyboard's,
 * so 1001 is free. */
#define TILE_PAPER  1001u
#define PAL_OURS    0x2000u     /* nametable bits: palette line 1 */
#define NAMETABLE   0xC000u
#define WINDOWTAB   0xB000u     /* must match osk.c's WIN_NAMETAB */
#define PLANEB      0xE000u
#define HSCROLL     0xFC00u
#define SATBASE     0xF800u

#define REPORT      0xFF0F00u   /* host-visible debug block */
#define CACHE       0xFF7000u   /* 32000-byte planar cache */
#define CDSECT_WRAM 0xFF0000u   /* 2048 bytes: the sector read off the disc */
#define CDSTAT_WRAM 0xFF0800u   /* sixteen bytes: what the sub says about it */

/* The three-row status letterbox under the ST screen. Off: the display
 * is 224 lines, the ST screen is 200, and with nothing in the remaining
 * 24 the picture is a 320x200 screen centred in the frame with a border
 * of ST colour 0 above and below -- which is what a television shows an
 * ST. The code is kept because it is this port's only instrument on a
 * console with no serial port; build with -DIOFW_HUD=1 to get it back,
 * and remember that it takes the bottom three tile rows with it. */
#ifndef IOFW_HUD
#define IOFW_HUD 0
#endif

/* Lines of blank frame above the ST screen. 224 - 200 = 24, so 12 and
 * 12: not a whole number of tile rows, which is why this is a vertical
 * scroll of plane A and not a different nametable origin. */
#define SCREEN_YOFF 12u

#define SCREEN_BANK 2u          /* PRG bank holding 0x40000-0x5FFFF */
#define SCREEN_WOFF 0x18000u    /* 0x58000 within bank 2 */
#define GA_BUSREQ   0xA12001u
#define GA_IFL2     0xA12000u

static void sub_bus_grab(void)
{
    VU8(GA_BUSREQ) = 0x03;
    while (!(VU8(GA_BUSREQ) & 0x02)) ;
}
static void sub_bus_release(void)
{
    VU8(GA_BUSREQ) = 0x01;
}

/* Grab the sub bus only if the sub is not mid-conversation with the CD
 * drive.
 *
 * Halting it there is not a delay, it is a corruption: the drive's gate
 * array clocks a fixed exchange every 1/75 s, and a bus grab lasting
 * milliseconds drops the sub out of that window entirely. Reads survive
 * arriving late; a command does not, which is why the link has been
 * perfect while passive and has died at the first command on every
 * build regardless of what that command was or how it was written.
 *
 * Checked twice, because the sub can enter the exchange between the two
 * checks: once before asking, and once after the grant, when it is too
 * late for the sub to have started. Losing a frame of screen copying
 * costs a little repaint latency and nothing else. */
/* Set when the pump has just come back from a refusal long enough to
 * have hidden a whole picture. The diff only converts a tile whose bytes
 * differ from the cache, so anything EmuTOS paints and then paints over
 * while the pump is locked out is never seen: the cache reads the same
 * before and after, no tile is marked, and the picture is simply lost.
 * That is what happens to the welcome screen -- it is drawn and cleared
 * inside a firmware visit, and only the periodic full re-sweep, five
 * seconds later, ever gets it onto the tube. By then the four-second
 * timer has expired and the desktop is up. */
static uint8_t pump_resync;

static int sub_bus_grab_polite(void)
{
    static uint8_t skipped;
    /* Its own counter, saturating: 'skipped' is a byte compared against
     * 600 and wraps long before it gets there. */
    static uint16_t lost;

    /* ...but never forever. Four frames was the most politeness the
     * display could afford back when the sub held its busy bit on
     * every exchange; now the bit means a foreground sector read is in
     * flight, and a screen frame sacrificed to a torn header costs far
     * more than it buys -- the torn header was how the ring ended up
     * pointing at one sector while the buffer held another. Half a
     * second of politeness during a read, four frames otherwise. */
    /* 600 while the sub holds the interlock, not 30. The interlock
     * used to be held for a couple of milliseconds at a time (a CDC
     * transfer), and 30 frames of patience was a wedge-guard. It is now
     * also held across a whole firmware visit -- seconds, watchdog-
     * bounded at 25 -- and grabbing after half a second of that puts
     * this CPU's halt right back into the middle of the firmware's
     * drive exchange, which is the thing the interlock exists to
     * prevent. A frozen status line during a visit is the cost of the
     * drive actually answering; the wedge-guard remains, ten seconds
     * out. */
    /* 600, exactly as v24 ran on the console: D: opening in one shot
     * and files reading correctly. The cut to 90 was meant to tame a
     * TV click and instead killed D: -- a forced grab 1.5 s into a
     * visit is a knife in every visit that needs longer, and the first
     * ones do. v24 is the baseline; the click gets investigated as its
     * own change, not bundled. */
    if (skipped < ((VU16(0xA12022) & 0x0008) ? 600 : 4)) {
        if (VU16(0xA12022) & 0x0008) {
            skipped++;
            if (lost < 0xFFFFu) lost++;
            return 0;
        }
        sub_bus_grab();
        if (VU16(0xA12022) & 0x0008) {
            sub_bus_release();
            skipped++;
            if (lost < 0xFFFFu) lost++;
            return 0;
        }
    } else {
        sub_bus_grab();
    }
    /* Eight frames is one full sweep of the screen. Anything shorter
     * can only have hidden part of one chunk, which the next sweep
     * picks up; anything longer can have hidden the whole picture
     * changing and changing back. */
    if (lost >= 8u)
        pump_resync = 1;
    lost = 0;
    skipped = 0;
    return 1;
}

/* ---- the printer ------------------------------------------------------
 *
 * EmuTOS is on the sub CPU and the serial port is on this one, so
 * Bconout(0) cannot reach the wire. What crosses instead is not the
 * bytes but the address of them: the sub fills a page in its own PRG
 * RAM, sends where it is and how long, and this side streams it out.
 *
 * The frame is GEOS-Genesis's, because the thing on the far end of the
 * wire is its Pico and that firmware is what knows PCL:
 *
 *     [0xA5][mode][len_hi][len_lo][payload...][checksum]
 *
 * checksum is the 8-bit sum of everything after the sync. Mode 0 is
 * text, and the Pico prints it and feeds the page -- so one frame is
 * one page, which is why the sub buffers a page rather than streaming
 * bytes.
 *
 * Nothing is buffered here. The header, the payload and the checksum
 * are generated as they go, a byte at a time, from a window into the
 * sub's own memory. A page is four kilobytes and this machine has
 * twenty-four for all of its code. */
/* Its own block, because REPORT is crowded and both slots the first
 * version of this used -- 0xFF0F3A and 0xFF0F3C -- are the cart probe's,
 * so the counters read back numbers another line had written. That is
 * the second time this exact thing has happened here; see the note above
 * WATCH. 0xFF0D40 up is clear. */
#define PRN_STAT        0xFF0D40u
/*  +0  long  'PRNT'
 *  +4  word  bytes in the page being sent
 *  +6  word  pages completed
 *  +8  word  jobs abandoned by the watchdog
 *  +10 word  (state << 8) | the UART's enable flag                     */

#define PRN_SYNC        0xA5u
#define PRN_CHUNK       64u             /* bytes fetched per bus grab */

uint8_t uart_send(uint8_t b);           /* uart.c */
uint8_t uart_active(void);
void uart_enable(uint8_t on);

static uint8_t  prn_state;              /* 0 idle, else the field being sent */
static uint8_t  prn_bank;
static uint32_t prn_base;               /* window address of the page */
static uint32_t prn_len;                /* bytes in it */
static uint32_t prn_pos;                /* how many have gone */
static uint8_t  prn_sum;
static uint8_t  prn_buf[PRN_CHUNK];
static uint16_t prn_have, prn_take;     /* bytes in prn_buf, and consumed */
static uint16_t prn_stall;              /* frames the job has not moved */
static uint16_t prn_pages;              /* telemetry: pages sent */

enum { PRN_IDLE = 0, PRN_SYNCB, PRN_MODE, PRN_LENH, PRN_LENL,
       PRN_BODY, PRN_CKSUM };

/* Refill prn_buf from the sub's memory. One bus grab, one bank switch,
 * up to sixty-four bytes -- rather than one of each per byte, which at
 * four kilobytes a page would be four thousand grabs. */
void prn_fill(void);
void prn_fill(void)
{
    uint32_t left;
    uint16_t n;
    uint8_t save;
    uint16_t i;

    if (prn_state != PRN_BODY || prn_take < prn_have)
        return;                         /* nothing wanted yet */
    left = prn_len - prn_pos;
    n = (uint16_t)((left > PRN_CHUNK) ? PRN_CHUNK : left);
    if (!n || !sub_bus_grab_polite())
        return;
    save = VU8(GA_MEMMODE);
    VU8(GA_MEMMODE) = (uint8_t)((save & ~0xC0u) | (prn_bank << 6));
    for (i = 0; i < n; i++)
        prn_buf[i] = VU8(prn_base + prn_pos + i);
    /* Back to whatever was selected, rather than to the screen's bank
     * by name: this runs from inside wait_vblank and has no business
     * knowing what the caller was in the middle of. */
    VU8(GA_MEMMODE) = (uint8_t)((VU8(GA_MEMMODE) & ~0xC0u) | (save & 0xC0u));
    sub_bus_release();
    prn_have = n;
    prn_take = 0;
}

/* Offer the port one byte of the frame. Returns 0 when there is nothing
 * to send or the transmitter is full, so the caller can stop asking. */
static uint8_t prn_step(void)
{
    uint8_t b;

    switch (prn_state) {
    case PRN_IDLE:  return 0;
    case PRN_SYNCB: b = PRN_SYNC;                       break;
    case PRN_MODE:  b = 0;                              break;  /* text */
    case PRN_LENH:  b = (uint8_t)(prn_len >> 8);        break;
    case PRN_LENL:  b = (uint8_t)prn_len;               break;
    case PRN_CKSUM: b = prn_sum;                        break;
    default:
        if (prn_take >= prn_have)
            return 0;                   /* wait for the frame's refill */
        b = prn_buf[prn_take];
        break;
    }

    if (!uart_send(b))
        return 0;

    switch (prn_state) {
    case PRN_SYNCB: prn_state = PRN_MODE;                    break;
    case PRN_MODE:  prn_sum += b; prn_state = PRN_LENH;      break;
    case PRN_LENH:  prn_sum += b; prn_state = PRN_LENL;      break;
    case PRN_LENL:  prn_sum += b;
                    prn_state = prn_len ? PRN_BODY : PRN_CKSUM; break;
    case PRN_CKSUM: prn_state = PRN_IDLE; prn_pages++;
                    VU16(PRN_STAT + 6u) = prn_pages;         break;
    default:
        prn_sum += b;
        prn_take++;
        if (++prn_pos >= prn_len)
            prn_state = PRN_CKSUM;
        break;
    }
    return 1;
}

uint8_t prn_busy(void) { return (uint8_t)(prn_state != PRN_IDLE); }

/* A job that cannot move must not become a job nothing can follow.
 *
 * Called once a frame. If the state machine has not advanced in ten
 * seconds -- the wire is 480 bytes a second and the longest page is
 * four kilobytes, so nine seconds is the honest worst case for a whole
 * page, let alone one byte -- the job is abandoned and the next one is
 * accepted. Without this, a port that never empties leaves the servant
 * refusing every page for the rest of the session, which is exactly
 * what an unenabled UART did. */
static uint32_t prn_seen;

static void prn_watchdog(void)
{
    VU16(PRN_STAT + 10u) = (uint16_t)((prn_state << 8) | uart_active());
    if (prn_state == PRN_IDLE) {
        prn_stall = 0;
        return;
    }
    if (prn_pos != prn_seen || prn_stall == 0) {
        prn_seen = prn_pos;
        prn_stall = 1;
        return;
    }
    if (++prn_stall > 600) {
        prn_state = PRN_IDLE;
        prn_stall = 0;
        VU16(PRN_STAT + 8u)++;          /* jobs abandoned */
    }
}

/* Waiting for the blank is the only idle this program has, and a
 * printer is exactly the thing to spend it on: the wire takes a byte
 * every two milliseconds and a frame is sixteen, so the gap between
 * finishing the frame's work and the blank arriving is worth about
 * eight bytes -- which is the whole of what 4800 baud can carry. The
 * pump loses nothing, because it had already stopped. */
static void wait_vblank(void)
{
    while (VU16(VDP_CTRL) & VDP_ST_VBLANK)
        prn_step();
    while (!(VU16(VDP_CTRL) & VDP_ST_VBLANK))
        prn_step();
}

/* ST 3-bit RGB (0x0RGB) -> Genesis CRAM.
 *
 * A CRAM word is 0000 BBB0 GGG0 RRR0: red in bits 1-3, green in 5-7,
 * blue in 9-11, and bits 0, 4, 8 and 12 ignored. So each 3-bit ST
 * channel shifts by one, five and nine.
 *
 * It shifted by two, six and ten for the whole life of this port, which
 * put each channel's top bit into the ignored bit above its field and
 * threw it away: eight ST levels per channel arrived as four, every
 * colour half as bright as intended and every second pair of shades
 * identical. It was invisible because the desktop was the only thing
 * that ever set a colour and it looked plausible. The palette test
 * asked for eight distinct reds, the frame came back with four, and
 * that is what a test is for. */
static uint16_t st2cram(uint16_t st)
{
    uint16_t r = (st >> 8) & 7, g = (st >> 4) & 7, b = st & 7;
    return (uint16_t)((b << 9) | (g << 5) | (r << 1));
}

/* Plane A's cell-to-tile mapping: one tile per screen cell across the
 * 40x25 the ST screen occupies, the blank tile everywhere else.
 *
 * Its own function because anything that writes text into plane A --
 * the trace screen -- destroys it, and blanking those rows afterwards
 * is not a repair. The converter goes on writing tile patterns into the
 * tiles the mapping used to point at, quite happily and quite
 * invisibly, and the console shows an empty backdrop while EmuTOS runs
 * perfectly well behind it. */
static void screen_nametab(void)
{
    uint16_t row, col, idx = 0;
    VU32(VDP_CTRL) = vdp_vram_w(NAMETABLE);
    for (row = 0; row < 32; row++)
        for (col = 0; col < 64; col++) {
            if (row < 25 && col < 40) VU16(VDP_DATA) = idx++;
            else                      VU16(VDP_DATA) = TILE_BLANK;
        }
}

static void vdp_init(void)
{
    /* EmuTOS default ST-low palette (TOS order) */
    static const uint16_t stpal[16] = {
        0x777, 0x700, 0x070, 0x770, 0x007, 0x707, 0x077, 0x555,
        0x333, 0x733, 0x373, 0x773, 0x337, 0x737, 0x377, 0x000
    };
    uint16_t i;

    /* Wipe VRAM: the Sega CD BIOS leaves its own fonts, logo tiles and
     * nametables behind. */
    vdp_reg(1, 0x04);                       /* display off while we wipe */
    VU32(VDP_CTRL) = vdp_vram_w(0);
    for (i = 0; i < 32768u; i++) VU16(VDP_DATA) = 0x0000;

    vdp_reg(0, 0x04);
    vdp_reg(1, 0x54);
    vdp_reg(2, NAMETABLE >> 10);
    vdp_reg(4, PLANEB >> 13);
    vdp_reg(5, SATBASE >> 9);
    vdp_reg(7, 0x10);           /* backdrop: palette line 1, entry 0 */
    vdp_reg(11, 0x00);
    vdp_reg(12, 0x81);
    vdp_reg(13, HSCROLL >> 10);
    vdp_reg(15, 2);
    vdp_reg(16, 0x01);
    vdp_reg(17, 0x00);          /* window plane: no horizontal extent */
    vdp_reg(18, 0x00);          /* window plane: no vertical extent */

    VU32(VDP_CTRL) = vdp_cram_w(0);
    for (i = 0; i < 16; i++) VU16(VDP_DATA) = st2cram(stpal[i]);

    /* Palette line 1 is ours: nothing on the ST side addresses it.
     *
     * Entry 0 is what register 7 points the backdrop at, so the frame
     * around the screen is black rather than ST colour 0. Entry 1 is
     * the diagnostic screen's green (osk_diag_init writes the same two
     * again, later and harmlessly). Entry 2 is ST colour 0 itself,
     * which is what TILE_PAPER draws -- a colour, not a transparency,
     * which is the whole point.
     *
     * Why the paper has to be drawn rather than left to the backdrop:
     * the ST's colour 0 becomes tile pixel value 0, and pixel 0 is
     * transparent on this VDP whatever the palette says. Every white
     * space on the desktop is therefore the backdrop showing through.
     * Turn the backdrop black to get a black frame and the desktop's
     * paper turns black with it. So plane B carries an opaque copy of
     * colour 0 under the 200 lines that are the screen, and the
     * backdrop is left visible only in the 24 lines that are not. */
    VU32(VDP_CTRL) = vdp_cram_w(2 * 16);
    VU16(VDP_DATA) = 0x0000;                /* 0: black, the frame */
    VU16(VDP_DATA) = 0x00E0;                /* 1: green, the diagnostic */
    VU16(VDP_DATA) = st2cram(stpal[0]);     /* 2: ST colour 0, the paper */

    /* One tile, every pixel value 2. */
    VU32(VDP_CTRL) = vdp_vram_w(TILE_PAPER * 32u);
    for (i = 0; i < 16; i++) VU16(VDP_DATA) = 0x2222;


    screen_nametab();

    /* Plane B and the window plane must be transparent everywhere.
     * Colour 0 is transparent on this VDP, and the ST background *is*
     * colour 0, so whatever sits on the lower plane shows through the
     * paper of every text page. A nametable of zeroes means every cell
     * of it draws tile 0 -- the screen's top-left character cell -- so
     * a console page tiled the entire display with its first letter:
     * 'E' from "EmuTOS...", 'W' from "Welcome to EmuCON". Point both
     * planes at the blank tile instead; the backdrop (ST colour 0)
     * then shows through as the paper it should be. */
    /* Plane B: the paper, and only where the screen is. Rows 0..24 are
     * the 200 lines of ST screen -- the same rows plane A puts the
     * picture on, and plane B is scrolled with it -- so this lines up
     * at screen lines 12..211 and nowhere else. Everything outside it
     * stays transparent on both planes, which is where the black gets
     * in. Columns 40..63 are not displayed in H40. */
    {
        uint16_t row, col;
        VU32(VDP_CTRL) = vdp_vram_w(PLANEB);
        for (row = 0; row < 32u; row++)
            for (col = 0; col < 64u; col++)
                VU16(VDP_DATA) = (row < 25u && col < 40u)
                                 ? (uint16_t)(PAL_OURS | TILE_PAPER)
                                 : (uint16_t)TILE_BLANK;
    }
    VU32(VDP_CTRL) = vdp_vram_w(WINDOWTAB);
    for (i = 0; i < 64u * 32u; i++) VU16(VDP_DATA) = TILE_BLANK;

    VU32(VDP_CTRL) = vdp_vram_w(HSCROLL);
    VU16(VDP_DATA) = 0;
    /* VSRAM 0 is plane A, 1 is plane B. The VDP adds the scroll value
     * to the screen line to index the plane, so moving the picture DOWN
     * by twelve lines is a scroll of minus twelve. Plane A wraps at 256
     * lines (reg 16 = 0x01 is 64x32 cells), so screen lines 0..11 come
     * from nametable rows 30 and 31 and lines 212..223 from rows 25 and
     * 26 -- all four of which screen_nametab() fills with TILE_BLANK,
     * and none of which anything writes to once the letterbox status
     * rows are gone. Plane B stays at zero: it is blank everywhere. */
    VU32(VDP_CTRL) = 0x40000010u;
    VU16(VDP_DATA) = (uint16_t)-(int16_t)SCREEN_YOFF;   /* plane A */
    VU16(VDP_DATA) = (uint16_t)-(int16_t)SCREEN_YOFF;   /* plane B, with it */

    /* park the sprite table: one dummy entry, off-screen */
    VU32(VDP_CTRL) = vdp_vram_w(SATBASE);
    VU16(VDP_DATA) = 0; VU16(VDP_DATA) = 0;
    VU16(VDP_DATA) = 0; VU16(VDP_DATA) = 0;
}

/* Planar->tile conversion. One table per bitplane, indexed by a whole
 * source byte: each entry is the 8 pixels of a finished tile row with
 * that plane's bit set. Four lookups and three ORs produce a tile row,
 * against sixteen lookups for the nibble tables this replaces. */
uint32_t tab8[4][256];          /* not static: convert.S indexes it */
static void tab_init(void)
{
    uint16_t p, b, i;
    uint32_t v;
    for (p = 0; p < 4; p++)
        for (b = 0; b < 256; b++) {
            v = 0;
            for (i = 0; i < 8; i++)
                if (b & (0x80u >> i))
                    v |= (uint32_t)(1u << p) << (28 - 4 * i);
            tab8[p][b] = v;
        }
}

/* Convert one whole 8x8 tile from the cache straight into VRAM.
 *
 * Converting a tile at a time rather than a scanline at a time is what
 * makes this affordable: a tile's 32 bytes are contiguous in VRAM, so
 * one address command covers all eight rows. Per-scanline conversion
 * needed an address command for every row of every tile, and address
 * commands were two thirds of the VDP traffic -- which is the real
 * bottleneck, not the arithmetic, because every access during active
 * display waits for a VRAM slot.
 *
 * Source bytes for a tile are one per plane at stride 2, and the next
 * row is 160 bytes on. Even columns are the high byte of each plane
 * word, odd columns the low byte. */
void scd_tile(const uint8_t *src, uint32_t vdpcmd);   /* convert.S */

static void convert_tile(uint16_t trow, uint16_t tcol)
{
    const uint8_t *s = (const uint8_t *)(CACHE
        + (uint32_t)trow * 1280u          /* 8 lines x 160 bytes */
        + (uint32_t)(tcol >> 1) * 8u      /* 16px group */
        + (tcol & 1));                    /* which half of it */
    uint16_t tile = (uint16_t)(trow * 40u + tcol);
#ifdef DIAG_BENCH_NOVDP
    uint8_t r;
#endif

#ifdef DIAG_BENCH_NOVDP
    /* Same arithmetic, work RAM instead of the VDP. Subtracting this
     * from the normal benchmark is how the CPU/VDP split was measured:
     * 22.0 of the 23.8 frames a full screen costs are arithmetic, and
     * only 1.8 are the VDP. Blanking the display changes nothing, so
     * neither core charges for slot contention during active display --
     * real hardware will, so treat 1.8 as a floor for the VDP share. */
    {
        volatile uint32_t *d = (volatile uint32_t *)0xFF0C00u;
        (void)tile;
        for (r = 0; r < 8; r++) {
            *d = tab8[0][s[0]] | tab8[1][s[2]]
               | tab8[2][s[4]] | tab8[3][s[6]];
            s += 160;
        }
    }
#else
    scd_tile(s, vdp_vram_w((uint16_t)(tile << 5)));
#endif
}

/* Full-screen conversion benchmark. Build with -DDIAG_BENCH; add
 * -DDIAG_BENCH_BLANK to run it with the display off, or
 * -DDIAG_BENCH_NOVDP to run the arithmetic with no VDP writes at all.
 * The result lands in REPORT+0x30 as frames per eight full screens. */
/* A watch block of my own, at 0xFF0D00, because REPORT is crowded and
 * the last attempt landed on 0xFF0F3C which IOFW was already using --
 * so a field read back a value some other line had written and I spent
 * a while believing it.
 *
 *   0xFF0D00  'WTCH', so a zero here means this never ran at all
 *   0xFF0D04  frames: increments every pass of the main loop
 *   0xFF0D06  the sub's CDD tick counter, live
 *   0xFF0D08  the highest tick value seen
 *   0xFF0D0A  the frame at which it last moved
 *   0xFF0D0C  the sub's boot trace, live -- the loader's copy is frozen
 *             eight samples after release and says nothing about later
 *   0xFF0D0E  the highest boot trace seen
 *
 * Validated on a CD boot, which reaches a desktop, before being
 * believed on a Mode 1 boot that does not.
 */
#define WATCH 0xFF0D00u

static void cdd_watch(void)
{
    uint16_t t = VU16(0xA1202Cu);   /* 0x26 is CART_REQ */
    uint16_t tr = VU16(0xA12020u);

    VU32(WATCH) = 0x57544348u;          /* 'WTCH' */
    VU16(WATCH + 4) = (uint16_t)(VU16(WATCH + 4) + 1);
    VU16(WATCH + 6) = t;
    if (t > VU16(WATCH + 8)) {
        VU16(WATCH + 8) = t;
        VU16(WATCH + 10) = VU16(WATCH + 4);
    }
    VU16(WATCH + 12) = tr;
    if (tr > VU16(WATCH + 14))
        VU16(WATCH + 14) = tr;
    /* The drive's own status nibble, which segacd.c publishes here.
     * On every CD boot it reads 1, 2 or 4 -- the firmware has already
     * moved the drive. A cold Mode 1 boot has never been measured, and
     * state 0 is the one cd_state_action() treats as a refusal. */
    VU16(WATCH + 20) = VU16(0xA12028u);
    VU16(WATCH + 28) = VU16(0xA12022u);   /* the sub's busy word */
    {   /* sticky: the VDI's screen clear, which is over in a few
         * frames and would be gone by the time anything looked. */
        uint16_t x = VU16(0xA1202Eu);
        if ((x & 0xF000u) == 0xB000u) VU16(WATCH + 30) = x;
        if ((x & 0xF000u) == 0xC000u) VU16(WATCH + 40) = x;
        if ((x & 0xF000u) == 0xD000u) VU16(WATCH + 42) = x;
    }
    {   /* the CD read path, live and as a high-water mark */
        uint16_t c = VU16(0xA1202Eu);
        VU16(WATCH + 16) = c;
        if (c > VU16(WATCH + 18)) VU16(WATCH + 18) = c;
    }
}

#ifdef DIAG_BENCH
/* Count frames off the V counter, not the vblank status bit: with the
 * display disabled the status bit reads vblank permanently, so the same
 * clock has to work for both halves of this measurement. The counter's
 * mid-frame jump stays in the high half, so a high->low transition is
 * one frame. */
static uint16_t vbl_edges, hi_half;
static void vbl_tick(void)
{
    uint8_t v = (uint8_t)(VU16(VDP_HVCNT) >> 8);
    if (v >= 0x80) hi_half = 1;
    else if (hi_half) { hi_half = 0; vbl_edges++; }
}
#endif

uint16_t input_update(void); /* input.c: pad+mouse -> comm; returns pad */
uint16_t pad_read(void);     /* input.c: raw pad, no comm-register writes */
void osk_upload_tiles(void);
void osk_post_key(uint8_t sc);
uint8_t osk_slot_free(void);
void uart_poll(void);
uint8_t uart_next(void);
void uart_stats(uint16_t *rx, uint16_t *err, uint16_t *drop);
void osk_toggle(void);
uint8_t osk_active(void);
uint8_t osk_input(uint16_t pad);
extern uint8_t mouse_seen;   /* input.c */
static uint8_t a_held;       /* A at power-on: keep the CD link passive */
static uint8_t b_held;       /* B at power-on: give HOCK a real edge */
static uint8_t start_held;   /* Start at power-on: show the CDD trace */
static uint8_t down_held;    /* Down: the D: diagnostic, not the desktop */
static uint8_t up_held;      /* Up: run the boot-time sector self-test */
static uint8_t right_held;   /* Right: let the parked CDBIOS drive D: */
static uint8_t left_held;    /* Left: Sega's two-frame read lead */
void osk_diag_init(void);
void osk_row(uint16_t row, const char *s);
void osk_status(const char *s);
void osk_status2(const char *s);
void osk_status3(const char *s);

/* ---- backup RAM cartridge proxy ------------------------------------- */

static uint16_t cart_sectors;   /* 0 = no cart */
static uint8_t cart_swap_force_m2;      /* swap_watch(): use the Mode 2 window */
static uint8_t cart_probe_id;           /* the ID register, as read */
static void cart_probe(void);
uint32_t cart_data = CART_DATA_M2;      /* until the boot mode is known */

/* Call the cartridge's own backup RAM driver, function 0 (BRMINIT),
 * which answers with the size in d0 and a status in d1 and sets carry
 * on failure. Register use from megadev's lib/sub/bram.h, which is MIT
 * and agrees with what the BIOS dispatcher at $0070EE does with the
 * same arguments.
 *
 * Jumping into somebody else's ROM is not a hunch. The BIOS installs
 * this exact address as _BURAM at startup ($0003F6, when bit 7 of
 * $400001 is set and $400010 reads "RAM_CARTRIDG"), so the console runs
 * this code every time its own memory manager is opened. The signature
 * is checked before the call, and only a cartridge that answers it is
 * called.
 *
 * Why this branch needs it and the CD branch does not: on a CD boot the
 * cartridge is in the slot at power-on and the BIOS has already run
 * this. A cartridge pushed into a live Mode 1 machine has been
 * initialised by nothing at all -- so its memory is not necessarily
 * where the BIOS would later find it, and the ladder lands on whatever
 * is mapped instead. That is a probe that finds 1024 sectors of noise
 * from a cartridge whose filesystem is intact. */
/* The work area BRMINIT wants, borrowed from the planar cache rather
 * than taken out of the servant's own memory.
 *
 * Sixteen hundred bytes is a quarter of everything this program has in
 * BSS, and it is wanted once, at probe time, by a cartridge that
 * answers a signature -- which the only cartridge this has ever run
 * with does not. Keeping it as a static ran the BSS a kilobyte past
 * 0xFF7000, so the last variables declared -- the print job's whole
 * state machine, and uart_on, which is the serial keyboard's -- sat
 * inside the region the pump writes four thousand bytes of screen into
 * every frame. They were being set correctly and overwritten within
 * the frame, which is a thing that reads as almost anything.
 *
 * The cache is the right place to borrow: cart_probe() runs once at
 * startup, before the pump has converted anything, and the pump rebuilds
 * the cache from the framebuffer regardless. It is the same borrow a
 * payload makes, for the same reason. */
static uint8_t *const brm_work = (uint8_t *)CACHE;
static uint8_t brm_str[12];
uint16_t brm_size, brm_stat;
uint8_t  brm_carry, brm_called;

static void cart_brminit(void)
{
    register uint16_t r_d0 __asm__("d0") = 0;           /* BRMINIT */
    register uint16_t r_d1 __asm__("d1");
    register uint32_t r_a0 __asm__("a0") = (uint32_t)brm_work;
    register uint32_t r_a1 __asm__("a1") = (uint32_t)brm_str;
    uint16_t sr;

    brm_called = 1;
    __asm__ volatile(
        "movea.l #0x400020,%%a2\n\t"
        "jsr     (%%a2)\n\t"
        "move.w  %%sr,%2"
        : "+d"(r_d0), "=d"(r_d1), "=d"(sr)
        : "a"(r_a0), "a"(r_a1)
        : "a2", "a3", "d2", "d3", "cc", "memory");
    brm_size = r_d0;
    brm_stat = r_d1;
    brm_carry = (uint8_t)(sr & 1);
}

/* Write one byte to the cart's odd-byte data window. */
static void cart_wr(uint32_t off, uint8_t v)
{
    VU8(CART_DATA + off * 2) = v;
}
static uint8_t cart_rd(uint32_t off)
{
    return VU8(CART_DATA + off * 2);
}

/* Does this offset actually remember two different bytes?
 *
 * Open bus reads back plausibly and forgets, and so does ROM, so a
 * single write-and-compare is not enough on its own. Non-destructive:
 * whatever was there goes back. */
static uint8_t cart_holds(uint32_t off)
{
    uint8_t save = cart_rd(off);
    uint8_t ok = 1;

    cart_wr(off, 0x5A);
    if (cart_rd(off) != 0x5A) ok = 0;
    cart_wr(off, 0xA5);
    if (cart_rd(off) != 0xA5) ok = 0;
    cart_wr(off, save);
    return ok;
}

/* Something real to read between a write and the read back. It lives
 * in the servant's own memory, which is the only place guaranteed to
 * answer, and holding 0x00 means it differs from both test patterns.
 *
 * Not a read of $000000, which is where this went first: gcc turns a
 * dereference of a literal null into __builtin_trap() at -O2, so the
 * servant executed an illegal instruction and the console reset before
 * anything was drawn. */
static volatile uint8_t bus_flush;

/* Does the byte at this absolute address remember what is written to
 * it? Same test as cart_try, without the record, for sweeping. */
static uint8_t byte_holds(uint32_t a)
{
    uint8_t save = VU8(a), got1, got2;

    VU8(a) = 0x5A; (void)bus_flush; got1 = VU8(a);
    VU8(a) = 0xA5; (void)bus_flush; got2 = VU8(a);
    VU8(a) = save;
    return (uint8_t)(got1 == 0x5A && got2 == 0xA5);
}

/* Where the probe landed, and the first bytes there, written where the
 * sub CPU can read them.
 *
 * The rung was reported for three builds and the address never was,
 * which is the one thing that says whether we are reading the
 * cartridge's data window at all. A plain backup RAM cartridge keeps
 * its memory at $600001 and nowhere else -- $400010 reads back as
 * 00 06 00 06 on this hardware, the size-ID register mirrored, no ROM
 * and no signature -- so an address in any other page means we found
 * registers and called them a disk.
 *
 * Called on both exits from the Mode 2 probe. The first version was
 * only on the ladder's, so the read-only fast path reported an address
 * of zero. */
/* Is $600001 the cartridge, or the Mega CD's own Word RAM?
 *
 * In Mode 1 the main CPU sees Word RAM at $600000 -- boot/m1emu.S calls
 * it WRAM_MAIN and fills EmuTOS through it. After the /CART flip the
 * map is supposed to become Mode 2, putting Word RAM at $200000 and the
 * cartridge slot at $400000-$7FFFFF. If any part of that did not move,
 * $600001 is still Word RAM: real memory that holds whatever is written
 * to it, drifts between runs as the system uses it, formats perfectly,
 * reads back perfectly -- and is volatile, so it is empty after every
 * power cycle. Which is the reported fault exactly.
 *
 * Two writes settle it. Word RAM answers at $200001 in Mode 2, so if a
 * byte written at $600001 turns up at $200001 they are one memory and
 * the cartridge is not what we have been formatting. Both originals go
 * back. */
uint8_t cart_aliases_wram;      /* bit per candidate, see cart_check_alias */
uint8_t cart_size_aliases;      /* the top half is the bottom half */

/* What is actually answering at $600001?
 *
 * It is real memory: sixteen distinct bytes at sixteen addresses, so
 * not open bus and not a latch. It is not the cartridge, because the
 * cartridge still holds its filesystem when the same cartridge is read
 * after a clean power-on -- our Mode 1 writes never reached it. And it
 * is not Word RAM at $200001. So it is something, and naming it is the
 * whole question.
 *
 * One marker, written once, looked for everywhere it could plausibly
 * turn up: Word RAM, the PRG-RAM window, the boot ROM area, and the
 * cartridge region's other decode points. Whatever lights up is what we
 * have been formatting. Every original goes back. */
/* Word RAM is banked, and the first version of this looked at one bank.
 *
 * In 1M mode the 256K is two 128K halves, one owned by each CPU, so a
 * marker written at $600001 can be sitting in the half that $200001
 * does not show. That is not a small gap here: on a Mode 1 boot the
 * Mega CD lives at $400000-$7FFFFF with Word RAM at $600000 -- the
 * loader says so itself, WRAM_MAIN -- and the /CART flip moves it down
 * to $000000. If it does not stop answering at the old addresses then
 * $600001 has both the cartridge and Word RAM on it, and Word RAM is
 * volatile SRAM, which is exactly what the hardware has been showing:
 * a volume that writes, verifies, reads back and is gone after a power
 * cycle, replaced by a power-up fingerprint.
 *
 * Reads only, no bank switching and no bus grab. The attempt that
 * walked the PRG banks had to grab the sub bus and move GA_MEMMODE out
 * from under the pump, and it corrupted the display. */
static const uint32_t alias_at[8] = {
    0x200001ul,     /* bit 0  Word RAM, Mode 2, first half */
    0x020001ul,     /* bit 1  PRG-RAM window, Mode 2 */
    0x000001ul,     /* bit 2  boot ROM area */
    0x400001ul,     /* bit 3  the cart ID register */
    0x500001ul,     /* bit 4  midway through the cart region */
    0x700001ul,     /* bit 5  the other write-protect address */
    0x220001ul,     /* bit 6  Word RAM, Mode 2, second half */
    0x620001ul      /* bit 7  $600001 one bank up, in place */
};

static void cart_check_alias(void)
{
    uint8_t save[8], s6, i;

    s6 = VU8(0x600001ul);
    for (i = 0; i < 8; i++) save[i] = VU8(alias_at[i]);

    VU8(0x600001ul) = 0x3C;
    (void)bus_flush;
    cart_aliases_wram = 0;
    for (i = 0; i < 8; i++)
        if (VU8(alias_at[i]) == 0x3C)
            cart_aliases_wram |= (uint8_t)(1u << i);

    for (i = 0; i < 8; i++) VU8(alias_at[i]) = save[i];
    VU8(0x600001ul) = s6;
}

static void cart_publish_where(void)
{
    uint8_t save = VU8(GA_MEMMODE), i;

    sub_bus_grab();
    VU8(GA_MEMMODE) = (uint8_t)((save & ~0xC0u) | (CART_BANK << 6));
    VU8(PRG_WINDOW + 0x1F240u) = (uint8_t)(cart_data >> 24);
    VU8(PRG_WINDOW + 0x1F241u) = (uint8_t)(cart_data >> 16);
    VU8(PRG_WINDOW + 0x1F242u) = (uint8_t)(cart_data >> 8);
    VU8(PRG_WINDOW + 0x1F243u) = (uint8_t)cart_data;
    VU8(PRG_WINDOW + 0x1F244u) = cart_aliases_wram;
    VU8(PRG_WINDOW + 0x1F245u) = cart_size_aliases;
    for (i = 0; i < 8; i++)
        VU8(PRG_WINDOW + 0x1F248u + i) = VU8(cart_data + i * 2u);
    VU8(GA_MEMMODE) = (uint8_t)((VU8(GA_MEMMODE) & ~0xC0u)
                                | (SCREEN_BANK << 6));
    sub_bus_release();
}

/* Writes off unless we are writing.
 *
 * The probe set the write enable and nothing ever cleared it, so the
 * cartridge sat writable from the moment it was swapped in until the
 * power went off -- and a backup RAM cartridge left write-enabled while
 * the supply collapses is the textbook way to lose its contents. The
 * console does not do this: Sega's BIOS brackets each access, bset #0
 * at $008120 before and the protect restored after.
 *
 * It fits what the hardware reported. A volume formatted and opened,
 * then unreadable after a power cycle, coming back not as random noise
 * -- which is what a flat battery gives -- but as mostly the same bytes
 * with a scattering flipped, differing a bit or two between runs. That
 * is a memory that was live when the rails went down.
 *
 * Which register depends on where the cartridge is: $7FFFFF for the
 * Mega CD's slot, $A130F1 for a Mode 1 boot cartridge's own save RAM. */
static void cart_write_enable(uint8_t on)
{
    if (cart_data >= 0x400000ul) {
        /* Whole byte, not read-modify-write. The RMW version preserved
         * whatever the upper bits held -- and after a hot insert those
         * bits are power-up junk, different every session. On a clone
         * that banks its oversize SRAM through them, that junk chooses
         * which 512K view the session lives in: a live format works,
         * the data lands and persists, and the next session powers up
         * into a different view and cannot see the volume it wrote.
         * Which is the fault report exactly. The official protocol
         * defines bit 0 and nothing else, so forcing the rest to zero
         * costs a real cart nothing and gives a clone the same state
         * the CD branch gets from a clean power-on -- the state in
         * which the same cartridge demonstrably persists. */
        VU8(CART_WP_M2) = (uint8_t)(on ? 0x01u : 0x00u);
    } else {
        VU8(CART_CTL_M1) = (uint8_t)(on ? 0x01u : 0x00u);
    }
}

/* Sixteen different bytes at sixteen different addresses, read back as
 * a set. This is the test byte_holds() cannot do.
 *
 * byte_holds() writes one address and reads it back, with a work-RAM
 * read in between to disturb the bus -- and the disassembly confirms
 * that read is emitted, so it is a real test as far as it goes. It just
 * does not go far enough: a single latch anywhere on the path passes it
 * exactly as a RAM chip does, because both return the last byte
 * written. Only writing several addresses before reading any of them
 * can tell the two apart -- a latch has one value to give and will
 * hand back the newest one for every address.
 *
 * That distinction stopped being theoretical when a cartridge reported
 * 1024 sectors and every sector read back as noise that differed
 * between runs, with the signature absent and $400001 wandering from DE
 * to C6. Half a megabyte of memory does not behave like that; an
 * unselected cartridge's floating bus does.
 *
 * Sixteen is enough to be certain and small enough to leave no mark:
 * the originals go back afterwards. */
static uint8_t mem_holds16(uint32_t base)
{
    uint8_t save[16], got[16], i, ok = 1;

    for (i = 0; i < 16; i++) save[i] = VU8(base + i * 2u);
    for (i = 0; i < 16; i++) VU8(base + i * 2u) = (uint8_t)(0x5A ^ (i * 17u));
    (void)bus_flush;
    for (i = 0; i < 16; i++) got[i] = VU8(base + i * 2u);
    for (i = 0; i < 16; i++) VU8(base + i * 2u) = save[i];

    for (i = 0; i < 16; i++)
        if (got[i] != (uint8_t)(0x5A ^ (i * 17u))) { ok = 0; break; }
    return ok;
}

/* Sweep the whole cartridge window for anything that behaves like RAM,
 * once per rung of the ladder below.
 *
 * The first byte of each 128K of $400000-$7FFFFF, on both halves of the
 * bus, as two 32-bit maps. 128K granularity because the window is 4M,
 * thirty-two bits is eight hex digits, and memory too small to show up
 * in a 128K sweep is too small to be worth a drive letter. Sixty-four
 * write-read-restore pairs; every byte goes back. In Mode 2 there is
 * nothing in this region but the cartridge slot. */
#define CART_PAGE  0x20000ul
#define CART_STEPS 6

/* The ladder. Each rung does something to the cartridge and then looks
 * again, and they stack, so by the last rung everything has been tried.
 * Ordered least invasive first, so a cart that needs nothing is found
 * before anything has been poked at it.
 *
 *   0  as found          -- a cart with its memory simply mapped
 *   1  $7FFFFF |= 1      -- Sega's write enable, as the BIOS does it
 *   2  $A130F1 = 1       -- the Mega Drive mapper's SRAM enable, which
 *                           is what an ordinary save cart needs and what
 *                           the Mode 1 path has always done
 *   3  $700001 = 0       -- if the bottom of that page is a bank
 *   4  $700001 = 1          register rather than the protect register,
 *                           neither bank should go untried
 *   5  $7FFFFF = 1       -- a plain write, in case the read half of the
 *                           read-modify-write returns junk that sets
 *                           bits which turn the memory back off
 *
 * This replaces a build per hypothesis. Every one of these was going to
 * be its own disc otherwise, and the console is a long way from here. */
uint32_t cart_sweep_odd, cart_sweep_even;               /* the rung that won */
uint32_t cart_step_odd[CART_STEPS], cart_step_even[CART_STEPS];
uint8_t  cart_probe_step = 0xFF;                        /* which rung it was */

static void cart_step_apply(uint8_t step)
{
    switch (step) {
    case 0: break;                                      /* as found */
    case 1: VU8(CART_WP_M2) = (uint8_t)(VU8(CART_WP_M2) | 0x01); break;
    case 2: VU8(CART_CTL_M1) = 0x01; break;             /* $A130F1 */
    case 3: VU8(0x700001u) = 0x00; break;
    case 4: VU8(0x700001u) = 0x01; break;
    case 5: VU8(CART_WP_M2) = 0x01; break;
    }
}

static void cart_scan(void)
{
    uint8_t p;

    cart_sweep_odd = cart_sweep_even = 0;
    for (p = 0; p < 32; p++) {
        uint32_t page = 0x400000ul + (uint32_t)p * CART_PAGE;
        if (byte_holds(page + 1)) cart_sweep_odd  |= 1ul << p;
        if (byte_holds(page))     cart_sweep_even |= 1ul << p;
    }
}

/* Does this cartridge carry its own driver?
 *
 * The BIOS builds the main-CPU jump table at $FFFD06 on startup, and
 * the last entry it writes is _BURAM -> $0070EE, its own backup RAM
 * handler. Then, at $0003F6:
 *
 *     lea    $400001,a2
 *     tst.b  (a2)
 *     bpl    done            ; bit 7 clear: an ordinary cartridge
 *     lea    15(a2),a2       ; -> $400010
 *     moveq  #5,d1
 *   1 cmpm.w (a1)+,(a2)+     ; 12 bytes against "RAM_CARTRIDG"
 *     dbne   d1,1b
 *     bne    done
 *     move.l #$00400020,(a0) ; _BURAM -> the CARTRIDGE'S driver
 *
 * So a cart that sets bit 7 and carries that string replaces the BIOS's
 * backup RAM handler with a routine of its own at $400020, and its
 * memory is wherever that routine says it is. Nowhere in this file's
 * assumptions, which is why six rungs of ladder over four megabytes
 * found nothing while the console's own menu formatted the thing
 * without difficulty.
 *
 * $0080B0 agrees from the other side: its btst #7 calls bit 7 set "no
 * RAM", meaning no *plain* RAM at $600000 -- correctly, because on such
 * a cart there is none. */
uint8_t cart_sig[16];           /* $400010..$40001F, as read */
uint8_t cart_probe_wp0;         /* the write-protect register as found */
uint8_t cart_mem_real;          /* the window holds sixteen distinct bytes */
uint8_t cart_probe_ours;        /* ...and our own boot sector is on it */
uint8_t cart_entry[8];          /* $400020.., the driver's first bytes */
uint8_t cart_is_smart;

static void cart_read_sig(void)
{
    uint8_t i;
    static const char want[12] = { 'R','A','M','_','C','A','R','T','R','I','D','G' };

    for (i = 0; i < 16; i++) cart_sig[i] = VU8(0x400010ul + i);
    for (i = 0; i < 8; i++)  cart_entry[i] = VU8(0x400020ul + i);

    cart_is_smart = (uint8_t)((cart_probe_id & 0x80) ? 1 : 0);
    for (i = 0; i < 12; i++)
        if (cart_sig[i] != (uint8_t)want[i]) { cart_is_smart = 0; break; }
}

/* Climb until something answers. Returns the base address of the lowest
 * page that held, or 0 if the whole ladder came up empty. */
/* Is our own volume at this base? Both formatters -- FORMATS.PRG and
 * the servant's own -- lay "EmuTOS" down at byte offsets 2 to 7 of
 * sector zero, so it is ours if anything is. Not a guess about the
 * hardware: a look for a string this project wrote. */
static uint8_t cart_looks_ours(uint32_t base)
{
    static const char want[6] = { 'E','m','u','T','O','S' };
    uint8_t i;

    for (i = 0; i < 6; i++)
        if (VU8(base + (uint32_t)(2 + i) * 2u) != (uint8_t)want[i])
            return 0;
    return 1;
}

/* Climb the ladder, and prefer a rung that has our filesystem on it.
 *
 * This used to take the first rung that found any memory at all, and
 * within it the first 128K page that held a byte. On a machine where
 * the cartridge was set up at power-on that is the right answer,
 * because only one thing is mapped. On a cartridge pushed into a live
 * machine it is not: more than one page answers, and the first one is
 * not necessarily the save RAM. That produced a drive of 1024 sectors
 * whose sector zero read BF 93 8A 1A -- real memory, stable in places,
 * addressable, and not the volume anyone formatted.
 *
 * So every rung is swept, every page that holds is considered, and a
 * page carrying our own boot sector wins outright. Failing that the
 * first page that holds memory is still returned, exactly as before, so
 * a cartridge with no filesystem on it yet can still be found and
 * formatted. */
static uint32_t cart_climb(void)
{
    uint8_t step;
    uint32_t fallback = 0;

    for (step = 0; step < CART_STEPS; step++) {
        uint8_t p;

        cart_step_apply(step);

        /* Read-only pass, before anything is written.
         *
         * The loop below used to run cart_scan() first and only then
         * ask whether a page was ours -- which means it had already put
         * 5A and A5 into the first byte of every page, including the
         * one holding the boot sector, before looking at any of them.
         * Checking after writing is not checking.
         *
         * So each rung is now searched read-only first. Only if nothing
         * under it carries our volume does the destructive sweep run,
         * which is what an unformatted cartridge needs and what a
         * formatted one must never see. */
        for (p = 0; p < 32; p++) {
            uint32_t page = 0x400000ul + (uint32_t)p * CART_PAGE;
            uint8_t half;

            for (half = 0; half < 2; half++) {
                uint32_t base = page + (half ? 1u : 0u);
                if (cart_looks_ours(base)) {
                    cart_probe_step = step;
                    cart_probe_ours = 1;
                    return base;
                }
            }
        }

        cart_scan();
        cart_step_odd[step]  = cart_sweep_odd;
        cart_step_even[step] = cart_sweep_even;

        for (p = 0; p < 32; p++) {
            uint32_t page = 0x400000ul + (uint32_t)p * CART_PAGE;
            uint8_t half;

            for (half = 0; half < 2; half++) {
                uint32_t base = page + (half ? 1u : 0u);
                uint32_t map  = half ? cart_sweep_odd : cart_sweep_even;

                if (!(map & (1ul << p)))
                    continue;
                if (!fallback) {
                    fallback = base;
                    cart_probe_step = step;
                }
            }
        }
    }
    return fallback;
}

/* How big is the memory behind the data window, in 8192<<k bytes?
 *
 * Only asked when the ID register did not answer. A cart wires as many
 * address lines as it has memory and leaves the rest of the window
 * decoding to nothing, so offset 8192<<k lands back on offset 0 as soon
 * as k passes the last line the cart has. Writing a marker at each
 * power of two and watching offset 0 finds that fold; 512K is the
 * largest the window can address, so surviving every step means 512K.
 *
 * Two ways for the memory to end, and both have to be caught. A cart
 * that decodes only the lines it has folds the top of the window onto
 * the bottom, so offset 0 changes. A cart that decodes fully and simply
 * ignores what is out of range keeps offset 0 but does not keep the
 * marker either. Checking one and not the other over-reports, which
 * would put a filesystem on addresses that are not there.
 *
 * Every byte it touches goes back, offset 0 last so that its own value
 * wins wherever the fold aliased something onto it. The caller has
 * already established that the memory holds what is written to it, so
 * these restores are reliable in the case that matters -- and in the
 * case where they are not, there was no cart to preserve. */
static uint8_t cart_measure(void)
{
    uint8_t save0 = cart_rd(0);
    uint8_t saved[6];
    uint8_t k, n = 0, found = 6;

    cart_wr(0, 0x00);
    for (k = 0; k < 6; k++) {
        uint32_t off = 8192ul << k;     /* 8K 16K 32K 64K 128K 256K */
        uint8_t mark = (uint8_t)(0xC0 | (k + 1));

        saved[k] = cart_rd(off);
        n = (uint8_t)(k + 1);
        cart_wr(off, mark);
        if (cart_rd(off) != mark) { found = k; break; }  /* not memory */
        if (cart_rd(0) != 0x00) { found = k; break; }    /* folded back */
    }

    while (n--)
        cart_wr(8192ul << n, saved[n]);
    cart_wr(0, save0);
    return found;
}

/* ---- the cartridge swap ---------------------------------------------
 *
 * The Mega Drive decides its own memory map from /CART. A cartridge
 * that grounds it takes $000000-$3FFFFF and the expansion port -- the
 * Mega CD -- takes $400000-$7FFFFF. That is Mode 1, and it is why
 * PRG_WINDOW_M1 is $420000.
 *
 * A backup RAM cart does not ground /CART. So the moment the boot
 * cartridge leaves the slot the map flips: the Mega CD moves down to
 * $000000 and the cartridge window moves up to $400000, and it stays
 * that way, because everything swapped in afterwards is another backup
 * RAM cart. Like floppies: the drive changes contents, not identity.
 *
 * What breaks without this is immediate and total. The servant reads
 * the ST framebuffer through prg_window every frame, and prg_window is
 * $420000 -- which after the flip is an empty cartridge slot. One frame
 * later the screen is noise.
 *
 * Detection is self-calibrating, so no BIOS revision or region has to
 * be recognised: while the map is known to be Mode 1, take the first
 * longword of whatever sits at $400000 -- the Mega CD's boot ROM -- and
 * keep it. The flip has happened when that value is no longer at
 * $400000 and *is* at $000000. Both halves are required: a cartridge
 * being pulled makes the bus do strange things for a moment, and one
 * bad read either way should not move the window.
 *
 * The 68000's vectors move too, from the boot cartridge's to the Mega
 * CD's. The servant runs masked at 7 and takes no interrupts, so
 * nothing vectors through the gap. */
static uint32_t m1_boot_first;  /* our own cartridge's first long */
static uint8_t  swap_streak;    /* consecutive frames it has been missing */
uint8_t cart_swapped;           /* the map has flipped */
uint8_t cart_swap_seq;          /* bumped whenever the cartridge changes */

/* Reading $000000 needs the address to come from somewhere gcc cannot
 * fold: it turns a dereference of a literal null into __builtin_trap()
 * at -O2, which on this machine is an illegal instruction and a reset.
 * That cost a whole test disc once already. */
static volatile uint32_t addr_zero = 0x000000ul;

static uint32_t read_zero(void)
{
    return *(volatile uint32_t *)(uint32_t)addr_zero;
}

/* The anchor is our own ROM, not the Mega CD's.
 *
 * The first version watched $400000 for the Mega CD boot ROM to move
 * down to $000000. It read FFFFFFFF there and so did every later
 * comparison, because that region is not the boot ROM on every machine
 * -- Genesis Plus GX leaves it open bus in Mode 1, exposing only the
 * PRG window at $420000 -- and an anchor that is open bus can never
 * change.
 *
 * $000000 in Mode 1 is the boot cartridge: this ROM, a known value,
 * guaranteed present because it is the thing currently executing. Pull
 * it and that longword cannot still be there, whatever the Mega CD
 * puts in its place. One side, no assumptions about anybody else's
 * memory map.
 *
 * Three consecutive frames, because a cartridge coming out of a live
 * connector makes the bus do strange things on the way and a single
 * odd read should not move the framebuffer. */
static void swap_arm(void)
{
    m1_boot_first = read_zero();
}

/* Armed by SWAP.PRG through the cart request channel, never by the
 * boot. Nothing here happens on its own: a watcher that re-points the
 * framebuffer the moment a bus glitch looks like a missing cartridge
 * has no business running under a desktop that is otherwise working.
 * The program says when. */
uint8_t swap_armed;

/* Set between arming and the button: the cartridge is in motion and
 * nothing may touch $000000-$7FFFFF. See swap_watch(). */
static uint8_t swap_quiet;

static void swap_publish(void)
{
    cart_swap_seq++;
    VU16(GA_CART_CNT) = (uint16_t)((cart_sectors & 0x07FFu)
                                   | ((uint16_t)cart_swap_seq << 11));
}

/* The button is the detector. It is not a confirmation of one.
 *
 * Measured, finally, with the border heartbeat below: the machine died
 * on a pull while this code touched nothing below $800000 -- the
 * border stopped breathing. So the freeze was never in any build. It
 * is the hardware event itself: /CART bounces while the slot decides
 * who owns the bottom eight megabytes, and whatever that does to the
 * bus -- a scraped global line, the gate array's decode flapping
 * mid-cycle -- it reaches cycles this code never aimed at the
 * cartridge. A 68000 cannot stop fetching, so no code here can remove
 * the risk. What survives unchanged is everything below: keep the
 * exposure at zero anyway (no reason to add rolls to a losing game),
 * and let the one flip be taken at a moment a wedge costs nothing --
 * which is placement, SWAP.PRG's business, not detection.
 *
 * Pulling a cartridge swings /CART, and the address decode for the
 * whole bottom eight megabytes changes with it -- asynchronously, more
 * than once, while the contacts bounce. A 68000 cycle caught in that
 * window can get no DTACK from either side and wait for it forever.
 * That is the freeze, it is a dice roll per bus cycle, and it explains
 * a record that nothing else does: two builds pulled clean and five
 * did not, with no code difference between the clean ones and the rest
 * that could account for it. They were all rolling; some won.
 *
 * Every build so far read $000000 once a frame to notice the pull, and
 * all but one also read the framebuffer at $420000 -- four thousand
 * bytes of it -- every frame throughout. Cutting the second one down
 * to nothing still left twelve rolls across a two-hundred-millisecond
 * pull, and still froze. There is no safe number of rolls; there is
 * only zero.
 *
 * So between arming and the button this code touches nothing below
 * $800000. The pump stops (swap_quiet), the sector path stops (the
 * drive is declared empty, which the request handler already answers
 * without going near memory), and $000000 is not sampled at all. The
 * VDP, the pad, the gate array at $A1xxxx and work RAM are all decoded
 * elsewhere and carry on, INT2 included -- EmuTOS keeps its VBL, which
 * an earlier attempt at this took away and killed the machine with.
 *
 * The picture stops while the cartridge is out. That is the cost, it
 * is bounded by a person pressing a button rather than by a guess, and
 * SWAP.PRG says so before it happens.
 *
 * The button was added two builds ago to replace the detector and then
 * the detector was left in place beside it. This removes it. */
static void swap_watch(uint16_t pad)
{
    static uint8_t ready;       /* buttons released since arming */

    if (!swap_armed) {
        ready = 0;
        return;
    }

    if (!ready) {               /* ignore whatever was held on the way in */
        if (!(pad & 0xF0))
            ready = 1;
        return;
    }
    if (!(pad & 0xF0))          /* A, B, C or Start */
        return;

    if (!cart_swapped) {
        /* First press: the cartridge is out and the hand that pulled it
         * is on the pad, so the connector is at rest. This is the only
         * read of $000000 in the whole sequence. */
        if (read_zero() == m1_boot_first) {
            /* Still seated. Put everything back as it was and say so
             * -- the generation moves, the sector count does not. */
            cart_probe();               /* Mode 1: the boot cart's SRAM */
            swap_quiet = 0;
            pump_resync = 1;
            swap_publish();
            swap_armed = 0;
            return;
        }

        cart_swapped = 1;
        prg_window = PRG_WINDOW_M2;     /* the framebuffer, at its new home */
        cart_sectors = 0;               /* the slot is empty right now */
        swap_quiet = 0;                 /* $020000 is there whatever the slot */
        pump_resync = 1;                /* rebuild the cache from scratch */
        swap_publish();
        ready = 0;                      /* the next press is the insert */
        return;                         /* ...and we stay armed for it */
    }

    /* Second press: the new cartridge is in and holding still, which is
     * the only condition under which the ladder is safe to run.
     *
     * No reset pulse. One was tried here -- the 68000's RESET
     * instruction, to hand a hot-inserted controller cartridge the
     * config edge it only otherwise gets at power-on -- and it was
     * vetoed before it ever ran on hardware: it is machinery that can
     * take the session down, and this program's whole contract is that
     * it does nothing of the kind. Which closes the question honestly:
     * a cartridge whose controller configures on reset cannot be
     * brought up by software after a hot insert. This swap works with
     * plain SRAM cartridges, the kind that are wired straight to the
     * bus and have nothing to boot. */
    cart_swap_force_m2 = 1;
    cart_probe();
    cart_swap_force_m2 = 0;
    /* The probe borrows the planar cache for BRMINIT's work area, and
     * this is the one call to it that happens with the pump running --
     * the first press cleared swap_quiet and the picture has been live
     * since. So the cache is not the cache any more and the diff would
     * read sixteen hundred bytes of it as unchanged, leaving that band
     * of the screen stale until something happened to redraw it. Rebuilt
     * from scratch instead, which is what the other branch of this
     * function already does and what a cartridge swap wants anyway. */
    pump_resync = 1;
    swap_publish();

    /* Disarm only when the slot answered. The old version disarmed
     * unconditionally, which broke every swap after the first: SWAP.PRG
     * speaks a two-press protocol -- pull, press, seat, press -- and
     * the press over the empty slot used up the whole arm, so the press
     * over the reseated cartridge fell on a disarmed servant and the
     * new cartridge was never probed or mounted. The owner ran exactly
     * that test and got an S: that would not open, which said nothing
     * about the cartridge and everything about this line. Staying armed
     * over an empty probe makes the second swap the same conversation
     * as the first; a still-armed servant after SWAP.PRG times out
     * costs only that a later press probes the slot once more, and the
     * first probe that finds anything disarms. */
    if (cart_sectors)
        swap_armed = 0;
    else
        ready = 0;
}

static void cart_probe(void)
{
    if (VU32(M1_FLAG) == M1_MAGIC && !cart_swap_force_m2) {
        /* Mode 1: our own cartridge, ordinary Genesis save RAM. There
         * is no size id to ask for, so the declared window is checked
         * at both ends instead -- a cart with no save RAM fitted reads
         * back ROM or open bus and fails the first one. */
        cart_data = CART_DATA_M1;
        VU8(CART_CTL_M1) = 0x01;
        cart_sectors = CART_SECTORS_M1;
        if (!cart_holds(0) || !cart_holds(CART_SECTORS_M1 * 512u - 1))
            cart_sectors = 0;
        return;
    }

    {   /* Mode 2: whatever is in the Mega CD's cartridge window.
         *
         * On this branch that is not the boot cartridge -- the boot
         * cartridge is what put us in Mode 1 -- it is a backup RAM cart
         * swapped in after the machine was already running. See
         * swap_watch().
         *
         * The ID register is not a presence test and its absence is not
         * an absence of memory: a cart may answer at either half of the
         * bus, may need the Sega mapper's enable at $A130F1, and may
         * carry its own driver with its memory nowhere the BIOS would
         * look. cart_climb() tries all of it and reports where it
         * landed; the size is measured rather than believed. All of
         * this is the CD branch's, ported here unchanged because the
         * cartridge does not know which branch is talking to it. */
        cart_probe_id = VU8(CART_ID_M2);
        cart_data = CART_DATA_M2;
        cart_probe_ours = 0;

        /* Known register state before the first read, every session.
         *
         * The fast path below looks for the volume read-only, but a
         * hot-inserted cartridge's latches hold power-up junk, and on
         * a banking clone that junk decides which view of the SRAM
         * answers at $600001. Looking before normalising means looking
         * into a random bank: the volume is there and invisible. So
         * both registers the ladder ever touches are forced to their
         * power-on defaults first -- protect on, $700001 clear --
         * which is the deterministic state a seated-at-power-on
         * cartridge is in when the CD branch reads it. */
        if (cart_swap_force_m2) {
        VU8(CART_WP_M2)  = 0x00;
        VU8(0x700001ul)  = 0x00;

        /* ...and the /TIME latches, the only documented state left.
         *
         * Ten power cycles read 52 9F at bytes 510/511: the same wrong
         * view every time. The one session that ran with $7FFFFF and
         * $700001 forced read 12 DF there -- the view moved. So the
         * view is latch-selected, those two are part of the address,
         * and something on /TIME holds the rest.
         *
         * Sega's own banking cartridge defines that hardware: the SSF2
         * mapper, bank registers $A130F3 through $A130FF in 512K pages,
         * reset state linear -- bank N in slot N -- with $A130F1 the
         * SRAM control. A clone built big and sold cheap is built from
         * that design, because it is the one every mapper document
         * describes. Writing the reset state into latches that do not
         * exist costs a plain cartridge nothing -- a /TIME strobe with
         * no listener is ignored -- and writing it into latches that do
         * exist puts a hot-inserted cartridge into the state a power-on
         * gives it: the state the CD branch reads, the one that
         * persists. */
        VU8(0xA130F1ul) = 0x00;
        VU8(0xA130F3ul) = 0x01;
        VU8(0xA130F5ul) = 0x02;
        VU8(0xA130F7ul) = 0x03;
        VU8(0xA130F9ul) = 0x04;
        VU8(0xA130FBul) = 0x05;
        VU8(0xA130FDul) = 0x06;
        VU8(0xA130FFul) = 0x07;
        }

        /* Swap only. cart_check_alias() reads eight addresses and
         * writes all eight back, and two of them are $020001 and
         * $200001 -- the PRG-RAM window and Word RAM -- with no bus
         * grant, so on a CD boot it reads whatever the unarbitrated
         * bus felt like and puts that into live system memory. The
         * console's own EmuTOS image is in there. It answers a
         * question about a hot-inserted cartridge; a cartridge seated
         * at power-on is where the BIOS put it. */
        if (cart_swap_force_m2)
            cart_check_alias();

        /* Sixty-four bytes of the cartridge's own front, verbatim, put
         * where the sub CPU can read them.
         *
         * Every question left about this cartridge is a question about
         * what it says at $400000-$40003F: whether the twelve signature
         * bytes are there at all, what its ROM answers with, what its
         * driver entry looks like. Three builds have reported "NO
         * MATCH" without ever showing the bytes that failed to match,
         * which is a verdict with the evidence withheld.
         *
         * It goes just past the sector bounce buffer -- sub $7F200,
         * clear of the 512 bytes a transfer uses -- so a later sector
         * read does not wipe it before anyone looks. Writing to PRG-RAM
         * needs the sub bus and CART_BANK selected, exactly as
         * cart_service() does it. */
        if (cart_swap_force_m2) {
            uint8_t save = VU8(GA_MEMMODE), i;

            sub_bus_grab();
            VU8(GA_MEMMODE) = (uint8_t)((save & ~0xC0u) | (CART_BANK << 6));
            for (i = 0; i < 64; i++)
                VU8(PRG_WINDOW + 0x1F200u + i) = VU8(0x400000ul + i);
            VU8(GA_MEMMODE) = (uint8_t)((VU8(GA_MEMMODE) & ~0xC0u)
                                        | (SCREEN_BANK << 6));
            sub_bus_release();
        }

        /* Whose memory answers at $600001? Asked directly, at last.
         *
         * Sega's plain cartridge protocol is odd bytes on an 8-bit
         * bus: the even addresses of the window belong to nothing.
         * Every console-side memory -- Word RAM, PRG RAM -- is
         * sixteen bits wide and holds both halves. So the even half
         * is given the same sixteen-byte test the odd half passes:
         * even bytes that remember are not a plain cartridge, they
         * are console RAM the map left behind, and everything ever
         * formatted here went into the console and not the cart.
         *
         * And in case the map only half-moved, the Mode 1 slot
         * addresses are sampled read-only -- ID at $000001, data at
         * $200001, Sega's same protocol one window down -- including
         * whether our own volume is sitting there. If it is, the
         * cartridge never moved and every probe of $600001 was aimed
         * at the wrong window.
         *
         * $000000 is read through addr_zero like read_zero() does:
         * gcc turns a literal null dereference into a trap. */
        if (cart_swap_force_m2) {
            uint8_t ev[41], i, save;
            uint32_t z = addr_zero;

            ev[0] = 'E'; ev[1] = 'V';
            ev[2] = mem_holds16(0x600000ul);        /* even half, low */
            ev[3] = mem_holds16(0x680001ul);        /* odd half, high */
            ev[4] = mem_holds16(0x680000ul);        /* even half, high */
            ev[5] = cart_looks_ours(0x200001ul);    /* M1 data window */
            ev[6] = cart_looks_ours(z + 1u);
            ev[7] = VU8(0x000001ul);                /* M1 ID address */
            for (i = 0; i < 8; i++) ev[8 + i]  = VU8(0x200001ul + i * 2u);
            for (i = 0; i < 8; i++) ev[16 + i] = VU8(z + i);
            for (i = 0; i < 8; i++) ev[24 + i] = VU8(0x680001ul + i * 2u);
            for (i = 0; i < 8; i++) ev[32 + i] = VU8(0x600000ul + i * 2u);
            /* The low half too, in the same protected state as the
             * three above, so the write-protect matrix has all four
             * cells: a half that refuses writes protected and accepts
             * them enabled is behind Sega's WP gate, which is what a
             * real battery SRAM is; a half that writes either way is
             * in front of it. */
            ev[40] = mem_holds16(0x600001ul);

            save = VU8(GA_MEMMODE);
            sub_bus_grab();
            VU8(GA_MEMMODE) = (uint8_t)((save & ~0xC0u) | (CART_BANK << 6));
            for (i = 0; i < 41; i++)
                VU8(PRG_WINDOW + 0x1F280u + i) = ev[i];
            VU8(GA_MEMMODE) = (uint8_t)((VU8(GA_MEMMODE) & ~0xC0u)
                                        | (SCREEN_BANK << 6));
            sub_bus_release();
        }

        /* Look before touching.
         *
         * Everything below this writes: cart_scan() puts 5A and A5 into
         * the first byte of all thirty-two pages on both halves of the
         * bus, once per rung, and page 16 of the odd half is $600001 --
         * which is byte zero of sector zero, the boot sector. It puts
         * the original back each time, but a rung whose action changes
         * the mapping between the save and the restore writes the old
         * byte somewhere new. Six rungs of that on a cartridge holding
         * somebody's files is not a probe, it is a hazard.
         *
         * And it is unnecessary when the cartridge is where the BIOS
         * would put it. So: read the standard window first, and if our
         * own boot sector is sitting there, take it and stop. The size
         * comes out of the BPB's own total-sectors field, so this whole
         * path writes nothing at all. */
        if (cart_looks_ours(CART_DATA_M2)) {
            uint16_t total = (uint16_t)(VU8(CART_DATA_M2 + 19u * 2u)
                             | ((uint16_t)VU8(CART_DATA_M2 + 20u * 2u) << 8));
            if (total) {
                cart_data       = CART_DATA_M2;
                cart_probe_ours = 1;
                cart_mem_real   = 1;
                /* 7 is not a rung. The ladder has six, 0 to 5, so this
                 * says "found at the standard window, ladder never run,
                 * nothing written" -- which is a different fact from
                 * "rung 0 happened to work". */
                cart_probe_step = 7;
                cart_sectors    = total;
                cart_read_sig();
                VU16(GA_CMD0) = (uint16_t)(((uint16_t)cart_probe_id << 8)
                                           | 0x10u | (7u << 5));
                cart_publish_where();
                cart_write_enable(0);
                return;
            }
        }

        /* Let the cartridge be written before asking whether it holds
         * anything.
         *
         * cart_climb() decides a rung by writing a marker and reading
         * it back, so with the write protect still set every rung fails
         * and the answer is "no cartridge" however good the memory is.
         * The CD branch has always done this and reads this same
         * cartridge; this branch never did, and reported zero sectors
         * on hardware from a cartridge that was seated and working.
         *
         * It read 512 sectors on an earlier run of the same code, which
         * is the tell: the ladder's own rungs write this register as a
         * side effect, so whether the probe worked depended on which
         * rung it happened to reach first. That is not a probe, it is a
         * coin toss. Read-modify-write, bit 0, exactly as the BIOS does
         * it at $008120 -- whatever else lives in that register is not
         * ours to clear. */
        cart_probe_wp0 = VU8(CART_WP_M2);
        VU8(CART_WP_M2) = (uint8_t)(cart_probe_wp0 | 0x01);

        /* The other two cells of the write-protect matrix, now that
         * the enable is set. The evidence block above ran with the
         * protect forced on; these two lines rerun both halves
         * enabled and park the results beside it. On the hardware
         * that motivated this, $680001 refused writes protected and
         * took them for cart_measure() enabled -- Sega's WP gate
         * behaving exactly as a real backup SRAM's does -- while the
         * window's low half is where every session's volume vanishes.
         * Whether the battery-held memory sits behind the gate at the
         * window's high half is precisely what these four cells say. */
        if (cart_swap_force_m2) {
            uint8_t on_lo = mem_holds16(0x600001ul);
            uint8_t on_hi = mem_holds16(0x680001ul);
            uint8_t save = VU8(GA_MEMMODE);

            sub_bus_grab();
            VU8(GA_MEMMODE) = (uint8_t)((save & ~0xC0u) | (CART_BANK << 6));
            VU8(PRG_WINDOW + 0x1F2A9u) = on_lo;
            VU8(PRG_WINDOW + 0x1F2AAu) = on_hi;
            VU8(GA_MEMMODE) = (uint8_t)((VU8(GA_MEMMODE) & ~0xC0u)
                                        | (SCREEN_BANK << 6));
            sub_bus_release();
        }

        /* Let a smart cartridge set itself up before looking for its
         * memory. Read the signature first: only a cartridge that says
         * "RAM_CARTRIDG" at $400010 gets its own code called. */
        cart_read_sig();
        if (cart_is_smart)
            cart_brminit();


        {
            uint32_t found = cart_climb();

            if (!found) {
                /* The protect goes back on this exit too.
                 *
                 * It is set unconditionally above and cleared on both
                 * the other two ways out of this function, and this
                 * one -- the ladder found nothing -- was left out. So a
                 * slot that fails to probe leaves the cartridge
                 * write-enabled for the rest of the session and through
                 * the power going down, which is the one condition a
                 * backup RAM cart must never be in: it is how the array
                 * gets scribbled on as the rails collapse. Worse, it
                 * latches -- a cart that probes badly once gets
                 * corrupted at power-off and probes badly again. */
                cart_write_enable(0);
                cart_data = CART_DATA_M2;
                cart_sectors = 0;
                return;
            }
            cart_data = found;
        }

        /* The ladder found something that answers a one-address test.
         * Ask the harder question before believing in it. */
        cart_mem_real = mem_holds16(cart_data);

        cart_read_sig();                /* smart cart? for the record */
        /* Publish the evidence, not the verdict.
         *
         * The first version of this published cart_is_smart, which is
         * two separate tests ANDed together -- bit 7 of $400001, and
         * twelve signature bytes at $400010 -- so "plain" on the
         * television could mean either, and the screen is the only
         * place any of this can be read. One word, so: the ID byte
         * whole, and each test its own bit.
         *
         *   bits 8-15  $400001 as read, bit 7 of it being the console's
         *              own "a cartridge is present" answer
         *   bit 0      the twelve signature bytes matched
         *   bit 1      the cartridge's BRMINIT was called
         *   bit 2      ...and set carry, meaning it refused
         *   bit 3      it answered with a size the call believes
         */
        {
            uint8_t sig_ok = 1, i;
            static const char want[12] =
                { 'R','A','M','_','C','A','R','T','R','I','D','G' };
            for (i = 0; i < 12; i++)
                if (cart_sig[i] != (uint8_t)want[i]) { sig_ok = 0; break; }

            VU16(GA_CMD0) = (uint16_t)(((uint16_t)cart_probe_id << 8)
                                       | (sig_ok      ? 0x01u : 0u)
                                       | (brm_called  ? 0x02u : 0u)
                                       | (brm_carry   ? 0x04u : 0u)
                                       | (brm_size    ? 0x08u : 0u)
                                       | (cart_mem_real ? 0x10u : 0u)
                                       | (uint16_t)((cart_probe_step & 7u) << 5));
        }

        cart_publish_where();

        if (!cart_mem_real) {
            /* One latch, not memory. Reporting a size here is how a
             * cartridge that is not answering came to be mounted as a
             * half-megabyte drive full of noise. */
            cart_sectors = 0;
        } else {
            uint8_t k = cart_measure();
            cart_sectors = (k >= 1) ? (uint16_t)(16u << k) : 0;
        }

        /* Does the top of the reported size alias the bottom?
         *
         * cart_measure() finds where the memory folds by walking
         * doubling offsets, and it stops at the first one that fails --
         * which is right only if the cartridge folds cleanly. If it
         * does not, we hand EmuTOS a drive twice its real size, the
         * high half is the low half under another name, and the first
         * file written into the upper clusters lands on top of the boot
         * sector and the FAT. An empty volume survives that; one with
         * anything in it does not, and that is the reported fault
         * word for word.
         *
         * Sector 0 byte 0 against the same byte one half-disk up. Both
         * originals go back. */
        if (cart_sectors > 1) {
            uint32_t half = (uint32_t)(cart_sectors / 2u) * 512u;
            uint8_t s0 = cart_rd(0), sh = cart_rd(half);

            cart_wr(0, 0x11);
            cart_wr(half, 0x22);
            (void)bus_flush;
            cart_size_aliases = (uint8_t)(cart_rd(0) != 0x11);
            cart_wr(half, sh);
            cart_wr(0, s0);
            if (cart_size_aliases)
                cart_sectors = (uint16_t)(cart_sectors / 2u);
        }

        /* Last, after cart_measure().
         *
         * The first attempt dropped the enable before the sizing, and
         * cart_measure() finds where the memory folds by writing
         * markers -- so every write failed, it measured nothing, and a
         * working cartridge reported zero sectors. The protect goes
         * back when the probe is actually finished with the bus. */
        cart_write_enable(0);
    }
}

/* Lay down a fresh FAT12 filesystem on the cartridge. Emulators either
 * wipe the cart (GPGX reformats it to Sega BRAM layout at load) or
 * start it empty, and a real cart out of the packet is unformatted
 * too — so the servant formats whatever it finds without our
 * signature. Geometry: 512-byte sectors, 4 per cluster, 1 FAT sector,
 * 64 root entries. */
static void cart_format(void)
{
    uint16_t total = cart_sectors > 1024 ? 1024 : cart_sectors;
    uint32_t i;

    for (i = 0; i < 512u * 7u; i++)      /* boot + 2 FAT + 4 root */
        cart_wr(i, 0);

    cart_wr(0, 0x60); cart_wr(1, 0x38);              /* bra.s */
    cart_wr(2, 'E'); cart_wr(3, 'm'); cart_wr(4, 'u');
    cart_wr(5, 'T'); cart_wr(6, 'O'); cart_wr(7, 'S');
    cart_wr(8, 0x24); cart_wr(9, 0x08); cart_wr(10, 0x26);   /* serial */
    cart_wr(11, 0x00); cart_wr(12, 0x02);            /* 512 bytes/sector */
    cart_wr(13, 4);                                  /* sectors/cluster */
    cart_wr(14, 1); cart_wr(15, 0);                  /* reserved */
    cart_wr(16, 2);                                  /* FATs */
    cart_wr(17, 64); cart_wr(18, 0);                 /* root entries */
    cart_wr(19, (uint8_t)total); cart_wr(20, (uint8_t)(total >> 8));
    cart_wr(21, 0xF8);                               /* media */
    cart_wr(22, 1); cart_wr(23, 0);                  /* sectors/FAT */
    cart_wr(24, 16); cart_wr(25, 0);                 /* sectors/track */
    cart_wr(26, 1); cart_wr(27, 0);                  /* sides */
    /* The two bytes EmuTOS actually looks for: atari_partition() calls
     * a root sector without them a non-ATARI one and allocates no drive
     * letter, so a perfectly good FAT12 volume gets no drive. The CD
     * branch found that and fixed all three formatters; this one is the
     * copy that never got it. */
    cart_wr(510, 0x55); cart_wr(511, 0xAA);

    cart_wr(512, 0xF8); cart_wr(513, 0xFF); cart_wr(514, 0xFF);   /* FAT 1 */
    cart_wr(1024, 0xF8); cart_wr(1025, 0xFF); cart_wr(1026, 0xFF);/* FAT 2 */

    {   /* volume label in the first root entry */
        static const char lbl[11] = {'M','E','G','A','C','D',' ',' ',' ',' ',' '};
        for (i = 0; i < 11; i++) cart_wr(1536 + i, (uint8_t)lbl[i]);
        cart_wr(1536 + 11, 0x08);
    }
}

/* ---- native payloads ------------------------------------------------
 *
 * Genesis-side code, staged off a disc and run with the VDP handed to
 * it. docs/payload.md is the contract; the short of it is that the
 * image lands at CACHE, which is the planar cache and is idle for
 * exactly as long as a payload owns the screen, and that the servant
 * knows nothing about any particular payload.
 *
 * Why the servant can hand the machine over so cheaply: the VDP
 * register file is write-only, so no correct design could have saved
 * and restored it anyway. The only way back is for the side that knows
 * the state to write it again, and vdp_init() is that side. A payload
 * therefore owes nothing on the way out -- which is what makes "run
 * arbitrary Genesis code" a safe thing to offer rather than a way to
 * lose the desktop. */
#define PAYLOAD_MAX   32000u
static uint8_t  payload_staged;         /* blocks accepted since boot */

/* The pump is off while a payload is being staged.
 *
 * The payload lands in the planar cache, and the planar cache is the
 * pump's own working memory -- it writes every longword of it that
 * differs from the screen, every frame. So the first attempt staged a
 * perfectly good header and the pump had eaten it by the time the run
 * arrived: the servant reported "not a payload" about a payload that
 * had been there a fraction of a second earlier.
 *
 * So the first block stops the pump, exactly as the cartridge swap
 * does, and the screen holds until the payload takes it. The countdown
 * is for the caller that stages and then dies or thinks better of it:
 * without it the picture would be frozen until the console was. Ten
 * seconds, then the cache is rebuilt and the desktop comes back. */
static uint8_t  payload_hold;
static uint16_t payload_hold_ttl;

/* Frames of forced sweep still owed before staging may begin. See the
 * long comment at op 4. */
static uint8_t  payload_flush;

/* Bulk data: where a payload's own megabyte-sized things are.
 *
 * Not staged and not copied. The sub CPU writes them into PRG RAM
 * directly -- it is its own memory -- and sends two words saying which
 * bank of the window they are in, how far into it, and how long they
 * are. The payload is handed the window address and reads them in
 * place. docs/payload.md, "Bulk data".
 *
 * Zero means none, which is what every payload before this had. */
static uint32_t payload_bulk;           /* window address, or 0 */
static uint8_t  payload_bulk_bank;
static uint32_t payload_bulk_len;

/* What the payload is handed, staged in memory so the jump can load
 * them without the compiler choosing the registers. Not static: the
 * assembly block below names them. */
uint32_t pl_scr, pl_work, pl_mode, pl_bulk, pl_blen, pl_entry;

/* Tentative definitions: these are defined with their initialisers down
 * beside the pump, which is where they belong and which is after this. */
static uint8_t  screen_bank;
static uint32_t screen_woff;
static uint16_t pal_gen;

struct payload_hdr {
    uint32_t magic;                     /* 'MDPL' */
    uint16_t version, flags;
    uint32_t entry, length, workspace;
    uint8_t  name[8];
};

/* Published so a host dump can tell "the file never arrived" from "it
 * arrived and was refused" from "it ran".
 *
 * WATCH + 48, not REPORT + 0x40. The first attempt used that and read
 * back 0x0200 on the very first run, because the sector bounce dump at
 * line ~1788 already writes 0xFF0F40 upwards -- the same collision the
 * WATCH block's own header records having been through once before.
 * The report block is crowded; this one is not. */
#define PAYLOAD_STAT (WATCH + 48u)

static void payload_run(void)
{
    const struct payload_hdr *h = (const struct payload_hdr *)CACHE;
    uint32_t total;

    VU16(PAYLOAD_STAT) = 0x0001;                /* asked */
    if (!payload_staged) {
        VU16(PAYLOAD_STAT) = 0x0006;            /* run without a payload */
        return;
    }
    if (h->magic != 0x4D44504Cul || h->version != 1 || h->flags) {
        VU16(PAYLOAD_STAT) = 0x0002;            /* not a payload */
        return;
    }
    total = h->length + h->workspace;
    if (h->length < sizeof *h || total > PAYLOAD_MAX
        || h->entry >= h->length || (h->entry & 1)) {
        VU16(PAYLOAD_STAT) = 0x0003;            /* will not fit, or bent */
        return;
    }

    VU16(PAYLOAD_STAT) = 0x0004;                /* going */
    {
        /* The ST screen for the payload to read, at whatever window and
         * bank the pump is currently following. Handed over as an
         * address rather than copied: 32000 bytes is the whole of the
         * space the payload has, and it is not going to spend it on a
         * second copy of a screen it can read in place. */
        /* Handed over in statics rather than as asm operands. There
         * are five of them now, the block saves d2-d7 itself, and
         * letting the compiler pick registers for the arguments meant
         * one of them could be a register the block had just pushed --
         * loaded after the push and clobbered before the jump. Reading
         * them out of memory inside the block cannot go wrong. */
        pl_scr  = PRG_WINDOW + screen_woff;
        pl_work = CACHE + h->length;
        /* ...and the main loop's own pass counter in the top half.
         *
         * A payload is staged from a file and its .data is whatever the
         * file says, so anything it seeds a random number generator
         * with is the same value on every run and it draws the same
         * "random" thing every time. Nothing on this side of the
         * machine keeps a clock a payload could ask, but this counter
         * has been running since the servant started and the number of
         * frames between boot and a person deciding to open a menu is
         * not a number anyone can repeat. It is not entropy in any
         * serious sense; it is a value that differs, which is what a
         * level generator actually needs. docs/payload.md. */
        pl_mode = ((VU32(M1_FLAG) == M1_MAGIC) ? 1u : 0u)
                | ((uint32_t)payload_bulk_bank << 8)
                | ((uint32_t)screen_bank << 10)
                | ((uint32_t)VU16(WATCH + 4) << 16);
        pl_bulk = payload_bulk;
        pl_blen = payload_bulk_len;
        pl_entry = CACHE + h->entry;

        VU8(GA_MEMMODE) = (uint8_t)((VU8(GA_MEMMODE) & ~0xC0u)
                                    | (screen_bank << 6));
        __asm__ volatile(
            "move.w  %%sr,-(%%sp)\n\t"
            "move.w  #0x2700,%%sr\n\t"
            "movem.l %%d0-%%d7/%%a0-%%a6,-(%%sp)\n\t"
            "movea.l pl_scr,%%a0\n\t"
            "movea.l pl_work,%%a1\n\t"
            "move.l  pl_mode,%%d0\n\t"
            "move.l  pl_bulk,%%d1\n\t"
            "move.l  pl_blen,%%d2\n\t"
            "movea.l pl_entry,%%a2\n\t"
            "jsr     (%%a2)\n\t"
            "movem.l (%%sp)+,%%d0-%%d7/%%a0-%%a6\n\t"
            "move.w  (%%sp)+,%%sr"
            :
            :
            : "cc", "memory");
    }

    /* Put the machine back. Everything the payload could have touched
     * is written again from what this side knows. */
    vdp_init();
    screen_nametab();
    osk_upload_tiles();
    osk_diag_init();
    pal_gen = 0xFFFFu;          /* whatever CRAM holds now, it is not ours */
    pump_resync = 1;
    payload_hold = 0;
    payload_staged = 0;
    VU16(PAYLOAD_STAT) = 0x0005;                /* came back */
}

/* Service at most one posted cart request per call. Runs with the sub
 * bus already granted and the window on CART_BANK. */
static uint8_t cart_last_seq;
static void cart_service(void)
{
    uint16_t req = VU16(GA_CART_REQ);
    uint8_t seq = (uint8_t)req;
    uint8_t op = (uint8_t)(req >> 8);
    uint32_t lba, base, i;
    volatile uint8_t *bounce;
    volatile uint8_t *cart;

    if (seq == cart_last_seq || op == 0) return;
    if (op == 3) {                      /* SWAP.PRG: arm the cartridge swap */
        swap_armed = 1;
        /* Take the sector path off the cartridge before anybody touches
         * it: an empty drive answers reads and writes without reaching
         * memory. On the first swap S: lives on the departing boot
         * cartridge; on every later swap it lives on the backup cart
         * about to be pulled -- so the drive goes empty on ALL arms,
         * where the first version only did it on the first and left
         * the sector path free to reach into a half-removed cartridge.
         * The probe on the confirming press republishes whatever is
         * really there, including "the same cartridge, untouched".
         *
         * The picture stop (swap_quiet, and the blue heartbeat that
         * proves the machine is alive during it) stays first-swap-only:
         * that is for the /CART flip, which only the boot cartridge's
         * departure causes. Later swaps keep the screen running -- the
         * framebuffer window at $020000 is Mega CD, not slot. */
        cart_sectors = 0;
        if (!cart_swapped)
            swap_quiet = 1;
        cart_last_seq = seq;
        VU16(GA_CART_ACK) = seq;
        return;
    }
    if (op == 6) {                      /* stage straight into VRAM */
        /* Tiles, and anything else a payload wants in video memory
         * before it starts. It exists because the obvious place for
         * Sonic's art is not Genesis RAM -- there are 32000 bytes there
         * and the art is fifty thousand -- but VRAM, which is 64K and
         * is entirely free for as long as a payload owns the screen.
         * The ST screen's own thousand tiles are rebuilt from scratch
         * on the way out regardless, so there is nothing in VRAM worth
         * preserving across a payload.
         *
         * It is not what Sonic uses. Its engine streams art out of a
         * pool a frame at a time, so the pool has to be readable
         * memory and goes to PRG RAM instead (ops 7 and 8 below). This
         * op is for a payload whose tiles are simply tiles. */
        uint32_t blk = VU16(GA_CART_LBA);
        volatile uint8_t *src = (volatile uint8_t *)(PRG_WINDOW + BOUNCE_WOFF);
        uint32_t k;

        VU32(VDP_CTRL) = vdp_vram_w((uint16_t)(blk * 512u));
        for (k = 0; k < 512u; k += 2)
            VU16(VDP_DATA) = (uint16_t)((src[k] << 8) | src[k + 1]);
        VU16(PAYLOAD_STAT + 10u)++;
        cart_last_seq = seq;
        VU16(GA_CART_ACK) = seq;
        return;
    }
    if (op == 7) {                      /* where the bulk data is */
        uint16_t where = VU16(GA_CART_LBA);
        payload_bulk_bank = (uint8_t)((where >> 14) & 3);
        payload_bulk = PRG_WINDOW + ((uint32_t)(where & 0x3FFFu) << 9);
        cart_last_seq = seq;
        VU16(GA_CART_ACK) = seq;
        return;
    }
    if (op == 8) {                      /* ...and how much of it there is */
        payload_bulk_len = (uint32_t)VU16(GA_CART_LBA) << 9;
        cart_last_seq = seq;
        VU16(GA_CART_ACK) = seq;
        return;
    }
    if (op == 9) {                      /* a page to print: where it is */
        uint16_t where = VU16(GA_CART_LBA);
        prn_bank = (uint8_t)((where >> 14) & 3);
        prn_base = PRG_WINDOW + ((uint32_t)(where & 0x3FFFu) << 9);
        cart_last_seq = seq;
        VU16(GA_CART_ACK) = seq;
        return;
    }
    if (op == 10) {                     /* ...how long, and go */
        /* Refused while one is still going out. A page is four
         * kilobytes and the wire is 480 bytes a second, so the sub can
         * be nine seconds ahead of the paper; it asks again. */
        if (prn_state == PRN_IDLE) {
            /* The engine is only running if somebody asked for the
             * serial keyboard, and printing is the other reason to want
             * it. Turned on here rather than at boot: a port left
             * driving its pins is not a thing to do to a connector
             * nobody said was a serial port. It stays on afterwards --
             * the receive side is gated on the same flag and an
             * unplugged keyboard says nothing. */
            if (!uart_active())
                uart_enable(1);
            prn_len   = VU16(GA_CART_LBA);
            prn_pos   = 0;
            prn_sum   = 0;
            prn_have  = 0;
            prn_take  = 0;
            prn_stall = 0;
            prn_state = PRN_SYNCB;
            VU16(PRN_STAT + 4u) = (uint16_t)prn_len;
        }
        VU16(GA_CART_CNT + 0u) = 0;     /* nothing to say back */
        cart_last_seq = seq;
        VU16(GA_CART_ACK) = seq;
        return;
    }
    if (op == 4 || op == 5) {           /* stage a payload, and run it */
        if (op == 4) {
            uint32_t blk = VU16(GA_CART_LBA);

            /* THE SCREEN HAS TO BE THE SCREEN FIRST.
             *
             * The pump notices changes 25 lines at a time, so a full
             * screen takes eight frames to be seen at all -- and the
             * first staging block stops the pump dead, because the
             * cache is where the payload lands. Whatever had not been
             * swept by that moment stayed stale in VRAM for the whole
             * run.
             *
             * That is a real gap, not a theoretical one: an accessory
             * is called the instant the AES has restored the desktop
             * under its menu, and the restore is exactly the part that
             * has not been swept. So the picture Sonic was walking on
             * was not the picture on the screen -- he stood on a window
             * edge that VRAM still showed as clear white, and a leftover
             * fragment of the drop-down sat inside a program icon.
             *
             * So the first block is refused, silently and without an
             * acknowledgement, until a forced full sweep has been and
             * gone. The sub CPU is inside cart_xfer's poll, which waits
             * some three seconds; this costs about a third of one. */
            if (!payload_hold) {
                if (!payload_flush) {
                    payload_flush = 28;         /* bootstrap is 24 + margin */
                    pump_resync = 1;            /* ...and it sets bootstrap */
                    VU16(PAYLOAD_STAT + 12u)++; /* sweeps forced */
                }
                return;                         /* no ack: the sub waits */
            }
            VU16(PAYLOAD_STAT + 8u)++;          /* op 4s the servant saw */
            payload_hold_ttl = 600;
            if ((blk + 1u) * 512u <= PAYLOAD_MAX) {
                volatile uint8_t *src =
                    (volatile uint8_t *)(PRG_WINDOW + BOUNCE_WOFF);
                volatile uint8_t *dst =
                    (volatile uint8_t *)(CACHE + blk * 512u);
                uint32_t k;
                for (k = 0; k < 512u; k++) dst[k] = src[k];
                if (payload_staged < 0xFF) payload_staged++;
                VU16(PAYLOAD_STAT + 2u) = payload_staged;
                VU16(PAYLOAD_STAT + 4u) = (uint16_t)blk;
                VU16(PAYLOAD_STAT + 6u) = VU16(CACHE);   /* 'MD' if it landed */
            }
        }
        cart_last_seq = seq;
        VU16(GA_CART_ACK) = seq;
        /* Acknowledged first, and only then run. A payload takes the
         * machine for as long as it likes, and the program that asked
         * for it is sitting in a loop waiting for this byte: leaving it
         * unacknowledged for the duration would look exactly like a
         * servant that had died. */
        if (op == 5) payload_run();
        return;
    }
    lba = VU16(GA_CART_LBA);
    if (!cart_sectors || lba >= cart_sectors) { cart_last_seq = seq;
        VU16(GA_CART_ACK) = seq;   /* word write: sub reads the low byte */ return; }

    bounce = (volatile uint8_t *)(PRG_WINDOW + BOUNCE_WOFF);
    base = lba * 512u;
    cart = (volatile uint8_t *)CART_DATA;

    if (op == 2) {                               /* write: bounce -> cart */
        cart_write_enable(1);
        for (i = 0; i < 512; i++)
            cart[(base + i) * 2] = bounce[i];
        cart_write_enable(0);
    } else {                                      /* read: cart -> bounce */
        for (i = 0; i < 512; i++)
            bounce[i] = cart[(base + i) * 2];
    }
    cart_last_seq = seq;
    VU16(GA_CART_ACK) = seq;   /* word write: sub reads the low byte */
    VU16(0xFF0F30u)++;                    /* telemetry: sectors serviced */
    VU16(0xFF0F32u) = (uint16_t)((op << 8) | (lba & 0xFF));
    {   /* first 16 bytes of the transferred sector, for the host */
        uint16_t t;
        for (t = 0; t < 8; t++)
            VU16(0xFF0F40u + 2u * t) = (uint16_t)((bounce[t*2] << 8) | bounce[t*2+1]);
        for (t = 0; t < 8; t++)
            VU16(0xFF0F50u + 2u * t) =
                (uint16_t)((VU8(CART_DATA + (uint32_t)(t*2)*2) << 8)
                          | VU8(CART_DATA + (uint32_t)(t*2+1)*2));
    }
}

/* ---- the CDD trace -------------------------------------------------- */

/* What the CDBIOS said to the drive while it read this disc, recorded
 * by boot/cddtrace.S in the SP's own image. It has to be fetched before
 * command 5, because after that EmuTOS owns PRG-RAM and the SP's image
 * is gone -- and it has to come out through the bank window, because
 * the trace lives in bank 0 while everything else here uses bank 2. */
static uint16_t cdtrace_n;

/* Bus grabs here are kept to a few hundred cycles each, and there are a
 * lot of them, which is the opposite of how it was written the first
 * time: one grab held across the whole tag scan. That cost a boot. The
 * CDBIOS is still resident and still driving the CDD link at 75 Hz
 * while this runs, and halting the sub CPU across a tenth of a second
 * drops it out of an exchange -- the same failure sub_bus_grab_polite()
 * exists for, arrived at from the other direction. The reads that
 * followed then failed, the SP stalled forever in ReadCD, and the boot
 * ended on a bare white screen with the drive stopped.
 *
 * BURST words per grab: 128 window reads is about 40 us, comfortably
 * inside an exchange rather than across twenty of them. */
#define CDTRACE_BURST 128u

static uint32_t cdtrace_peek(uint32_t off, uint16_t words, uint16_t *dst)
{
    uint8_t save;
    uint16_t i;

    sub_bus_grab();
    save = VU8(GA_MEMMODE);
    VU8(GA_MEMMODE) = (uint8_t)(save & ~0xC2u);         /* bank 0 */
    for (i = 0; i < words; i++)
        dst[i] = VU16(PRG_WINDOW + off + 2u * i);
    VU8(GA_MEMMODE) = (uint8_t)(save & ~0x02u);
    sub_bus_release();
    return off;
}

static void cdtrace_fetch(void)
{
    static uint16_t buf[CDTRACE_BURST];
    uint32_t off;

    cdtrace_n = 0;
    for (off = CDTRACE_SCAN0; off < CDTRACE_SCAN1;
         off += 2u * (CDTRACE_BURST - 5u)) {
        uint16_t j;

        cdtrace_peek(off, CDTRACE_BURST, buf);
        /* The tag can straddle a burst, so each one overlaps the last
         * by the five words a tag-plus-count needs. */
        for (j = 0; j + 5u <= CDTRACE_BURST; j++) {
            uint16_t n, i;
            if (buf[j] != (uint16_t)(CDTRACE_TAG0 >> 16)) continue;
            if (buf[j + 1] != (uint16_t)CDTRACE_TAG0) continue;
            if (buf[j + 2] != (uint16_t)(CDTRACE_TAG1 >> 16)) continue;
            if (buf[j + 3] != (uint16_t)CDTRACE_TAG1) continue;
            n = buf[j + 4];
            if (n > CDTRACE_ENTS) n = CDTRACE_ENTS;
            VU16(0xFF0F60u) = (uint16_t)(off + 2u * j);
            for (i = 0; i < (uint16_t)(n * (CDTRACE_SIZE / 2u));
                 i += CDTRACE_BURST) {
                uint16_t k, want = (uint16_t)(n * (CDTRACE_SIZE / 2u) - i);
                if (want > CDTRACE_BURST) want = CDTRACE_BURST;
                cdtrace_peek(off + 2u * j + 12u + 2u * i, want, buf);
                for (k = 0; k < want; k++)
                    VU16(CDTRACE_WRAM + 2u * (i + k)) = buf[k];
            }
            cdtrace_n = n;
            VU16(0xFF0F62u) = (uint16_t)(0x5A00u | cdtrace_n);
            return;
        }
    }
    VU16(0xFF0F62u) = 0x5A00u;
}

static char hexd(uint8_t v)
{
    v = (uint8_t)(v & 15);
    return (char)(v < 10 ? '0' + v : 'A' + v - 10);
}

/* One entry, one 40-column line:
 *
 *   NO M COMMAND    STATUS     WHERE  XCHG
 *
 * The two packets print one character per byte because that is what
 * they are -- ten bytes each carrying one nibble, which is how the CDD
 * link is wired. WHERE is the position the drive had reached when the
 * entry closed; XCHG is how many 1/75 s exchanges it covered. */
static void cdtrace_line(uint16_t k, char *out)
{
    const volatile uint8_t *e =
        (const volatile uint8_t *)(CDTRACE_WRAM + (uint32_t)k * CDTRACE_SIZE);
    uint16_t i, p = 0, x;

    out[p++] = hexd((uint8_t)(k >> 4));
    out[p++] = hexd((uint8_t)k);
    out[p++] = ' ';
    out[p++] = hexd(e[26]);
    out[p++] = ' ';
    for (i = 0; i < 10; i++) out[p++] = hexd(e[i]);
    out[p++] = ' ';
    for (i = 0; i < 10; i++) out[p++] = hexd(e[10 + i]);
    out[p++] = ' ';
    for (i = 0; i < 6; i++) out[p++] = hexd(e[20 + i]);
    out[p++] = ' ';
    x = (uint16_t)(((uint16_t)e[28] << 8) | e[29]);
    out[p++] = hexd((uint8_t)(x >> 12));
    out[p++] = hexd((uint8_t)(x >> 8));
    out[p++] = hexd((uint8_t)(x >> 4));
    out[p++] = hexd((uint8_t)x);
    out[p] = 0;
}

#define TRACE_ROWS 23u          /* rows 1..23; 24 is the footer, 25-27 the
                                   status lines, and 27 is under the CRT's
                                   overscan on the user's television */

static void cdtrace_show(void)
{
    char line[48];
    uint16_t page = 0, prev = 0xFFFF, redraw = 1;
    uint16_t pages = (uint16_t)((cdtrace_n + TRACE_ROWS - 1u) / TRACE_ROWS);

    if (!pages) pages = 1;
    for (;;) {
        uint16_t pad;

        if (redraw) {
            uint16_t r;
            osk_row(0, "NO M COMMAND    STATUS     WHERE  XCHG");
            for (r = 0; r < TRACE_ROWS; r++) {
                uint16_t k = (uint16_t)(page * TRACE_ROWS + r);
                if (k < cdtrace_n) {
                    cdtrace_line(k, line);
                    osk_row((uint16_t)(r + 1), line);
                } else {
                    osk_row((uint16_t)(r + 1), "");
                }
            }
            if (!cdtrace_n)
                osk_row(1, "NO TRACE IN THE LOADER IMAGE");
            line[0] = 'P'; line[1] = 'A'; line[2] = 'G'; line[3] = 'E';
            line[4] = ' ';
            line[5] = (char)('1' + page);
            line[6] = ' '; line[7] = 'O'; line[8] = 'F'; line[9] = ' ';
            line[10] = (char)('0' + pages);
            {
                static const char tail[] =
                    "   A PAGES   START BOOTS";
                uint16_t t;
                for (t = 0; tail[t]; t++) line[11 + t] = tail[t];
                line[11 + t] = 0;
            }
            osk_row(24, line);
            redraw = 0;
        }

        wait_vblank();
        pad = pad_read();
        if ((pad & 0x40) && !(prev & 0x40)) {   /* A: next page */
            if (++page >= pages) page = 0;
            redraw = 1;
        }
        if ((pad & 0x80) && !(prev & 0x80))     /* Start: on with the boot */
            break;
        prev = pad;
    }
    screen_nametab();           /* undo the damage, see the comment there */
}

/* One bit per screen tile. A bitmap rather than a list because it
 * cannot overflow however much changes at once: dropping updates
 * starves whole bands of the screen, which is exactly what a heavy
 * repaint does. Tiles stay marked until converted, so a tile whose
 * eight lines straddle two copy chunks simply converts twice. */
#define TILE_COLS 40u
#define TILE_ROWS 25u
#define TILE_BYTES (TILE_COLS / 8)      /* 5 bitmap bytes per tile row */
static uint8_t tdirty[TILE_BYTES * TILE_ROWS];

static uint8_t  screen_bank = SCREEN_BANK;
static uint32_t screen_woff = SCREEN_WOFF;
/* Last palette generation uploaded to CRAM. 0xFFFF so that the first
 * block found is always applied, whatever the value in it. */
static uint16_t pal_gen = 0xFFFFu;

uint32_t prg_window = PRG_WINDOW_M2;

int main(void)
{
    /* Before anything reads PRG-RAM: in Mode 1 the window is 0x400000
     * higher, and the Mode 2 address lands in the cartridge's own ROM
     * where reads quietly succeed. */
    if (VU32(M1_FLAG) == M1_MAGIC) {
        prg_window = PRG_WINDOW_M1;
        swap_arm();     /* remember the boot ROM while the map is known */
    }

    uint16_t chunk = 0;
    uint16_t quiet = 0;      /* frames left with EmuTOS's VBL withheld */

    VU32(PRN_STAT) = 0x50524E54u;       /* 'PRNT': this block is ours */

    {   /* the main BIOS leaves boot debris in work RAM: zero the whole
         * telemetry block so anything nonzero afterward is ours */
        uint16_t k;
        for (k = 0; k < 0x100; k++) VU16(0xFF0E00u + 2u * k) = 0;
    }

    tab_init();
    vdp_init();
    osk_upload_tiles();
    osk_diag_init();

    /* pin the window on the screen's bank (never write DMNA here) */
    VU8(GA_MEMMODE) =
        (uint8_t)((VU8(GA_MEMMODE) & ~0xC0u) | (SCREEN_BANK << 6));

    /* The CDBIOS write-protects low PRG-RAM (WP bits, main-side only).
     * EmuTOS owns all of it now — vectors live there. Unprotect. */
    VU8(0xA12002u) = 0x00;

    /* Sample the pad before anything slow. Both CPUs are running: the
     * sub reads these flags early in its own startup, and cart_probe
     * below walks a megabyte, so publishing afterwards loses the race
     * and the sub sees a flag that was never set. Buttons held at
     * power-on are readable from the first instruction; read them
     * there.
     *
     * Holding C claims the console's internal backup RAM for EmuTOS,
     * reformatting it. Default is hands-off, because it usually holds
     * Sega-format game saves. */
    {
        uint8_t hi;
        VU8(IO_CTRL1) = 0x40;
        VU8(IO_DATA1) = 0x40;
        __asm__ volatile("nop\n\tnop");
        hi = VU8(IO_DATA1);
        if ((uint8_t)(~hi) & 0x20)              /* C held */
            VU16(GA_BRAM_CLAIM) = 0x0C1A;
        b_held = (uint8_t)(((uint8_t)(~hi) & 0x10) ? 1 : 0);

        {   /* A held: keep the CD link passive, sending the drive no
             * commands at all. That is the known-good state, and one
             * bad command holds the link down until the console is
             * power-cycled, so it is worth a button to get back to. */
            uint8_t lo;
            VU8(IO_DATA1) = 0x00;
            __asm__ volatile("nop\n\tnop");
            lo = VU8(IO_DATA1);
            a_held = (uint8_t)(((uint8_t)(~lo) & 0x10) ? 1 : 0);
            start_held = (uint8_t)(((uint8_t)(~lo) & 0x20) ? 1 : 0);
            /* Down: run the D: diagnostic instead of the desktop.
             * Left: command reads two frames early, as the CDBIOS does,
             * rather than this driver's twenty.
             *
             * Both on the d-pad because every face button is spoken
             * for, and both selectable at boot because there is one
             * CD-R left and a disc that can only answer one question is
             * worth less than a disc that can answer four. */
            down_held = (uint8_t)(((uint8_t)(~hi) & 0x02) ? 1 : 0);
            left_held = (uint8_t)(((uint8_t)(~hi) & 0x04) ? 1 : 0);
            /* Up: the stage-3 sector self-test. It used to run on every
             * boot, two far-apart reads starting the moment the HUD
             * came up -- pure drive work on a proven point. Now it
             * runs only when asked for; the emulator harness asks. */
            up_held = (uint8_t)(((uint8_t)(~hi) & 0x01) ? 1 : 0);
            /* Right: hand D: to the machine's own CDBIOS. Opt-in, and
             * it has to be: engaging it automatically put the first
             * visit inside EmuTOS's boot, where a firmware call that
             * does not return takes the whole machine with it before
             * anything can be printed or chosen. */
            right_held = (uint8_t)(((uint8_t)(~hi) & 0x08) ? 1 : 0);
            /* 0x5A marks the word as answered: a comm register reads
             * zero before anyone writes it, so "A is not held" and
             * "nobody has looked yet" would otherwise be the same. */
            VU16(GA_CMD1) = (uint16_t)(0x5A00u | a_held | (b_held << 1)
                                       | (down_held << 2) | (left_held << 3)
                                       | (up_held << 4) | (right_held << 5));
        }
    }

    /* Only when asked for. An ordinary boot must do exactly what it did
     * before any of this existed -- no bus grabs, no extra reads, no new
     * place to stall -- because the boot is the one part of this project
     * that has always worked and it is not the thing under test.
     *
     * Op 6 is the seek probe: the boot files all sit within a few
     * seconds of each other, so without it the trace only ever shows the
     * drive nudging forward. It runs here, with the SP idle between
     * commands, rather than during the loading.
     *
     * Not on a Mode 1 boot: every line of it is a handshake with the
     * SP, and on this branch the sub CPU is running EmuTOS, which has
     * never heard of GA_COMFLG_S. The second wait is unbounded, so
     * holding Start would hang the servant before it drew anything --
     * and now that the cartridge reads the pad, Start is a button a
     * person might actually be holding. */
    if (start_held && VU32(M1_FLAG) != M1_MAGIC) {
        while (VU8(GA_COMFLG_S)) ;
        VU8(GA_COMFLG_M) = 0;
        while (!VU8(GA_COMFLG_S)) ;
        VU8(GA_COMFLG_M) = 6;
        while (VU8(GA_COMFLG_S)) ;
        /* Leave the command byte set, as the IP does after its own last
         * command. Clearing it here lets the SP go straight back to
         * "ready", and the next block waits for a status that has
         * already been and gone -- which deadlocked the handoff and put
         * the console on a blank screen with EmuTOS never started. */
        cdtrace_fetch();
        cdtrace_show();
    }

    cart_probe();

    if (cart_sectors) {
        {   /* Write/read-back self-test on the last sector -- and put
             * it back afterwards. This used to leave the pattern
             * behind, which was harmless while the cart was scratch
             * space and is data loss now that it carries a filesystem:
             * the last sector is ordinary cluster space, so those 256
             * bytes belong to whatever file happens to end up there. */
            static uint8_t keep[256];
            uint32_t off = (uint32_t)(cart_sectors - 1) * 512u;
            uint16_t k, bad = 0;
            for (k = 0; k < 256; k++) keep[k] = cart_rd(off + k);
            for (k = 0; k < 256; k++) cart_wr(off + k, (uint8_t)(k ^ 0x5A));
            for (k = 0; k < 256; k++)
                if (cart_rd(off + k) != (uint8_t)(k ^ 0x5A)) bad++;
            VU16(0xFF0F3Au) = bad;          /* 0 = cart writes work */
            VU16(0xFF0F3Cu) = 0x5A00 | (cart_rd(off + 1) & 0xFF);
            for (k = 0; k < 256; k++) cart_wr(off + k, keep[k]);
        }
        /* Ours already? Then put the boot signature in if it predates
         * it -- disk.c only looks at a volume whose root sector ends
         * 55 AA, and this formatter did not always write those. That
         * repair is the only write this boot makes to the cartridge.
         *
         * It used to format anything that was not ours, because a
         * volume with no filesystem got no drive letter and boot was
         * the only moment it could be done. disk.c gives all four
         * devices a letter now, so an unformatted cartridge shows up
         * and FORMATS.PRG can have it. Formatting on every boot, on a
         * branch where the cartridge is also the boot device and now
         * also a thing you swap, is a standing offer to delete
         * somebody's files. */
        if (cart_rd(2) == 'E' && cart_rd(3) == 'm' && cart_rd(5) == 'T'
            && (cart_rd(510) != 0x55 || cart_rd(511) != 0xAA)) {
            cart_wr(510, 0x55);
            cart_wr(511, 0xAA);
        }
        VU16(0xFF0F38u) = 1;
    }
    /* Sector count in the low eleven bits, and a sequence number above
     * it that swap_watch() bumps when the cartridge changes: 16<<6 is
     * 1024, the largest a cart can report, so the top five are free. */
    VU16(GA_CART_CNT) = (uint16_t)(cart_sectors & 0x07FFu);
    VU16(0xFF0F34u) = cart_sectors;
    /* Which window the probe settled on, so a host dump can tell a
     * cart that is absent from one that was looked for in Mode 2's
     * address space on a Mode 1 boot. */
    VU16(0xFF0F36u) = (uint16_t)(cart_data >> 16);

    /* On a Mode 1 boot all of this has already happened, in the
     * cartridge, before IOFW was entered: Word RAM is the sub's, EmuTOS
     * is planted in it, and the sub is running. There is no SP to
     * command and nothing to wait for -- and waiting anyway is an
     * unbounded spin against a CPU that will never answer, which is
     * exactly where the first Mode 1 build stopped. */
    if (VU32(M1_FLAG) != M1_MAGIC) {
        /* hand Word RAM to the sub so the SP can load the EmuTOS image */
        VU8(GA_MEMMODE) = (uint8_t)(VU8(GA_MEMMODE) | 0x02);   /* DMNA */

        /* command 5: load EMUTOS.IMG, evict the CDBIOS, boot */
        while (VU8(GA_COMFLG_S)) ;
        VU8(GA_COMFLG_M) = 0;
        while (!VU8(GA_COMFLG_S)) ;
        VU8(GA_COMFLG_M) = 5;
        while (VU8(GA_COMFLG_S)) ;  /* SP clears status just before the jump */
        VU8(GA_COMFLG_M) = 0;       /* if the sub ever reboots into the BIOS,
                                       an idle SP must not replay command 5 */
    }

    VU32(REPORT) = 0x45324F4Bu;  /* 'E2OK': handoff completed */

    /* Give EmuTOS an undisturbed boot: no INT2 for ~5 s.
     *
     * This used to be a separate 300-frame loop that also did no screen
     * conversion, and that is why the welcome screen has never been
     * reliably visible. Measured: EmuTOS paints it into the framebuffer
     * at 0x58000 around frame 390 and clears it again around frame 630,
     * and the old quiet loop ran to frame ~690 -- so the picture came
     * and went entirely inside a window in which nothing was converted.
     * A dump of the sub's framebuffer at frame 500 has the full welcome
     * screen in it while the television is showing the backdrop colour,
     * and the pump's own heartbeat is still zero.
     *
     * What the window is actually for is the INT2 -- EmuTOS's VBL --
     * which is now the only thing it withholds. The screen pump runs
     * from the first frame, and it takes the bus through
     * sub_bus_grab_polite() exactly as it does afterwards: the sub
     * raises the interlock when it must not be halted, and the cart
     * proxy below has always grabbed the bus during this window
     * anyway, unconditionally, because B: is mounted inside it. */
    quiet = 300;
#ifdef DIAG_BENCH
    {   /* eight full-screen conversions, counting vblanks as we go */
        uint16_t rep, trow, tcol, k;
        for (k = 0; k < 16000; k++)          /* plausible cache content */
            VU16(CACHE + 2u * (uint32_t)k) = (uint16_t)(0x5AA5u ^ k);
        vbl_edges = 0; hi_half = 0;
#ifdef DIAG_BENCH_BLANK
        vdp_reg(1, 0x04);                    /* display off: no slot waits */
#endif
        for (rep = 0; rep < 8; rep++)
            for (trow = 0; trow < TILE_ROWS; trow++)
                for (tcol = 0; tcol < TILE_COLS; tcol++) {
                    convert_tile(trow, tcol);
                    vbl_tick();
                }
#ifdef DIAG_BENCH_BLANK
        vdp_reg(1, 0x54);
#endif
        VU16(REPORT + 0x30) = vbl_edges;     /* frames for 8 full screens */
    }
#endif
    VU16(REPORT + 0x28) = VU16(GA_STAT1);   /* SP's image checksum */
    VU16(REPORT + 0x2A) = VU16(0xA12024);   /* os_version from the image */

    for (;;) {
        cdd_watch();    /* every frame, so a stalled clock says when */
        uint16_t ndirty = 0;
        uint16_t base_line = (uint16_t)(chunk * 25);
        static uint16_t bootstrap = 24;   /* 3 full sweeps convert all */
        /* ...and again periodically, as a cross-check.
         *
         * The dirty tracking only converts a tile whose bytes changed
         * since the cache last saw them, so a tile the cache captured
         * wrongly once is never revisited. Everything measured says
         * the memory itself is dirty rather than the tracking, but
         * that rests on one read through the same window, and forcing
         * a full sweep every few seconds costs nothing at idle and
         * settles it: if the picture comes clean and stays clean, the
         * memory was fine and the tracking was not. */
        {
            static uint16_t resweep;
            if (++resweep >= 300) { resweep = 0; bootstrap = 24; }
        }

        wait_vblank();
        {
            static uint16_t prevpad;
            uint16_t pad = input_update();     /* publish input for this VBL */

            swap_watch(pad);        /* only while SWAP.PRG has armed it */
            if ((pad & 0x80) && !(prevpad & 0x80))
                osk_toggle();                  /* Start edge */
            if (osk_active())
                osk_input(pad);
            prevpad = pad;
        }

        {   /* The serial keyboard. Drained every frame whether or not
             * the on-screen one is up -- the checkbox is on the OSK,
             * but once it is ticked the point is to close the OSK and
             * type. One scancode a frame, and only into a slot the OSK
             * did not already take this frame. */
            uint8_t sc;
            uart_poll();
            if (osk_slot_free() && (sc = uart_next()) != 0)
                osk_post_key(sc);
            /* Bytes in, bytes that arrived broken, scancodes the queue
             * had no room for. Nowhere to print them with the letterbox
             * retired, so they go where a work-RAM dump can read them:
             * "the port is silent" and "the port is talking and the
             * baud rate is wrong" are different problems. */
            uart_stats((uint16_t *)REPORT + 0x1D,
                       (uint16_t *)REPORT + 0x1E,
                       (uint16_t *)REPORT + 0x1F);
        }
        prn_fill();             /* the page, a chunk a frame, with the bus */
        prn_watchdog();         /* every frame, not only when the cart speaks */

        /* service a pending cart request (needs CART_BANK + the bus) */
        if ((uint8_t)VU16(GA_CART_REQ) != cart_last_seq) {
            sub_bus_grab();
            VU8(GA_MEMMODE) =
                (uint8_t)((VU8(GA_MEMMODE) & ~0xC0u) | (CART_BANK << 6));
            cart_service();
            VU8(GA_MEMMODE) =
                (uint8_t)((VU8(GA_MEMMODE) & ~0xC0u) | (SCREEN_BANK << 6));
            sub_bus_release();
        }

        /* Follow EmuTOS's actual screen base, published each VBL. Falls
         * back to the boot-time address until the first VBL arrives. */
        {
            uint32_t sb = (uint32_t)VU16(GA_STAT0) << 8;
            if (sb >= 0x1000u && sb < 0x80000u) {
                screen_bank = (uint8_t)((sb >> 17) & 3);
                screen_woff = sb & 0x1FFFFu;
            }
        }

        {   /* Heartbeat, for exactly the window where nothing else can
             * speak. Every freeze report so far has left one question
             * open -- is this CPU still running? -- and the pump that
             * normally answers it by repainting is deliberately off
             * while the cartridge moves. So the border answers instead:
             * one CRAM write per frame, a blue that breathes, VDP only,
             * nothing anywhere near the bus the cartridge is on.
             *
             * Border still breathing after a "freeze": this CPU
             * survived the pull and the fault is in what happens next.
             * Border stuck on one shade: the machine died with no code
             * on the danger bus at all, and that is a fact about the
             * connector, not about anything a build can change. */
            static uint16_t hb;
            static uint8_t hb_on;
            if (swap_quiet) {
                uint16_t ramp = (uint16_t)((hb >> 2) & 0x0F);
                if (ramp > 7) ramp = (uint16_t)(15 - ramp);
                VU32(VDP_CTRL) = vdp_cram_w(2 * 16);
                VU16(VDP_DATA) = (uint16_t)((ramp << 9) | 0x0200);
                hb++; hb_on = 1;
            } else if (hb_on) {
                hb_on = 0; hb = 0;
                VU32(VDP_CTRL) = vdp_cram_w(2 * 16);
                VU16(VDP_DATA) = 0x0000;    /* the frame back to black */
            }
        }

        /* copy+diff 25 lines (4000 bytes) of the planar screen */
        if (swap_quiet)
            goto no_copy;       /* the cartridge is moving: read nothing */
        if (payload_hold) {
            /* Staging: the cache is not ours this frame. */
            if (!--payload_hold_ttl) { payload_hold = 0; pump_resync = 1; }
            goto no_copy;
        }

        if (!sub_bus_grab_polite()) {
            VU16(WATCH + 24)++;         /* frames the pump was refused */
            goto no_copy;
        }
        if (pump_resync) {              /* see pump_resync */
            pump_resync = 0;
            bootstrap = 24;             /* three full sweeps, as at boot */
            VU16(WATCH + 46)++;
        }
        VU16(WATCH + 26)++;             /* ...and frames it got the bus */
        VU8(GA_MEMMODE) =
                        /* Bank bits only. Bit 1 is DMNA, and clearing it as a side
             * effect of selecting a bank is wrong on its face -- it
             * asks for Word RAM back, which on a Mode 1 boot is the
             * memory EmuTOS executes from. Kept because it is correct,
             * not because it fixed anything: the picture is identical
             * with and without it, so this was not the fault. */
            (uint8_t)((VU8(GA_MEMMODE) & ~0xC0u) | (screen_bank << 6));

        /* A palette a program can set.
         *
         * The sixteen ST colours go into CRAM once, in vdp_init(), and
         * nothing could ever change them: this machine has no ST
         * palette hardware for Setcolor to write to, so a program that
         * draws its own picture got the desktop's colours whatever it
         * intended. A picture viewer is not worth much like that.
         *
         * The block sits immediately past the framebuffer, in the 32K
         * EmuTOS reserves for the screen and does not use -- 32000
         * bytes of bitmap, then a magic, a generation, and sixteen
         * 0x0RGB words. Being there means it is in the bank the pump
         * has already selected, inside the grab it already holds, so a
         * frame that changes nothing costs two word reads. The screen
         * base is whatever EmuTOS published, so this follows it.
         *
         * Entry 2 of palette line 1 goes with it: that is the opaque
         * copy of ST colour 0 that plane B draws as paper, and leaving
         * it behind would paint the picture's background in the
         * desktop's white. */
        if (screen_woff + 32000u + 38u <= 0x20000u) {
            const volatile uint16_t *pb = (const volatile uint16_t *)
                (PRG_WINDOW + screen_woff + 32000u);

            if (pb[0] == 0x5041u && pb[1] == 0x4C21u   /* "PAL!" */
                && pb[2] != pal_gen) {
                uint16_t i;

                pal_gen = pb[2];
                VU32(VDP_CTRL) = vdp_cram_w(0);
                for (i = 0; i < 16; i++)
                    VU16(VDP_DATA) = st2cram(pb[3 + i]);
                VU32(VDP_CTRL) = vdp_cram_w(2 * 18);
                VU16(VDP_DATA) = st2cram(pb[3]);
            }
        }
        {
            const volatile uint32_t *src = (const volatile uint32_t *)
                (PRG_WINDOW + screen_woff + (uint32_t)base_line * 160u);
            uint32_t *dst = (uint32_t *)(CACHE + (uint32_t)base_line * 160u);
            uint16_t line, g;
            for (line = 0; line < 25; line++) {
                uint16_t trow40 = (uint16_t)(((base_line + line) >> 3) * 40u);
                for (g = 0; g < 20; g++) {      /* one 16px group = 2 longs */
                    uint32_t a = src[0], b = src[1];
                    if (a != dst[0] || b != dst[1] || bootstrap) {
                        uint16_t t = (uint16_t)(trow40 + (g << 1));
                        dst[0] = a; dst[1] = b;
                        /* t is even, so both its tiles share a byte */
                        tdirty[t >> 3] |= (uint8_t)(0xC0u >> (t & 7));
                        ndirty++;
                    }
                    src += 2; dst += 2;
                }
            }
        }
        {   /* The first and last longwords of the screen as the pump
             * sees them. Noise at the top with zeros at the bottom
             * means EmuTOS never cleared the top; zeros at both means
             * it did and the conversion is what is failing. */
            const volatile uint32_t *sp0 = (const volatile uint32_t *)
                (PRG_WINDOW + screen_woff);
            const volatile uint32_t *sp1 = (const volatile uint32_t *)
                (PRG_WINDOW + screen_woff + 31996u);
            {   /* Is it written once, or continuously? A count of the
                 * frames on which the first longword differs from the
                 * last frame's separates "something scribbled here
                 * during boot" from "something is scribbling here
                 * now", and those are different bugs. */
                uint32_t now = sp0[0];
                if (now != VU32(WATCH + 32)) VU16(WATCH + 44)++;
                VU32(WATCH + 32) = now;
            }
            VU32(WATCH + 36) = sp1[0];
        }
        sub_bus_release();
no_copy:
        if (quiet) {
            /* Last frame of the quiet window: whatever EmuTOS painted
             * while its VBL was withheld is on screen now, so make the
             * next sweep unconditional rather than trusting a cache
             * built while nothing was driving the picture. */
            if (--quiet == 0)
                bootstrap = 24;
        } else {
            VU8(GA_IFL2) = 0x01; /* EmuTOS VBL — after the bus is back */
        }

        {   /* Convert every marked tile from the local cache. Scanned a
             * byte at a time: 40 tiles per row is exactly 5 bytes, so a
             * clean row costs five tests instead of forty. Testing every
             * bit individually cost enough to push the whole loop past a
             * vblank, which halved the frame rate at idle. */
            uint16_t trow;
            uint8_t *p = tdirty;
            for (trow = 0; trow < TILE_ROWS; trow++, p += TILE_BYTES) {
                uint8_t k;
                for (k = 0; k < TILE_BYTES; k++) {
                    uint8_t m = p[k];
                    if (m) {
                        uint8_t b;
                        p[k] = 0;
                        for (b = 0; b < 8; b++)
                            if (m & (uint8_t)(0x80u >> b))
                                convert_tile(trow, (uint16_t)(k * 8u + b));
                    }
                }
            }
        }

        if (bootstrap) bootstrap--;
        /* Counted here rather than at the top of the loop: a frame the
         * pump was refused the bus is a frame it swept nothing, and it
         * must not count towards the sweep staging is waiting for. */
        if (payload_flush && !--payload_flush) {
            payload_hold = 1;           /* VRAM is the framebuffer now */
            payload_hold_ttl = 600;
        }
        VU16(REPORT + 4) = ndirty;
        VU16(REPORT + 6)++;                     /* heartbeat */

        /* The sub's telemetry, mirrored where the test harness can read
         * it as numbers. Reading it off the rendered status line means
         * reading a bitmap font out of a screenshot, which is slow,
         * lossy, and exactly the kind of instrument that has already
         * cost this port a wrong conclusion. */
        VU16(REPORT + 0x32) = VU16(0xA12022);
        VU16(REPORT + 0x34) = VU16(0xA12028);
        VU16(REPORT + 0x36) = VU16(0xA1202A);

#if IOFW_HUD
        {   /* Status in the letterbox, ~4x a second.
             *
             * There are exactly three rows to work with: the display is
             * 224 lines, the ST screen is 200 of them, and 24 lines is
             * three tiles. So the layout is what fits, not what would
             * be nice -- and the bottom row is left blank on purpose,
             * because a CRT overscans and whatever is on row 27 is
             * half-eaten at the edge of the tube. The row that matters
             * goes at the top of the three, where it can be read.
             *
             * The B: request and result fields that used to have a row
             * of their own are gone until stage 4 needs them again.
             * Retiring a spent instrument beats keeping it and losing
             * the one in use. */
            static uint16_t tick;
            if (++tick >= 15) {
                static const char hex[] = "0123456789ABCDEF";

                {   /* Row 25: the disc read. Top of the letterbox,
                     * because this is the row being read off a
                     * photograph of a television.
                     *
                     *   CDs Vvv Aa Bb Iii Hmmssff Wmmssff -A Rn
                     *
                     * s   state: . C S Y X P
                     * vv  verdict: 03 is both sectors read and right
                     * a b how each of the two reads ended, 0 = fine
                     * ii  sectors the decoder reported, last read
                     * H   header of the last sector it decoded
                     * W   the header that read was waiting for
                     * F   it took a sector it had not asked for
                     * A/B which CDC control pair (B held picks the other)
                     * Rn  times the drive landed past the sector
                     *
                     * H against W is the whole diagnosis when a read
                     * finds nothing: equal means the matching is at
                     * fault, different means the drive is elsewhere,
                     * zeroes mean the decoder is not decoding. */
                    static char l1[41];
                    static const char t1[] =
                        "CD. V00 A0 B0 I00 H000000 W000000 0-A R0";
                    static const char st[] = ".CSYXP";
                    uint8_t k;
                    for (k = 0; k < 40; k++) l1[k] = t1[k];
                    l1[40] = 0;
                    if (VU16(CDSTAT_WRAM) == 0xCDC0u) {
                        uint16_t w1 = VU16(CDSTAT_WRAM + 2);
                        uint16_t hd0 = VU16(CDSTAT_WRAM + 4);
                        uint16_t hd2 = VU16(CDSTAT_WRAM + 6);
                        uint16_t vd = VU16(CDSTAT_WRAM + 14);
                        uint16_t wt0 = VU16(CDSTAT_WRAM + 32);
                        uint16_t wt2 = VU16(CDSTAT_WRAM + 34);
                        uint16_t fl = VU16(CDSTAT_WRAM + 40);
                        uint16_t fc = VU16(CDSTAT_WRAM + 44);
                        uint8_t st8 = (uint8_t)(w1 >> 12);
                        l1[2] = (st8 < 6) ? st[st8] : 'X';
                        l1[5] = hex[(vd >> 4) & 0xF];
                        l1[6] = hex[vd & 0xF];
                        l1[9]  = hex[(fl >> 8) & 0xF];     /* first read */
                        l1[12] = hex[fl & 0xF];            /* second read */
                        l1[15] = hex[(w1 >> 8) & 0xF];     /* sectors seen */
                        l1[16] = hex[(w1 >> 4) & 0xF];
                        l1[19] = hex[(hd0 >> 12) & 0xF];   /* head M */
                        l1[20] = hex[(hd0 >> 8) & 0xF];
                        l1[21] = hex[(hd0 >> 4) & 0xF];    /* head S */
                        l1[22] = hex[hd0 & 0xF];
                        l1[23] = hex[(hd2 >> 12) & 0xF];   /* head F */
                        l1[24] = hex[(hd2 >> 8) & 0xF];
                        l1[27] = hex[(wt0 >> 12) & 0xF];   /* want M */
                        l1[28] = hex[(wt0 >> 8) & 0xF];
                        l1[29] = hex[(wt0 >> 4) & 0xF];    /* want S */
                        l1[30] = hex[wt0 & 0xF];
                        l1[31] = hex[(wt2 >> 12) & 0xF];   /* want F */
                        l1[32] = hex[(wt2 >> 8) & 0xF];
                        l1[34] = (fc & 0xFF00) ? 'F' : '-';
                        l1[35] = (fc & 1) ? 'B' : 'A';
                        l1[38] = hex[(fc >> 4) & 0xF];
                        {   /* What the drive says about itself: nibble 0
                             * of its status packet, which the BIOS
                             * dispatches sixteen ways on and this
                             * driver has never looked at. */
                            uint16_t ds = VU16(CDSTAT_WRAM + 58);
                            uint16_t gs = VU16(CDSTAT_WRAM + 60);
                            /* what the drive says now, and -- if a read
                             * has been refused -- what it said then */
                            l1[36] = (gs & 0xFF00) ? '_'
                                   : hex[(ds >> 8) & 0xF];
                            if (gs & 0xFF)
                                l1[37] = hex[gs & 0xF];
                        }
                    }
                    osk_status(l1);
                }

                {   /* Row 26: the CD link, D:, and the rest.
                     *
                     *   CD sivp Kxy Rc Dd Snn Hnn Enn BWMS ..
                     *
                     * s    link score: valid packets in the last 16, F
                     *      clean, 0 nothing; S if level 4 had to be
                     *      switched off, Q if the drive went quiet
                     * i v  the drive interrupt's heartbeat and the sub
                     *      CPU's own -- between them they name which
                     *      side stopped, which one counter cannot
                     * p    which question is being asked
                     * Kxy  the two checksums, ours and the drive's
                     * R    the round trip, proven or not
                     * c    DTS/DRS as last sampled, and ever seen
                     * Dd   D:, the disc filesystem. Y a block came out,
                     *      L the dead-drive latch is refusing reads
                     *      without asking the drive, X tried and no
                     *      block, - nothing has been tried.
                     * Snn  seeks, Hnn sectors merely waited for
                     *      -- waits should outnumber seeks by a lot, or
                     *      D: is not a disk but a punishment
                     * Enn  reads that gave up
                     * BWMS backup RAM, its writes, mouse, sound
                     *
                     * The servant's own frame counter used to sit here.
                     * The screen redrawing at all says the same thing,
                     * and D: needed the room. */
                    static char l2[41];
                    static const char t2[] =
                        "CD 0000 K00 -0 D- S00 H00 E00 NNNN ..    ";
                    uint16_t cd = VU16(0xA12022), ms = VU16(0xA12028);
                    uint16_t sw = VU16(0xA1202A);
                    uint8_t k;
                    for (k = 0; k < 40; k++) l2[k] = t2[k];
                    l2[40] = 0;
                    /* Three different failures used to print as '0': no
                     * valid packets, the interrupt having stopped, and
                     * nothing latched yet. Reading the first of those
                     * when it was the second cost several rounds. */
                    l2[3] = (sw & 0x100) ? 'S'
                          : (sw & 0x8000) ? 'Q' : hex[(cd >> 12) & 0xF];
                    l2[4] = hex[(cd >> 8) & 0xF];        /* CDD irqs */
                    l2[5] = hex[(cd >> 4) & 0xF];        /* sub VBLs */
                    l2[6] = hex[cd & 0xF];               /* probe */
                    l2[9]  = hex[(ms >> 8) & 0xF];       /* checksum: ours */
                    l2[10] = hex[(ms >> 4) & 0xF];       /* checksum: theirs */
                    /* Bit 0 alone. Bit 1 means a disc read is under way,
                     * and testing the whole nibble would have called the
                     * round trip proven the moment one started. */
                    l2[12] = (ms & 1) ? 'Y' : '-';
                    l2[13] = hex[(sw >> 9) & 7];
                    if (VU16(CDSTAT_WRAM) == 0xCDC0u) {
                        uint16_t sk = VU16(CDSTAT_WRAM + 48);
                        uint16_t ht = VU16(CDSTAT_WRAM + 50);
                        uint16_t er = VU16(CDSTAT_WRAM + 52);
                        uint16_t dt = VU16(CDSTAT_WRAM + 54);
                        /* Y beats everything: a block came out of the
                         * disc, which is the thing being waited for and
                         * it is sticky once true. Then L, the dead-drive
                         * latch -- reads are being refused in the block
                         * layer without the drive being asked, which is
                         * what "always DX" has actually meant since the
                         * boot probe set bit 0. Then X, tried and
                         * failed; then nothing tried at all. */
                        l2[16] = (dt & 2) ? 'Y'
                               : (dt & 0x100) ? 'L'
                               : (dt & 1) ? 'X' : '-';
                        l2[19] = hex[(sk >> 4) & 0xF];
                        l2[20] = hex[sk & 0xF];
                        l2[23] = hex[(ht >> 4) & 0xF];
                        l2[24] = hex[ht & 0xF];
                        l2[27] = hex[(er >> 4) & 0xF];
                        l2[28] = hex[er & 0xF];
                    }
                    /* B: N none, Y ours (mounted), S Sega saves kept */
                    l2[30] = ((sw & 0xF) == 1) ? 'Y'
                           : ((sw & 0xF) == 2) ? 'S' : 'N';
                    l2[31] = (sw & 0x10) ? 'Y' : 'N';    /* writes verified */
                    l2[32] = mouse_seen ? 'Y' : 'N';
                    l2[33] = (sw & 0x20) ? ((sw & 0x40) ? 'Y' : 'I') : 'N';
                    /* Both ends of the passive-link switch: what the pad
                     * said here, and what the sub decided from it. They
                     * disagreed once, silently. */
                    l2[35] = a_held ? (b_held ? '*' : 'A')
                                    : (b_held ? 'B' : '.');
                    l2[36] = (sw & 0x80) ? 'C' : '.';
                    /* Wx: what the cartridge loader's drive wake did.
                     *
                     *   -  Down was not held, so it never ran
                     *   A  awake -- DRV_INIT returned, read inconclusive
                     *   R  ...and the firmware read LBA 1600 itself
                     *   Q  ...at 1600+150: our lead-in base is wrong
                     *   X  ...and the firmware cannot read it either
                     *   T  the firmware never called the stub at all
                     *   S  the stub ran; DRV_INIT never returned
                     *   H  a reset/bus handover was not acknowledged
                     *   B  no packed CDBIOS found in the boot ROM
                     *   U  it unpacked, but not into firmware
                     *
                     * On the screen because the alternative was the
                     * cartridge's save RAM, and reading that means
                     * powering off and pulling the cart -- to answer
                     * "did the button register". */
                    l2[37] = 'W';
                    l2[38] = (char)VU8(0xFF09B2u);
                    /* ...and the drive state the wake last saw, which
                     * is what Doom CD32X tests every frame: 1 or 4 is
                     * a drive you can talk to, anything else is one it
                     * re-initialises out of. */
                    l2[39] = (char)VU8(0xFF09B3u);
                    osk_status2(l2);
                }

                /* Row 27: nothing. The tube eats it. */
                osk_status3("");

                tick = 0;
            }
        }
#endif /* IOFW_HUD */

        {   /* boot-trace ring: log changes of the sub's STAT0 marker */
            static uint16_t last = 0; static uint16_t n = 0;
            uint16_t cur = VU16(GA_STAT0);
            if (cur != last) {
                last = cur;
                if (n < 32) VU16(0xFF0E00u + 2u * n) = cur;  /* first 32 */
                n++;
                VU16(0xFF0E40u) = n;
            }
        }

        /* The disc read. Bit 1 of the sub's CDD word says there is
         * something to fetch; the report itself lives in PRG-RAM because
         * the comm registers were full. Fetched four times a second,
         * which is as often as anything looks at it, and the sector
         * itself is copied once, when the report says it is there.
         *
         * The sector goes to a fixed place in work RAM so the test
         * harness can diff it against the image the disc was built
         * from. That is the whole point of stage 3: "did we get sector
         * N" should be a comparison, not a squint at a status line. */
        if (!swap_quiet && (VU16(0xA12028) & 0x0002)
                && (VU16(REPORT + 6) & 15u) == 0) {
            static uint16_t cd_have = 0xFFFF;   /* slot already copied */
            uint8_t save = VU8(GA_MEMMODE);
            if (sub_bus_grab_polite()) {
                uint16_t k;
                VU8(GA_MEMMODE) = (uint8_t)(((save & ~0xC2u)
                                    | (CDSECT_BANK << 6)) & ~0x02u);
                for (k = 0; k < CDREPORT_WORDS; k++)
                    VU16(CDSTAT_WRAM + 2u * k) =
                        VU16(PRG_WINDOW + CDREPORT_WOFF + 2u * k);
                /* Word 6 says which of the sub's target sectors is in
                 * the buffer. It changes when a new one lands, and the
                 * sub holds each one for two seconds -- eight of these
                 * fetches -- before overwriting it. The magic guards
                 * against reading a bank that was never written. */
                if (VU16(CDSTAT_WRAM) == 0xCDC0u
                        && VU16(CDSTAT_WRAM + 12) != 0xFFFFu
                        && VU16(CDSTAT_WRAM + 12) != cd_have) {
                    cd_have = VU16(CDSTAT_WRAM + 12);
                    for (k = 0; k < 512; k++)
                        VU32(CDSECT_WRAM + 4u * k) =
                            VU32(PRG_WINDOW + CDSECT_WOFF + 4u * k);
                }
                VU8(GA_MEMMODE) = (uint8_t)(save & ~0x02u);
                sub_bus_release();
            }
        }

        /* one-shot probe: the same offset through all four banks proves
         * whether rebanking works, then bank-0 sysvars mean something */
        if (VU16(REPORT + 6) == 900 && !swap_quiet) {
            uint8_t save = VU8(GA_MEMMODE);
            uint8_t bk;
            sub_bus_grab();
            for (bk = 0; bk < 4; bk++) {
                VU8(GA_MEMMODE) =
                    (uint8_t)(((save & ~0xC2u) | (bk << 6)) & ~0x02u);
                VU32(REPORT + 8 + 4u * bk) = VU32(PRG_WINDOW + 0x44E);
            }
            VU8(GA_MEMMODE) = (uint8_t)((save & ~0xC2u));      /* bank 0 */
            {   /* dump PRG 0x000-0x1FF to WRAM 0xFF0A00 for the host */
                uint16_t k;
                for (k = 0; k < 128; k++)
                    VU32(0xFF0A00u + 4u * k) = VU32(PRG_WINDOW + 4u * k);
            }
            VU8(GA_MEMMODE) = (uint8_t)(save & ~0x02u);        /* restore */
            sub_bus_release();
        }

        chunk = (uint16_t)((chunk + 1) & 7);
    }
}
