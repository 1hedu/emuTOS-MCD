/* SONIC.ACC -- Sonic, off the Desk menu, onto the desktop that is there.
 *
 * The point of him is that he runs on your desktop. Not on a copy of it,
 * not on a screen a program cleared first: on the icons and the menu bar
 * and whatever windows are open, with every horizontal edge of them a
 * surface he can stand on.
 *
 * NATIVE.PRG cannot do that and never could. A .PRG launched from the
 * desktop is a program: GEM hands it the screen, and the first thing it
 * has to do is say what it is for -- so by the time Sonic starts, the
 * desktop he was supposed to be running on has been replaced by a list
 * of files and a "press a key". He was walking on a blank console.
 *
 * A desk accessory is the shape that was wanted. It loads at boot, adds
 * one line to the Desk menu, and when that line is picked the AES sends
 * it a message -- and does not touch the screen. The desktop is still
 * drawn, the accessory hands the payload over, and Sonic falls into the
 * picture that was already there. That is what he is on GEOS too.
 *
 * So this program prints nothing, asks nothing, and offers no choice.
 * Pick it and he runs; press Start and he stops.
 */

typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;

#define PAYLOAD_QUIET 1                 /* a character of console output
                                         * would be printed into the
                                         * picture he is about to walk on */
#include "payload.h"

long aes_call(void *pb);
long dos_dgetdrv(void);

/* ---- the AES, reduced to the four calls an accessory makes ---------- */
#define APPL_INIT       10
#define EVNT_TIMER      24
#define EVNT_MESAG      23
#define MENU_REGISTER   35
#define FORM_ALERT      52

#define AC_OPEN         40

static short contrl[5], global[16], intin[16], intout[16];
static long  addrin[4], addrout[4];

/* Filled in at run time, not written down here.
 *
 * These programs are built -mpcrel and carry no relocation table, which
 * is what lets them be loaded anywhere without one -- and the price is
 * that a static initialiser holding the address of another static is a
 * link-time address that nothing fixes up. Writing the six pointers out
 * as an initialiser produced an AES parameter block full of addresses
 * from the wrong end of memory, and EmuTOS answered with an alert:
 * "Unsupported AES function #4121", which is what it reads when the
 * opcode it fetched was never an opcode. */
static void *aespb[6];

static void aes_setup(void)
{
    aespb[0] = contrl; aespb[1] = global;
    aespb[2] = intin;  aespb[3] = intout;
    aespb[4] = addrin; aespb[5] = addrout;
}

static short aes(short op, short nin, short nout, short nain)
{
    contrl[0] = op; contrl[1] = nin; contrl[2] = nout;
    contrl[3] = nain; contrl[4] = 0;
    aes_call(aespb);
    return intout[0];
}

static short appl_init(void)          { return aes(APPL_INIT, 0, 1, 0); }
static short menu_register(short id, const char *title)
{
    intin[0] = id; addrin[0] = (long)title;
    return aes(MENU_REGISTER, 1, 1, 1);
}
static void evnt_mesag(short *buf)
{
    addrin[0] = (long)buf;
    aes(EVNT_MESAG, 0, 1, 1);
}
static void evnt_timer(long ms)
{
    intin[0] = (short)(ms & 0xFFFF); intin[1] = (short)(ms >> 16);
    aes(EVNT_TIMER, 2, 1, 0);
}
static void form_alert(short def, const char *s)
{
    intin[0] = def; addrin[0] = (long)s;
    aes(FORM_ALERT, 1, 1, 1);
}

/* ---- where the payload lives ---------------------------------------- */
/* An accessory is loaded from the root of the boot drive, and that is
 * where its files are. The current drive belongs to the desktop by the
 * time a message arrives -- it may have been in D: for ten minutes -- so
 * the drive letter is taken once, at load time, and written into the
 * name. */
static char mdp[] = "C:\\SONIC.MDP";

/* Two lines of GEM alert, for the one case worth interrupting the
 * desktop over: the files are not there, or the driver is not the one
 * this was built against. Anything else the servant refuses quietly and
 * the desktop simply carries on. */
static const char no_sonic[] =
    "[3][Sonic is not here.|SONIC.MDP and SONIC.MDD|belong beside SONIC.ACC.][ OK ]";

int pmain(void)
{
    short msg[8];
    short ap_id, menu_id;

    mdp[0] = (char)('A' + (dos_dgetdrv() & 0xFF));
    aes_setup();

    ap_id = appl_init();
    if (ap_id < 0) for (;;) ;           /* no AES: an accessory has no
                                         * other way to exist, and must
                                         * not return either */
    menu_id = menu_register(ap_id, "  Sonic");

#ifdef ACC_AUTO
    /* Emulator only. A menu is a mouse and a mouse is a hand, so the one
     * link a headless run cannot exercise is the one between the Desk
     * menu and this loop. This waits for the desktop to finish drawing
     * itself and then does exactly what AC_OPEN does -- which also means
     * the picture he lands on is the real one. */
    {   /* twice, with a gap, because once proved nothing: the first run
         * always worked and the second was the one that died. */
        int n;
        for (n = 0; n < 2; n++) {       /* twice: the second run is the one
                                         * a person reported never coming */
            evnt_timer(8000L);
            if (!payload_open() || !payload_start(mdp))
                form_alert(1, no_sonic);
        }
    }
#endif

    for (;;) {
        evnt_mesag(msg);
        /* AC_OPEN and nothing else. The item id is in msg[4] and is
         * compared where there is one to compare against -- but a
         * message addressed to this process can only be about its own
         * menu line, and refusing to act on the one thing the accessory
         * exists for because an id did not match is the wrong way round. */
        if (msg[0] != AC_OPEN)
            continue;
        (void)menu_id;
        if (!payload_open() || !payload_start(mdp))
            form_alert(1, no_sonic);
        /* And nothing else. The servant acknowledges the hand-over
         * before it jumps, so this is back at the menu immediately while
         * Sonic has the machine; the desktop is untouched underneath him
         * and is still there when he leaves. */
    }
}
