/* PALTEST.PRG -- emulator-only proof that a program can set the palette.
 *
 * Never on a cartridge: it runs from the AUTO folder, paints the screen
 * and then loops forever, which on a television is a console that will
 * not boot. tools/build-rom.sh refuses any payload containing it, the
 * same guard SDIAGAUT.PRG has.
 *
 * What it proves. The servant uploads sixteen colours to the VDP once at
 * startup and, until now, nothing could ever change them: this machine
 * has no ST palette hardware for Setcolor to write to. SHOW.PRG depends
 * entirely on the block the servant now watches just past the
 * framebuffer, and a picture viewer whose colours silently do not work
 * is a picture viewer that shows the desktop's grey. So: sixteen bands,
 * one per pen, and a palette nothing else on this machine would ever
 * produce -- a red ramp over a green one. If the frame comes back with
 * those in it, the path works.
 */

typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;

/* v_bas_ad, the screen base, straight out of the ST sysvar. Not
 * Physbase(): this is the exact longword EmuTOS publishes to the
 * servant every VBL (segacd.c writes v_bas_ad >> 8 to 0xFF8020), so
 * following it is following the same pointer the pump follows, with no
 * trap in between and no chance of the two disagreeing. */
#define V_BAS_AD  (*(UBYTE * volatile *)0x44EL)

#define SCREEN_BYTES 32000L

struct palblock {
    UWORD magic0, magic1;
    UWORD gen;
    UWORD colour[16];
};

int pmain(void)
{
    UWORD *scr = (UWORD *)V_BAS_AD;
    struct palblock *pb =
        (struct palblock *)(V_BAS_AD + SCREEN_BYTES);
    int y, g, p, i;

    /* Sixteen horizontal bands, pen n in band n. ST low is four
     * bitplanes interleaved a word at a time: sixteen pixels per group
     * of four words, one plane each, so a solid run of pen v is each
     * plane word set to all-ones or all-zeros by v's bits. */
    /* Counted, not divided: these programs link without libgcc, so a
     * divide is an undefined reference rather than a slow instruction. */
    {
        int band = 0, n = 0;
        for (y = 0; y < 200; y++) {
            UWORD pen = (UWORD)(band & 15);
            for (g = 0; g < 20; g++)
                for (p = 0; p < 4; p++)
                    *scr++ = (UWORD)((pen & (1u << p)) ? 0xFFFFu : 0x0000u);
            if (++n == 12) { n = 0; band++; }
        }
    }

    for (i = 0; i < 16; i++)
        pb->colour[i] = (UWORD)((i < 8) ? (i << 8) : ((i - 8) << 4));
    pb->magic0 = 0x5041;
    pb->magic1 = 0x4C21;
    pb->gen++;

    /* Nothing is printed after the painting. The VT52 console scrolls
     * when it reaches the bottom of the screen, and a scroll repaints
     * all 32000 bytes -- which erased the bands the first time this
     * ran and made a working paint look like a broken one. */
    for (;;) ;                  /* emulator only -- see the header */
}
