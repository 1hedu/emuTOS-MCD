/* SPLITAUT.PRG -- emulator-only: the servant shows the physical screen.
 *
 * Never on a cartridge: it runs from AUTO and then holds the machine,
 * like PALTEST.PRG, and tools/build-rom.sh refuses it by name.
 *
 * An ST program may draw off-screen by moving only the logical screen,
 * Setscreen(buf, -1, -1), and expect nobody to see it until it moves
 * the physical one too. This one does both, a few seconds apart:
 *
 *   1. the screen on display gets bands of pens 1 and 2; a buffer gets
 *      pen 15 all over, drawn with the logical screen pointed at it;
 *      pens 1, 2 and 15 are set to red, blue and green through Setcolor.
 *      The frame should show red and blue bands.
 *   2. after 300 VBLs the physical screen moves to the buffer too. The
 *      frame should go solid green: the palette came along with it.
 */
typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;

void *xbios_physbase(void);
long xbios_setscreen(void *log, void *phys, long rez);
long xbios_setcolor(long pen, long colour);
void xbios_vsync(void);

/* Eight-line bands of pens 1 and 2, or pen 15 all over. */
static void fill(UWORD *scr, int striped)
{
    int y, g, p;

    for (y = 0; y < 200; y++) {
        UWORD pen = striped ? (UWORD)(((y >> 3) & 1) ? 2 : 1) : 15;
        for (g = 0; g < 20; g++)
            for (p = 0; p < 4; p++)
                *scr++ = (UWORD)((pen & (1u << p)) ? 0xFFFFu : 0x0000u);
    }
}

/* The second screen. The servant takes a screen address in 256-byte
 * units and reads its blocks from the 768 bytes after the picture, so
 * 32768 bytes from a 256-byte boundary. In BSS rather than Malloc'd:
 * these programs do not Mshrink, so they own all the free memory. */
static UBYTE second[32768L + 256L] __attribute__((aligned(4)));

int pmain(void)
{
    UWORD *shown = (UWORD *)xbios_physbase();
    UWORD *buf = (UWORD *)(((ULONG)second + 255) & ~255UL);
    int i;

    fill(shown, 1);
    xbios_setscreen(buf, (void *)-1L, -1);
    fill(buf, 0);
    xbios_setcolor(1, 0x700);
    xbios_setcolor(2, 0x007);
    xbios_setcolor(15, 0x070);

    for (i = 0; i < 300; i++)
        xbios_vsync();
    xbios_setscreen((void *)-1L, buf, -1);

    for (;;) ;                  /* emulator only -- see the header */
}
