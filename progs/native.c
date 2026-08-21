/* NATIVE.PRG -- run Genesis-side code off a disc.
 *
 * EmuTOS is on the Mega CD's sub 68000 and the Genesis' own 68000 is the
 * servant, which is the only one of the two with a path to the VDP, the
 * pads and the sound hardware. So a .PRG can draw into a framebuffer and
 * do nothing else -- no sprites, no scrolling planes, no PSG. Anything
 * that wants the machine has to run over there, and this is what sends
 * it: it finds .MDP files in the folder it is run from, feeds one across
 * five hundred and twelve bytes at a time, and asks for it to be run.
 *
 * The transfer is the sector path's own bounce buffer and its own
 * handshake, used in the other direction. Nothing was invented to move
 * the bytes; the only new things are two request codes.
 *
 * The loading itself is in payload.h, because SONIC.ACC does it too --
 * and that one is the better way to run a payload that wants to be seen
 * against the desktop, because a .PRG launched from the desktop gets the
 * screen and the desktop goes away. This is the general tool: it lists
 * what is there and runs what you pick.
 *
 * docs/payload.md is the contract on the other side. Nothing in this
 * program knows what any payload does.
 */

typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;

#include "payload.h"

long con_in(void);
long dos_fsetdta(void *dta);
long dos_fsfirst(const char *spec, long attr);
long dos_fsnext(void);
long dos_dsetdrv(long drv);

#define HZ200  (*(volatile unsigned long *)0x000004BAL)

struct dta {
    char    reserved[21];
    UBYTE   attr;
    UWORD   time, date;
    ULONG   size;
    char    name[14];
};

static struct dta the_dta;
static char names[16][14];
static int  nfiles;

static void scopy(char *d, const char *s) { while ((*d++ = *s++)) ; }

static void collect(void)
{
    long r;
    nfiles = 0;
    dos_fsetdta(&the_dta);
    r = dos_fsfirst("*.MDP", 0);
    while (r == 0 && nfiles < 16) {
        scopy(names[nfiles], the_dta.name);
        nfiles++;
        r = dos_fsnext();
    }
}

int pmain(void)
{
    if (!payload_open()) {
        con_ws("\r\nNo matching CD driver.\r\n\r\nPress any key.\r\n");
        con_in();
        return 0;
    }

#ifdef NATIVE_AUTO
    /* Emulator only, and never on a cartridge: no menu and no keys.
     * Send the first payload in the folder and hand over, so a frame
     * dump can show that it ran and a later one that the machine came
     * back. This is how the handover is tested without a hand on the
     * pad.
     *
     * The boot drive first, because that is where the stub payload
     * goes; then D:, which has to be asked for -- it refuses every read
     * until the desktop is up (segacd.c's cd_booting) and a program in
     * AUTO runs before the desktop. */
    collect();
    if (!nfiles) {
        ctl(SCD_BOOT_OVER, 0, 0);
        dos_dsetdrv(3);
        collect();
    }
    if (nfiles) payload_start(names[0]);
    for (;;) ;
#endif
    for (;;) {
        int i, c;
        long k;

        collect();
        con_ws("\033E");
        con_ws("NATIVE -- run Genesis-side code\r\n\r\n");
        if (!nfiles) {
            con_ws("No .MDP files in this folder.\r\n\r\n");
            con_ws("They live on the data disc. Boot that,\r\n");
            con_ws("or use EJECT.PRG to put it in, and run\r\n");
            con_ws("this from D:.\r\n\r\nPress any key.\r\n");
            con_in();
            return 0;
        }
        for (i = 0; i < nfiles; i++) {
            char line[24];
            int p = 0, j = 0;
            line[p++] = (char)('A' + i);
            line[p++] = ' ';
            while (names[i][j] && p < 20) line[p++] = names[i][j++];
            line[p++] = '\r'; line[p++] = '\n'; line[p] = 0;
            con_ws(line);
        }
        con_ws("\r\nA letter runs one.  Q quits.\r\n\r\n");
        con_ws("The screen belongs to it until it ends.\r\n");
        con_ws("It comes back on its own.\r\n\r\n");

        k = con_in();
        c = (int)(k & 0xFF);
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c == 'Q') break;
        if (c < 'A' || c >= 'A' + nfiles) continue;

        con_ws("\033E");
        if (!payload_start(names[c - 'A'])) {
            con_ws("\r\nPress any key.\r\n"); con_in(); continue;
        }

        /* Give it the machine. Coming straight back to a menu would
         * fight the payload for the screen it has just been given. */
        {
            unsigned long t0 = HZ200;
            while (HZ200 - t0 < 200UL) ;
        }
        con_ws("\033E");
        con_ws("Ran ");
        con_ws(names[c - 'A']);
        con_ws(".\r\n\r\nPress any key.\r\n");
        con_in();
    }

    con_ws("\033f\033E");
    return 0;
}
