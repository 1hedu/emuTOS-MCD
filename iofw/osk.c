/* On-screen keyboard on the VDP Window plane.
 *
 * The Window plane overlays plane A in a fixed screen region without the
 * sub CPU (running stock EmuTOS) knowing. Start toggles it; while up, the
 * d-pad walks a QWERTY grid and A "presses" a key, publishing an Atari
 * scancode to the sub through GA_KEY. When down, the d-pad drives the
 * pointer as before.
 *
 * Layout: keys are 2 tiles wide (glyph + spacer). Window occupies rows
 * 18..27. Glyph tiles are uploaded to VRAM tile OSK_TILE0; the selected
 * key uses palette line 3, the rest line 2.
 */
#include "hw.h"
#include "osk_font.h"

#define WIN_NAMETAB 0xB000u          /* window plane nametable */
/* First window row. 17 rather than 18, and the keyboard's last line
 * is row 26 rather than 27, because a television does not show row 27:
 * the console owner's set cuts it off, which is what overscan does to
 * the last scanline of a 224-line frame on most CRTs. So the bottom
 * screen row is left deliberately empty and the keyboard sits one row
 * higher. It costs one more row of the ST screen while the keyboard is
 * open, which is covered by the keyboard anyway. */
#define WIN_ROW     17               /* first window row (0..27) */
#define WIN_LABEL   26               /* the UART line, above the blank */
#define OSK_TILE0   1024u            /* glyph tiles start here */
/* The key face, as a colour index rather than a hole. See
 * osk_upload_tiles: the font's background pixels are index 0, which is
 * transparent on this VDP whatever the palette says, so the face was
 * never drawn -- it was the backdrop seen through the keyboard. They
 * are remapped to this on the way into VRAM. 3, not 2, because palette
 * line 1 entry 2 belongs to the screen's paper tile. */
#define GLYPH_BG    3u

#define GLYPH_DIGIT0 1               /* '0' is glyph 1 */
#define GLYPH_A      11
#define GLYPH_DOT    37
#define GLYPH_MINUS  38
#define GLYPH_USCORE 39
#define GLYPH_SHIFT  40
#define GLYPH_DEL    41
#define GLYPH_ENTER  42
#define GLYPH_CLOSE  43
#define GLYPH_BOX    44              /* the UART checkbox, clear */
#define GLYPH_BOXON  45              /* ...and set */
#define GLYPH_UP     46
#define GLYPH_DOWN   47
#define GLYPH_LEFT   48
#define GLYPH_RIGHT  49
#define GLYPH_ESC    50

/* Scancodes the keyboard uses for itself rather than sending on. Real
 * ST scancodes stop well below these. */
#define KEY_CLOSE   0xFF
#define KEY_UART    0xFE

uint8_t uart_active(void);
void uart_enable(uint8_t on);
static uint8_t glyph_for(char c);

/* A key: glyph index, Atari scancode, cell width (in 2-tile units). */
struct key { uint8_t glyph; uint8_t scan; uint8_t w; };

/* Atari ST scancodes */
#define SC_1 0x02
#define SC_Q 0x10
#define SC_A 0x1E
#define SC_Z 0x2C
#define SC_SPACE 0x39
#define SC_ENTER 0x1C
#define SC_BS    0x0E
#define SC_LSHIFT 0x2A
#define SC_DOT 0x34
#define SC_MINUS 0x0C
/* Cursor keys. Without them no full-screen program can move a caret,
 * and this keyboard is the only one most of these consoles will ever
 * have -- EDIT.PRG needs them and so will anything like it. */
#define SC_UP    0x48
#define SC_DOWN  0x50
#define SC_LEFT  0x4B
#define SC_RIGHT 0x4D
/* Escape. Every other key on this keyboard is text, so a program that
 * wants a command has nothing to listen for; EDIT.PRG uses this one to
 * reach save and quit without stealing a printable character. */
#define SC_ESC   0x01

/* rows of the keyboard; scancodes run in keyboard order per ST layout */
static const struct key row0[] = {
    {2,0x02,1},{3,0x03,1},{4,0x04,1},{5,0x05,1},{6,0x06,1},
    {7,0x07,1},{8,0x08,1},{9,0x09,1},{10,0x0A,1},{1,0x0B,1} };
static const struct key row1[] = {
    {GLYPH_A+16,SC_Q,1},{GLYPH_A+22,0x11,1},{GLYPH_A+4,0x12,1},
    {GLYPH_A+17,0x13,1},{GLYPH_A+19,0x14,1},{GLYPH_A+24,0x15,1},
    {GLYPH_A+20,0x16,1},{GLYPH_A+8,0x17,1},{GLYPH_A+14,0x18,1},
    {GLYPH_A+15,0x19,1} };
static const struct key row2[] = {
    {GLYPH_A+0,SC_A,1},{GLYPH_A+18,0x1F,1},{GLYPH_A+3,0x20,1},
    {GLYPH_A+5,0x21,1},{GLYPH_A+6,0x22,1},{GLYPH_A+7,0x23,1},
    {GLYPH_A+9,0x24,1},{GLYPH_A+10,0x25,1},{GLYPH_A+11,0x26,1} };
static const struct key row3[] = {
    {GLYPH_A+25,SC_Z,1},{GLYPH_A+23,0x2D,1},{GLYPH_A+2,0x2E,1},
    {GLYPH_A+21,0x2F,1},{GLYPH_A+1,0x30,1},{GLYPH_A+13,0x31,1},
    {GLYPH_A+12,0x32,1},{GLYPH_DOT,SC_DOT,1},{GLYPH_MINUS,SC_MINUS,1} };
static const struct key row4[] = {
    {GLYPH_SHIFT,SC_LSHIFT,2},{0,SC_SPACE,4},{GLYPH_USCORE,0x0D,1},
    {GLYPH_DEL,SC_BS,1},{GLYPH_ENTER,SC_ENTER,2},
    {GLYPH_LEFT,SC_LEFT,1},{GLYPH_DOWN,SC_DOWN,1},
    {GLYPH_UP,SC_UP,1},{GLYPH_RIGHT,SC_RIGHT,1},{GLYPH_ESC,SC_ESC,1},
    {GLYPH_CLOSE,KEY_CLOSE,1},{GLYPH_BOX,KEY_UART,1} };

static const struct key *const rows[5] =
    { row0, row1, row2, row3, row4 };
static const uint8_t rowlen[5] = { 10, 10, 9, 9, 12 };

/* left indent (in tiles) per row, for a centred keyboard with the
 * classic QWERTY stagger; keys are 2 tiles wide each */
static const uint8_t rowx[5] = { 9, 9, 11, 12, 4 };

static uint8_t osk_on;
static uint8_t sel_r, sel_c;
static uint8_t key_seq;

void osk_upload_tiles(void)
{
    uint16_t i;
    const uint8_t *p = osk_font;

    /* Glyph tiles -- the VDP data port takes 16-bit words, not bytes.
     *
     * Every background pixel becomes GLYPH_BG on the way in. The font
     * is ink on index 0, and index 0 is transparent: for as long as the
     * backdrop was ST colour 0 that looked like a light key face, and
     * it stopped looking like one the moment the backdrop went black to
     * put a frame around the screen. Drawing the face costs nothing and
     * makes the keyboard independent of whatever is behind it, which
     * also means a selected key could be distinguished by its face and
     * not only its ink. */
    VU32(VDP_CTRL) = vdp_vram_w(OSK_TILE0 * 32);
    for (i = 0; i < OSK_GLYPH_COUNT * 16u; i++) {
        uint8_t a = p[0], b = p[1];
        uint8_t ah = (uint8_t)(a >> 4), al = (uint8_t)(a & 15);
        uint8_t bh = (uint8_t)(b >> 4), bl = (uint8_t)(b & 15);
        if (!ah) ah = GLYPH_BG;
        if (!al) al = GLYPH_BG;
        if (!bh) bh = GLYPH_BG;
        if (!bl) bl = GLYPH_BG;
        VU16(VDP_DATA) = (uint16_t)((ah << 12) | (al << 8) | (bh << 4) | bl);
        p += 2;
    }

    /* OSK palette: line 2 = key face, line 3 = selected.
     *
     * Byte addresses, not entry numbers. CRAM is 64 words and the VDP
     * control port takes the byte address, so a palette line starts
     * every 32 -- megadev loads its second palette at to_vdp_addr(32),
     * not 16 (examples/gfx/src/cybercity.c).
     *
     * This was written as 32 and 48, which are lines 1 and 1-again, so
     * lines 2 and 3 were never written by anything: the keyboard drew
     * in whatever CRAM happened to hold, and the selected key could not
     * differ from the rest because its palette line did not exist. It
     * looked deliberate for four days -- blue glyphs on a CD boot,
     * black on a Mode 1 boot, same code both times, because the two
     * boot paths leave different rubbish in CRAM.
     *
     * And the highlight is the ink, not the face. Colour index 0 is
     * transparent on this VDP whatever the palette line says, so the
     * "light key face" was never a colour at all -- it is the backdrop
     * showing through, which is why every key looks the same shade as
     * the letterbox around them. A selected key distinguished by its
     * background therefore could not exist even once its palette line
     * did. Entry 1 is the only one that renders, so entry 1 is the
     * one that changes. */
    VU32(VDP_CTRL) = vdp_cram_w(2 * 32);      /* palette line 2, entry 0 */
    VU16(VDP_DATA) = 0x0000;                   /* 0: transparent regardless */
    VU16(VDP_DATA) = 0x0000;                   /* 1: ink, black */
    VU16(VDP_DATA) = 0x0000;                   /* 2: unused */
    VU16(VDP_DATA) = 0x0EEE;                   /* 3: the key face */
    VU32(VDP_CTRL) = vdp_cram_w(2 * 48);      /* palette line 3, selected */
    VU16(VDP_DATA) = 0x0000;
    VU16(VDP_DATA) = 0x008E;                   /* 1: ink, orange */
    VU16(VDP_DATA) = 0x0000;
    VU16(VDP_DATA) = 0x0EEE;                   /* 3: the key face */

    vdp_reg(18, 0x00);                          /* window off until toggled */
}

/* Draw the whole keyboard into the window nametable. */
static void osk_draw(void)
{
    uint16_t r, base;

    uint16_t scr;

    /* clear the whole window region to the light key-face background */
    for (scr = WIN_ROW; scr < 28; scr++) {
        uint8_t col;
        VU32(VDP_CTRL) = vdp_vram_w((uint16_t)(WIN_NAMETAB + scr * 64 * 2));
        for (col = 0; col < 40; col++)
            VU16(VDP_DATA) = (uint16_t)(0x4000 | (OSK_TILE0 + 0));
    }

    /* The label row: a checkbox on its own is a mystery, so say what it
     * does. Row 27 below it stays blank for the television. */
    {
        static const char lbl_off[] = "UART KEYBOARD OFF";
        static const char lbl_on[]  = "UART KEYBOARD ON";
        const char *lbl = uart_active() ? lbl_on : lbl_off;
        uint8_t col, n = 0;
        while (lbl[n]) n++;
        VU32(VDP_CTRL) = vdp_vram_w((uint16_t)(WIN_NAMETAB + WIN_LABEL * 64 * 2));
        for (col = 0; col < 40; col++) {
            uint8_t g = (col >= 11 && col < (uint8_t)(11 + n))
                        ? glyph_for(lbl[col - 11]) : 0;
            VU16(VDP_DATA) = (uint16_t)(0x4000 | (OSK_TILE0 + g));
        }
    }

    /* The row the television eats. Transparent tiles, not the key face,
     * so what shows through is the black frame rather than a grey bar
     * hanging below the keyboard on sets that do display it. */
    {
        uint8_t col;
        VU32(VDP_CTRL) = vdp_vram_w((uint16_t)(WIN_NAMETAB + 27 * 64 * 2));
        for (col = 0; col < 40; col++)
            VU16(VDP_DATA) = TILE_BLANK;
    }

    /* keys: 2*w tiles wide, glyph centred in the cell, one blank screen
     * row between keyboard rows. Row r occupies screen row WIN_ROW+r*2,
     * indented by rowx[r] for the staggered, centred look. */
    for (r = 0; r < 5; r++) {
        const struct key *k = rows[r];
        uint8_t n = rowlen[r], c, col = rowx[r];
        for (c = 0; c < n; c++) {
            uint16_t pal = (r == sel_r && c == sel_c) ? 0x6000 : 0x4000;
            uint8_t w = (uint8_t)(k[c].w * 2), j, gpos = (uint8_t)(w / 2);
            base = (uint16_t)(WIN_NAMETAB + (WIN_ROW + r * 2) * 64 * 2 + col * 2);
            VU32(VDP_CTRL) = vdp_vram_w(base);
            for (j = 0; j < w; j++) {
                uint16_t g = (j == gpos - 1) ? k[c].glyph : 0;   /* glyph left of centre */
                if (g == GLYPH_BOX && uart_active()) g = GLYPH_BOXON;
                VU16(VDP_DATA) = (uint16_t)(pal | (OSK_TILE0 + g));
            }
            col = (uint8_t)(col + w);
        }
    }
}

/* Show/hide by moving the window plane on or off screen. */
static void osk_window(uint8_t on)
{
    vdp_reg(3, WIN_NAMETAB >> 10);            /* window nametable base */
    vdp_reg(17, 0x00);                        /* window H: full width */
    vdp_reg(18, on ? (0x80 | WIN_ROW) : 0x00);/* window V: WIN_ROW..27 */
}

void osk_toggle(void)
{
    osk_on ^= 1;
    if (osk_on) { osk_draw(); osk_window(1); }
    else osk_window(0);
}

uint8_t osk_active(void) { return osk_on; }

/* The only writer of GA_KEY. The sub takes one scancode per VBL -- it
 * notices a new one by the sequence byte, and a second write in the
 * same frame would replace the first before it was ever read -- so the
 * on-screen keyboard and the serial port have to go through one place
 * that knows whether the slot is already spoken for this frame. */
static uint8_t posted;

void osk_post_key(uint8_t sc)
{
    key_seq++;
    VU16(GA_KEY) = (uint16_t)((sc << 8) | key_seq);
    posted = 1;
}

uint8_t osk_slot_free(void)
{
    uint8_t was = posted;
    posted = 0;
    return (uint8_t)!was;
}

/* Handle one frame of pad input while the OSK is up. Consumes edges from
 * the raw pad word (SACBRLDU in our packing). Returns nonzero if it used
 * the input (so the caller suppresses pointer motion). */
uint8_t osk_input(uint16_t pad)
{
    static uint16_t prev;
    uint16_t e = pad & (uint16_t)~prev;
    prev = pad;

    if (!osk_on) return 0;

    if (e & 0x04) { if (sel_c) sel_c--; osk_draw(); }               /* L */
    if (e & 0x08) { if (sel_c + 1 < rowlen[sel_r]) sel_c++; osk_draw(); } /* R */
    if (e & 0x01) { if (sel_r) sel_r--;                              /* U */
                    if (sel_c >= rowlen[sel_r]) sel_c = rowlen[sel_r]-1;
                    osk_draw(); }
    if (e & 0x02) { if (sel_r + 1 < 5) sel_r++;                      /* D */
                    if (sel_c >= rowlen[sel_r]) sel_c = rowlen[sel_r]-1;
                    osk_draw(); }
    if (e & 0x40) {                                                  /* A: press */
        uint8_t sc = rows[sel_r][sel_c].scan;
        if (sc == KEY_CLOSE) { osk_toggle(); return 1; }
        if (sc == KEY_UART) {           /* the serial keyboard, on or off */
            uart_enable((uint8_t)!uart_active());
            osk_draw();
            return 1;
        }
        osk_post_key(sc);
    }
    return 1;
}

/* ---- hardware diagnostics ------------------------------------------
 * The ST screen is 320x200 inside a 320x224 display, so plane A rows
 * 25-27 are unused letterbox. Row 25 carries a status line for real
 * hardware, where there is no debugger and no WRAM dump. Palette line
 * 1 is set to green-on-black so it reads as an overlay, not as part of
 * the desktop. */
#define NAMETAB_A 0xC000u

static uint8_t glyph_for(char c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(GLYPH_DIGIT0 + (c - '0'));
    if (c >= 'A' && c <= 'Z') return (uint8_t)(GLYPH_A + (c - 'A'));
    if (c == '.') return GLYPH_DOT;
    if (c == '-') return GLYPH_MINUS;
    if (c == '_') return GLYPH_USCORE;
    return 0;                       /* space and anything unmapped */
}

void osk_diag_init(void)
{
    /* Byte address again -- see osk_init. 16 is entry 8 of palette line
     * ZERO, which is one of the ST's sixteen colours: this was quietly
     * overwriting the desktop's palette every time it ran, and getting
     * away with it only because the converter rewrites line 0 whenever
     * EmuTOS changes a colour. */
    VU32(VDP_CTRL) = vdp_cram_w(2 * 16);   /* palette line 1, entry 0 */
    VU16(VDP_DATA) = 0x0000;               /* 0: black */
    VU16(VDP_DATA) = 0x00E0;               /* 1: green */
    /* 2 is the screen's paper -- vdp_init owns it, do not write it here.
     * 3 is the glyph background, and on this plane the text should read
     * as it always has: green on black. */
    VU32(VDP_CTRL) = vdp_cram_w(2 * 19);
    VU16(VDP_DATA) = 0x0000;               /* 3: black */
}

/* One row of plane A, blank-padded to the full 40 columns. Everything
 * that puts text on the screen goes through here. */
void osk_row(uint16_t row, const char *s)
{
    uint16_t i;
    VU32(VDP_CTRL) = vdp_vram_w((uint16_t)(NAMETAB_A + row * 64 * 2));
    for (i = 0; i < 40; i++) {
        uint8_t g = s[i] ? glyph_for(s[i]) : 0;
        VU16(VDP_DATA) = (uint16_t)(0x2000 | (OSK_TILE0 + g));
        if (!s[i]) { /* pad the rest of the row with blanks */
            uint16_t j;
            for (j = i + 1; j < 40; j++)
                VU16(VDP_DATA) = (uint16_t)(0x2000 | OSK_TILE0);
            break;
        }
    }
}

void osk_status(const char *s)
{
    osk_row(25, s);
}

/* Second diagnostic row (plane A row 26), for storage telemetry. */
void osk_status2(const char *s)
{
    osk_row(26, s);
}

/* Third diagnostic row (plane A row 27, the last one in the letterbox),
 * for the disc read. Its own row because the CD failure modes need more
 * than the two characters row 26 had left, and a diagnosis squeezed
 * into two characters is how the last three rounds of this got read
 * wrong. */
void osk_status3(const char *s)
{
    osk_row(27, s);
}
