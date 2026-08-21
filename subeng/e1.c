/* E1 sub-CPU engine: renders an authentic ST-low planar screen (320x200,
 * 4 interleaved bitplanes, 32000 bytes) and converts it to Genesis 4bpp
 * tiles in the shared PRG bank the main CPU's window is pinned on.
 * This is the performance-critical routine of the whole EmuTOS port —
 * the benchmark below is E1's go/no-go measurement.
 */
#include <stdint.h>

#define VU8(a)  (*(volatile uint8_t  *)(a))
#define VU16(a) (*(volatile uint16_t *)(a))

/* sub-side comm registers */
#define CMD0  0xFF8010u   /* main->sub */
#define CMD1  0xFF8012u
#define STAT0 0xFF8020u   /* sub->main */
#define STAT1 0xFF8022u

#define E1_MAGIC      0x1E51
#define E1_CMD_BENCH  0x0001
#define E1_CMD_STREAM 0x0002
#define E1_ST_BENCH_DONE 0x2222

#define PLANAR 0x20000u   /* ST-low screen buffer (ST-RAM territory) */
#define FBTILE 0x60000u   /* tile framebuffer, PRG bank 3 = main window */

/* tab[plane][nibble] spreads 4 plane bits into 4 nibble positions:
 * source bit 3 (leftmost pixel) -> output bits 12+plane, etc. */
static uint16_t tab[4][16];

static void tab_init(void)
{
    uint16_t p, n, j, v;
    for (p = 0; p < 4; p++)
        for (n = 0; n < 16; n++) {
            v = 0;
            for (j = 0; j < 4; j++)
                if (n & (1u << (3 - j)))
                    v |= (uint16_t)(1u << p) << (12 - 4 * j);
            tab[p][n] = v;
        }
}

/* One full ST-low screen -> 1000 tiles (25 rows x 40). */
static void convert_full(void)
{
    const uint16_t *src = (const uint16_t *)PLANAR;
    uint8_t *rowbase = (uint8_t *)FBTILE;
    uint16_t y, g;
    const uint16_t *t0 = tab[0], *t1 = tab[1], *t2 = tab[2], *t3 = tab[3];

    for (y = 0; y < 200; y++) {
        uint8_t *row = rowbase + ((y & 7) << 2);
        for (g = 0; g < 20; g++) {
            uint16_t w0 = src[0], w1 = src[1], w2 = src[2], w3 = src[3];
            uint16_t o0, o1, o2, o3;
            src += 4;
            o0 = t0[w0 >> 12] | t1[w1 >> 12] | t2[w2 >> 12] | t3[w3 >> 12];
            o1 = t0[(w0 >> 8) & 15] | t1[(w1 >> 8) & 15]
               | t2[(w2 >> 8) & 15] | t3[(w3 >> 8) & 15];
            o2 = t0[(w0 >> 4) & 15] | t1[(w1 >> 4) & 15]
               | t2[(w2 >> 4) & 15] | t3[(w3 >> 4) & 15];
            o3 = t0[w0 & 15] | t1[w1 & 15] | t2[w2 & 15] | t3[w3 & 15];
            {
                uint8_t *t = row + ((uint16_t)g << 6);
                *(uint32_t *)t        = ((uint32_t)o0 << 16) | o1;
                *(uint32_t *)(t + 32) = ((uint32_t)o2 << 16) | o3;
            }
        }
        if ((y & 7) == 7) rowbase += 40 * 32;
    }
}

/* 16px-wide vertical bars, one color index per 16px group. */
static void render_bars(void)
{
    uint16_t *p = (uint16_t *)PLANAR;
    uint16_t y, g, pl;
    for (y = 0; y < 200; y++)
        for (g = 0; g < 20; g++) {
            uint16_t c = g & 15;
            for (pl = 0; pl < 4; pl++)
                *p++ = (c & (1u << pl)) ? 0xFFFF : 0x0000;
        }
}

/* Invert one 16x16 cell (all four planes) — the pad-driven cursor box. */
static void xor_box(uint16_t bx, uint16_t by)
{
    uint16_t y0 = (uint16_t)(by << 4), line;
    for (line = 0; line < 16; line++) {
        uint16_t y = (uint16_t)(y0 + line);
        uint16_t idx = (uint16_t)((y << 6) + (y << 4)   /* y * 80 words */
                                  + (bx << 2));
        uint16_t *w = (uint16_t *)PLANAR + idx;
        w[0] ^= 0xFFFF; w[1] ^= 0xFFFF; w[2] ^= 0xFFFF; w[3] ^= 0xFFFF;
    }
}

int main(void)
{
    uint16_t bx = 9, by = 6, prevpad = 0, boxdrawn = 0;

    tab_init();
    render_bars();
    convert_full();

    VU16(STAT1) = 0;
    VU16(STAT0) = E1_MAGIC;

    for (;;) {
        uint16_t cmd = VU16(CMD0);

        if (cmd == E1_CMD_BENCH) {
            uint16_t i;
            VU16(STAT1) = 0;
            for (i = 1; i <= 100; i++) {
                convert_full();
                VU16(STAT1) = i;
            }
            VU16(STAT0) = E1_ST_BENCH_DONE;
            while (VU16(CMD0) == E1_CMD_BENCH) ;
            VU16(STAT0) = E1_MAGIC;
        }
        else if (cmd == E1_CMD_STREAM) {
            uint16_t pad = VU16(CMD1);
            uint16_t pressed = pad & (uint16_t)~prevpad;
            uint16_t obx = bx, oby = by;
            prevpad = pad;

            if (!boxdrawn) { xor_box(bx, by); boxdrawn = 1; }
            if ((pressed & 0x04) && bx > 0)  bx--;   /* L */
            if ((pressed & 0x08) && bx < 19) bx++;   /* R */
            if ((pressed & 0x01) && by > 0)  by--;   /* U */
            if ((pressed & 0x02) && by < 11) by++;   /* D */
            if (bx != obx || by != oby) {
                xor_box(obx, oby);               /* remove from old cell */
                xor_box(bx, by);                 /* draw at new cell */
            }

            convert_full();
            VU16(STAT1)++;
        }
    }
}
