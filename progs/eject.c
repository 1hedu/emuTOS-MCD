/* EJECT.PRG -- open the tray, wait for the swap, remount D:.
 *
 * TOS has no CD anywhere in its API, so there is nothing canonical to
 * plug into: eject is a program, the same shape as SRAMTOOL. Doom CD32X
 * Fusion swaps discs from its menu with the same two drive commands,
 * which is the working precedent on this exact console.
 */
typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;
#include "scdapi.h"

void con_ws(const char *s);
long bios_getpb_unused(void);
long bios_getbpb(long dev);
long xbios_supexec(long func);

/* The pad, from scdapi.h rather than a number copied into this file --
 * which is how it came to be reading a register the servant stopped
 * publishing to. Any of the three face buttons, because being stranded
 * with an open tray over which button it is would be a poor joke. */
#define PADGO    (SCD_PAD_A | SCD_PAD_B | SCD_PAD_C | SCD_PAD_START)
#define HZ200    (*(volatile unsigned long *)0x000004BAL)
#define VEC_COOKIES 0x5A0L

static struct scd_api *g_api;
static long sup_op;
static long sup_ret_dummy;
static struct scd_snap snap __attribute__((aligned(4)));

static long sup_snapshot(void)
{
    g_api->snapshot(&snap);
    return 0;
}

/* Two hex digits, without pulling in anything. */
static void put2(unsigned long v)
{
    static const char hex[] = "0123456789ABCDEF";
    char t[3];
    t[0] = hex[(v >> 4) & 0xF];
    t[1] = hex[v & 0xF];
    t[2] = 0;
    con_ws(t);
}

/* The drive's own status nibble, before and after the command.
 *
 * Run from C: this program opens the tray; run from S: or I: it
 * reportedly did not, and returned cleanly either way. Nothing in here
 * knows which drive it was loaded from, so the difference is somewhere
 * it can only be seen -- print the drive state around the command and
 * the answer is on the screen instead of in the next round trip. */
/* Is there a snapshot to take at all?
 *
 * A driver built without CONF_WITH_SCD_DIAG publishes NULL here rather
 * than a stub returning zeros. CDTEST checks it; this program did not,
 * and on any build without SCD_DIAG=1 the call below went through the
 * NULL -- Supexec ran it in supervisor mode, so execution began at
 * address 0 and walked the vector table until an illegal instruction
 * at 0x30. The tray never opened. */
static int have_snapshot(void)
{
    return g_api && g_api->snapshot;
}

static void show_state(const char *when)
{
    if (!have_snapshot()) {
        con_ws(when);
        con_ws(" drive state unavailable (driver built without\r\n");
        con_ws("        diagnostics -- rebuild with SCD_DIAG=1)\r\n");
        return;
    }
    xbios_supexec((long)sup_snapshot);
    con_ws(when);
    con_ws(" drive state ");
    put2(snap.state);
    con_ws("\r\n");
}

static long sup_control(void)
{
    g_api->control(sup_op, 0, 0, 0);
    return 0;
}

static void ctl(long op)
{
    sup_op = op;
    xbios_supexec((long)sup_control);
    (void)sup_ret_dummy;
}

static struct scd_api *find_api(void)
{
    unsigned long * volatile *p_cookies =
        (unsigned long * volatile *)VEC_COOKIES;
    unsigned long *jar = *p_cookies;

    if (!jar) return 0;
    for (; jar[0]; jar += 2)
        if (jar[0] == SCD_COOKIE)
            return (struct scd_api *)jar[1];
    return 0;
}

static void waitticks(unsigned long t)
{
    unsigned long t0 = HZ200;
    while (HZ200 - t0 < t) ;
}

int pmain(void)
{
    long g;
    int tries;

    g_api = find_api();
    if (!g_api || g_api->version != SCD_VERSION) {
        /* Say which is which. A stale copy of this program left on S:
         * or I: from an older disc fails exactly here and returns
         * without doing anything, which looks like the eject quietly
         * not working. */
        con_ws("No matching CD driver: this program wants v");
        put2(SCD_VERSION);
        con_ws(", the driver is v");
        put2(g_api ? g_api->version : 0);
        con_ws(g_api ? ".\r\n" : " (no cookie).\r\n");
        con_ws("That is an old copy of EJECT.PRG. Use the one on C:.\r\n");
        return 0;
    }

    con_ws("Opening the tray...\r\n");
    show_state("before:");
    ctl(SCD_EJECT);
    waitticks(400);             /* 2 s for the mechanism */
    show_state("after: ");

    con_ws("Swap the disc, then press A.\r\n");
    {   /* Bounded. This program opens the tray before it waits, so a
         * wait that cannot end leaves the machine with no disc and no
         * way back -- which is exactly what happened. A minute, then
         * close it anyway. */
        unsigned long t0 = HZ200;
        while (!(SCD_PADWORD & PADGO) && HZ200 - t0 < 12000UL) ;
        while ((SCD_PADWORD & PADGO) && HZ200 - t0 < 12000UL) ;
    }

    con_ws("Closing and remounting");
    ctl(SCD_RELOAD);

    /* Getbpb is the mount. Nonzero is a parsed boot sector; the drive
     * needs several seconds to spin, read the TOC and settle first. */
    g = 0;
    for (tries = 0; tries < 15 && !g; tries++) {
        con_ws(".");
        waitticks(400);
        g = bios_getbpb(3);      /* D:, the disc */
    }
    con_ws("\r\n");
    con_ws(g ? "D: is mounted.\r\n"
             : "D: did not mount -- run EJECT.PRG again to retry.\r\n");
    return 0;
}
