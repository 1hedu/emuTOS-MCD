/* VECTOR.ACC -- Vectorman, off the Desk menu, onto the desktop that is there.
 *
 * The same shape as SONIC.ACC, for the same reason: a desk accessory is
 * picked from a menu and the AES does not touch the screen, so he lands
 * on the desktop as it is, windows and all. progs/sonicacc.c says why at
 * length; docs/vectorman.md says what he does once he is there.
 *
 * The one difference is where his files are. Sonic's are on the boot
 * drive, beside the accessory. Vectorman's VECTOR.MDD is fifty-odd
 * kilobytes, and on a cartridge that is better kept on R: -- the
 * cartridge's own ROM, which the payload reads where it lies and which
 * costs no RAM at all -- than on C:, where it would be PRG RAM that
 * Sonic's art wants too. So the accessory looks on the boot drive, then
 * R:, then D:, and takes the first that has VECTOR.MDP.
 */

typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;

#define PAYLOAD_QUIET 1                 /* nothing printed into the picture
                                         * he is about to stand on */
#include "payload.h"

long aes_call(void *pb);
long dos_dgetdrv(void);

#define APPL_INIT       10
#define EVNT_TIMER      24
#define EVNT_MESAG      23
#define MENU_REGISTER   35
#define FORM_ALERT      52

#define AC_OPEN         40

static short contrl[5], global[16], intin[16], intout[16];
static long  addrin[4], addrout[4];

/* Filled in at run time: an initialiser holding another static's address
 * is a link-time address nothing relocates. progs/sonicacc.c. */
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
#ifdef ACC_AUTO
static void evnt_timer(long ms)
{
    intin[0] = (short)(ms & 0xFFFF); intin[1] = (short)(ms >> 16);
    aes(EVNT_TIMER, 2, 1, 0);
}
#endif
static void form_alert(short def, const char *s)
{
    intin[0] = def; addrin[0] = (long)s;
    aes(FORM_ALERT, 1, 1, 1);
}

static char mdp[] = "C:\\VECTOR.MDP";
static char boot;

/* The first drive of boot, R:, D: with VECTOR.MDP in its root. Asked at
 * every opening rather than once: a disc can be changed under a
 * running accessory, and R: is only there on a cartridge boot. */
static int find(void)
{
    char order[3];
    int i;
    long fh;

    order[0] = boot; order[1] = 'R'; order[2] = 'D';
    for (i = 0; i < 3; i++) {
        mdp[0] = order[i];
        fh = dos_fopen(mdp, 0);
        if (fh >= 0) {
            dos_fclose(fh);
            return 1;
        }
    }
    return 0;
}

static const char no_vectorman[] =
    "[3][Vectorman is not here.|VECTOR.MDP and VECTOR.MDD|belong on C:, R: or D:.][ OK ]";

static void open_him(void)
{
    if (!find() || !payload_open() || !payload_start(mdp))
        form_alert(1, no_vectorman);
}

int pmain(void)
{
    short msg[8];
    short ap_id;

    boot = (char)('A' + (dos_dgetdrv() & 0xFF));
    aes_setup();

    ap_id = appl_init();
    if (ap_id < 0) for (;;) ;           /* an accessory must not return */
    menu_register(ap_id, "  Vectorman");

#ifdef ACC_AUTO
    /* Emulator only: no hand on the menu, so it opens itself once the
     * desktop has drawn -- and again eight seconds after he is sent
     * home, because the second run is the one Sonic's died on.
     * tools/build-rom.sh keeps this name off a cartridge. */
    {
        int n;
        for (n = 0; n < 2; n++) {
            evnt_timer(8000L);
            open_him();
        }
    }
#endif

    for (;;) {
        evnt_mesag(msg);
        if (msg[0] == AC_OPEN)
            open_him();
    }
}
