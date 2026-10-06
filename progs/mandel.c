/* MANDEL.PRG -- the Mandelbrot set, computed on the machine.
 *
 * DEMO.PI1 is the same picture, drawn on the PC at build time by
 * tools/mkpi1.py so that SHOW.PRG has something to show. This draws it
 * here instead, on the Mega CD's 68000, straight onto the screen so
 * that it can be watched coming in, and then saves it as C:\MANDEL.PI1
 * for SHOW to open. Same view, same sixteen colours, same rule for the
 * pens, so the two can be put side by side.
 *
 * There is no floating point on a 68000, and these programs link
 * without libgcc, so there is no division either. The arithmetic is
 * fixed point, 4.12 in sixteen bits, which is what MULS takes: four
 * bits of integer is room for anything an iteration produces before the
 * escape test catches it, and the squares are compared in the 32 bits
 * MULS returns, before they are shifted back. The coordinates step
 * across the picture by addition, in 16.16 so that 320 steps do not
 * drift.
 */
typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;
typedef short WORD;
typedef long LONG;

#include "scdapi.h"

void con_ws(const char *s);
long dos_cconis(void);
long con_in(void);
long dos_fcreate(const char *name, long attr);
long dos_fwrite(long handle, long count, const void *buf);
long dos_fclose(long handle);
void *xbios_physbase(void);
long xbios_setcolor(long pen, long colour);

#define W 320
#define H 200
#define LIMIT 15

/* tools/mkpi1.py's dusk ramp, interior black. */
static const UWORD palette[16] = {
    0x000, 0x001, 0x102, 0x203, 0x304, 0x405, 0x516, 0x627,
    0x738, 0x748, 0x759, 0x76a, 0x77b, 0x77c, 0x77e, 0x777,
};

/* Iterations before |z| leaves the circle of radius 2, at most LIMIT.
 * cx and cy are 4.12, passed as ints.
 *
 * In assembly because it is the whole of the running time, and gcc
 * kept the loop counter and both coordinates on the stack. Everything
 * here is in registers: zx d0, zy d1, the squares d2 and d6, the count
 * d3 (counting down, so the answer is LIMIT-1 minus what is left), cx
 * d4, cy d5. Each MULS takes two 4.12 words to an 8.24 long; |z|^2 is
 * tested at that width, before anything is shifted back. */
int escape(int cx, int cy);
__asm__(
    "escape:\n\t"
    "movem.l %d2-%d7,-(%sp)\n\t"
    "move.w  24+6(%sp),%d4\n\t"       /* cx */
    "move.w  24+10(%sp),%d5\n\t"      /* cy */
    "moveq   #0,%d0\n\t"
    "moveq   #0,%d1\n\t"
    "moveq   #14,%d3\n"                /* LIMIT - 1 */
    "1:\n\t"
    "move.w  %d0,%d2\n\t"
    "muls.w  %d0,%d2\n\t"             /* zx^2 */
    "move.w  %d1,%d6\n\t"
    "muls.w  %d1,%d6\n\t"             /* zy^2 */
    "move.l  %d2,%d7\n\t"
    "add.l   %d6,%d7\n\t"
    "cmp.l   #0x04000000,%d7\n\t"     /* 4.0 in 8.24 */
    "bgt.s   2f\n\t"
    "muls.w  %d0,%d1\n\t"             /* zx * zy */
    "asr.l   #8,%d1\n\t"
    "asr.l   #3,%d1\n\t"              /* 2xy, back to 4.12 */
    "add.w   %d5,%d1\n\t"
    "sub.l   %d6,%d2\n\t"
    "asr.l   #8,%d2\n\t"
    "asr.l   #4,%d2\n\t"
    "move.w  %d2,%d0\n\t"
    "add.w   %d4,%d0\n\t"             /* zx^2 - zy^2 + cx */
    "dbra    %d3,1b\n\t"
    "moveq   #15,%d0\n\t"             /* LIMIT: never left */
    "bra.s   3f\n"
    "2:\n\t"
    "moveq   #14,%d0\n\t"
    "sub.w   %d3,%d0\n\t"
    "ext.l   %d0\n"
    "3:\n\t"
    "movem.l (%sp)+,%d2-%d7\n\t"
    "rts");

/* The header and the picture, one buffer, for one Fwrite. */
static UWORD header[17] __attribute__((aligned(4)));

static void save(const UWORD *screen)
{
    long fh = dos_fcreate("C:\\MANDEL.PI1", 0);
    int i;

    if (fh < 0)
        return;
    header[0] = 0;                          /* ST low resolution */
    for (i = 0; i < 16; i++) header[1 + i] = palette[i];
    dos_fwrite(fh, sizeof(header), header);
    dos_fwrite(fh, 32000L, screen);
    dos_fclose(fh);
}

/* A key, or a pad button pressed and let go. */
static void wait_any(void)
{
    while (SCD_PADWORD & (SCD_PAD_A | SCD_PAD_B | SCD_PAD_C | SCD_PAD_START))
        ;
    for (;;) {
        if (dos_cconis()) { con_in(); return; }
        if (SCD_PADWORD & (SCD_PAD_A | SCD_PAD_B | SCD_PAD_C | SCD_PAD_START)) {
            while (SCD_PADWORD & (SCD_PAD_A | SCD_PAD_B | SCD_PAD_C
                                  | SCD_PAD_START))
                ;
            return;
        }
    }
}

int pmain(void)
{
    UWORD *screen = (UWORD *)xbios_physbase();
    UWORD saved[16];
    LONG cy16 = -75366L;                    /* -1.15 in 16.16 */
    const LONG dy16 = 754;                  /* 2.30 / 200 */
    const LONG dx16 = 594;                  /* 2.90 / 320 */
    UWORD *row = screen;                    /* 80 words a line, in order */
    int x, y, i;

    for (i = 0; i < 16; i++)
        saved[i] = (UWORD)xbios_setcolor(i, -1);
    con_ws("\033E\033f");                   /* clear, no text cursor */
    __asm__ volatile(".word 0xA00A" ::: "d0", "d1", "d2",
                     "a0", "a1", "a2", "cc", "memory");    /* hide mouse */
    for (i = 0; i < 16; i++)
        xbios_setcolor(i, palette[i]);

    for (y = 0; y < H; y++) {
        WORD cy = (WORD)(cy16 >> 4);
        LONG cx16 = -134349L;               /* -2.05 in 16.16 */

        for (x = 0; x < W; x += 16) {
            UWORD p0 = 0, p1 = 0, p2 = 0, p3 = 0;
            for (i = 0; i < 16; i++) {
                int n = escape((WORD)(cx16 >> 4), cy);
                int pen = (n >= LIMIT) ? 0 : n + 1;
                UWORD bit = (UWORD)(0x8000u >> i);
                if (pen & 1) p0 |= bit;
                if (pen & 2) p1 |= bit;
                if (pen & 4) p2 |= bit;
                if (pen & 8) p3 |= bit;
                cx16 += dx16;
            }
            row[0] = p0; row[1] = p1; row[2] = p2; row[3] = p3;
            row += 4;
        }
        cy16 += dy16;
    }

    save(screen);
    wait_any();

    for (i = 0; i < 16; i++)
        xbios_setcolor(i, saved[i]);
    __asm__ volatile(".word 0xA009" ::: "d0", "d1", "d2",
                     "a0", "a1", "a2", "cc", "memory");    /* show mouse */
    con_ws("\033E\033e");
    return 0;
}
