/* SWAP.PRG -- change the cartridge with the power on.
 *
 * The Mega Drive decides its own memory map from /CART. A cartridge
 * that grounds it takes $000000-$3FFFFF and the Mega CD takes
 * $400000-$7FFFFF; that is how this machine booted. A backup RAM cart
 * does not ground it, so pulling the boot cartridge flips the two
 * halves round and everything swapped in afterwards is another backup
 * RAM cart. The drive changes contents, not identity -- which is what a
 * floppy does, and why this is a program and not something the boot
 * decides on your behalf.
 *
 * The same shape as EJECT.PRG: tell the machine what is about to
 * happen, let the person do it, then wait for the drive to come back.
 * Nothing watches the cartridge slot unless this program has asked it
 * to.
 *
 * What survives the pull: C: is copied into the sub CPU's RAM at boot,
 * I: is soldered to the Mega CD, D: is the disc. Only S: changes, and
 * it changes size -- so anything with a file open on S: is in the same
 * trouble as pulling a floppy mid-write. Hence the warning below.
 */
typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;
#include "scdapi.h"

void con_ws(const char *s);
long bios_getbpb(long dev);
long xbios_supexec(long func);

#define HZ200    (*(volatile unsigned long *)0x000004BAL)

/* The servant publishes the cartridge geometry here: sector count in
 * the low eleven bits, a generation in the five above it that goes up
 * every time the cartridge changes. Waiting on the generation rather
 * than the count means a swap between two carts of the same size still
 * registers. */
#define CART_CNT   (*(volatile unsigned short *)0x00FF801AL)
#define CART_GEN   (CART_CNT >> 11)
#define CART_SECS  (CART_CNT & 0x07FF)

static struct scd_api *g_api;
static long sup_op;

static long sup_control(void)
{
    g_api->control(sup_op, 0, 0, 0);
    return 0;
}

static void ctl(long op)
{
    sup_op = op;
    xbios_supexec((long)sup_control);
}

static struct scd_api *find_api(void)
{
    unsigned long * volatile *p_cookies =
        (unsigned long * volatile *)0x5A0L;
    unsigned long *jar = *p_cookies;

    if (!jar) return 0;
    for (; jar[0]; jar += 2)
        if (jar[0] == SCD_COOKIE)
            return (struct scd_api *)jar[1];
    return 0;
}

static void put2(unsigned long v)
{
    static const char hex[] = "0123456789ABCDEF";
    char t[3];
    t[0] = hex[(v >> 4) & 0xF]; t[1] = hex[v & 0xF]; t[2] = 0;
    con_ws(t);
}

static void putu(unsigned long v)
{
    static const unsigned long p10[6] = { 100000UL, 10000UL, 1000UL,
                                          100UL, 10UL, 1UL };
    char t[2]; int i, started = 0;
    t[1] = 0;
    for (i = 0; i < 6; i++) {
        int d = 0;
        while (v >= p10[i]) { v -= p10[i]; d++; }
        if (d || started || i == 5) { t[0] = (char)('0' + d); con_ws(t); started = 1; }
    }
}

int pmain(void)
{
    unsigned short gen0;
    unsigned long t0;
    long g;
    int tries;

    g_api = find_api();
    if (!g_api || g_api->version != SCD_VERSION) {
        con_ws("No matching CD driver: this program wants v");
        put2(SCD_VERSION);
        con_ws(", the driver is v");
        put2(g_api ? g_api->version : 0);
        con_ws(g_api ? ".\r\n" : " (no cookie).\r\n");
        return 0;
    }

    con_ws("\r\nChange the cartridge\r\n\r\n");
    con_ws("S: is the cartridge's save RAM. Close any\r\n");
    con_ws("window on S: before going further -- the\r\n");
    con_ws("drive is about to change under it.\r\n\r\n");
    con_ws("C:, D: and I: are not affected.\r\n\r\n");
    con_ws("Fair warning: pulling the boot cartridge\r\n");
    con_ws("can wedge the machine. Not this program's\r\n");
    con_ws("doing -- the slot was never built for it,\r\n");
    con_ws("and no software prevents it. So do this\r\n");
    con_ws("straight after a boot, when a wedge costs\r\n");
    con_ws("a power cycle and nothing else. Once the\r\n");
    con_ws("pull succeeds, changing one backup cart\r\n");
    con_ws("for another never moves the map again.\r\n\r\n");

    gen0 = CART_GEN;
    con_ws("S: now: ");
    putu(CART_SECS);
    con_ws(" sectors.\r\n\r\n");

    /* Everything is said before the servant is armed, because arming it
     * stops the picture and these lines are the last thing on screen
     * until the button.
     *
     * Pulling the cartridge swings /CART and the bottom eight megabytes
     * change owner while the contacts bounce. A bus cycle caught in
     * that window can hang the machine outright, so the servant touches
     * none of it -- not the screen it converts, not S:, not the address
     * it would have watched to notice the pull. That last one is why
     * this asks for a button rather than offering one: with nothing
     * allowed to look at the bus, a person is the only thing left that
     * knows the cartridge is out. */
    con_ws("Pull the cartridge out, then press a\r\n");
    con_ws("button on the pad.\r\n\r\n");
    con_ws("On the first swap since boot, the\r\n");
    con_ws("picture stops until you press it --\r\n");
    con_ws("deliberate: nothing may read the bus\r\n");
    con_ws("while the map moves. Later swaps keep\r\n");
    con_ws("the screen on.\r\n");

    /* Let the servant convert those lines before it is told to stop. */
    t0 = HZ200;
    while (HZ200 - t0 < 60UL) ;

    ctl(SCD_CARTSWAP);

    /* Three minutes. Long, but the screen is off and a person fumbling
     * a cartridge out of a live slot should not be raced. */
    t0 = HZ200;
    while (CART_GEN == gen0 && HZ200 - t0 < 36000UL)
        ;
    if (CART_GEN == gen0) {
        con_ws("\r\nTimed out. Nothing changed.\r\n");
        con_ws("\r\nPress any key.\r\n");
        return 0;
    }
    gen0 = CART_GEN;

    /* The picture is live again from here: the framebuffer has moved to
     * $020000, the Mega CD's own window, which is there whatever is or
     * is not in the cartridge slot. */
    if (CART_SECS) {
        con_ws("\r\nThe cartridge is still in -- nothing was\r\n");
        con_ws("pulled. S: is unchanged. Run this again\r\n");
        con_ws("when you mean it.\r\n");
        con_ws("\r\nPress any key.\r\n");
        return 0;
    }

    con_ws("\r\nOut. Put the backup RAM cart in, then\r\n");
    con_ws("press a button again.\r\n");

    /* Second press. The slot is looked at exactly once, here, with the
     * new cartridge seated and the bus at rest -- the only condition
     * under which the probe's write-read-restore sweep is safe. */
    t0 = HZ200;
    while (CART_GEN == gen0 && HZ200 - t0 < 36000UL)
        ;
    if (CART_GEN == gen0) {
        con_ws("\r\nTimed out. Run this again with the\r\n");
        con_ws("cartridge in -- it picks up from here.\r\n");
        con_ws("\r\nPress any key.\r\n");
        return 0;
    }

    if (!CART_SECS) {
        con_ws("\r\nNothing in the slot answered. Reseat it\r\n");
        con_ws("and run this again -- it picks up from\r\n");
        con_ws("where it left off.\r\n");
        con_ws("\r\nPress any key.\r\n");
        return 0;
    }

    con_ws("New cartridge: ");
    putu(CART_SECS);
    con_ws(" sectors (");
    putu(CART_SECS / 2);
    con_ws("K).\r\nMounting S:");

    /* Getbpb is the mount. Unit 37 is S:, and the drive needs a moment
     * for the servant to have finished probing it. */
    g = 0;
    for (tries = 0; tries < 10 && !g; tries++) {
        unsigned long t = HZ200;
        con_ws(".");
        while (HZ200 - t < 100UL) ;
        g = bios_getbpb(18);
    }
    con_ws("\r\n");
    con_ws(g ? "S: is mounted.\r\n"
             : "S: did not mount -- it may need formatting.\r\n"
               "FORMATS.PRG will do that, and will erase it.\r\n");

    /* Say what this S: is, while the person deciding what to put on it
     * is looking at the screen.
     *
     * Not keyed on bit 7 of $400001. The first cut of this warning was,
     * with $0080B0 as the citation -- and then the hardware read C6 at
     * probe entry and 06 by the header dump, same session. A bit that
     * clears while you watch is a first-read artifact of the hot
     * insert, not the cartridge's answer, and nothing gets keyed on an
     * artifact.
     *
     * Keyed instead on what the probe actually found. It publishes
     * sector zero's first eight bytes at $1F248 in PRG RAM ($7F248 from
     * this side), on both of its exits, and both formatters put
     * "EmuTOS" at bytes 2-7. Present means the stored volume reached
     * the bus. Absent, on a cartridge that answered with a size, means
     * one of two things and this program cannot tell which: a cartridge
     * that was never formatted, or one whose stored volume did not come
     * up with the hot insert -- this hardware has one of those, its
     * power-off remains drift by bits between cycles like any volatile
     * power-up fingerprint, while the same cartridge seated at power-on
     * reads back its volume and keeps it. So say both readings, rather
     * than let the second one be discovered from a folder that mounts
     * empty next week. */
    {
        const volatile UBYTE *s0 = (const volatile UBYTE *)0x0007F248L;

        if (s0[2] != 'E' || s0[3] != 'm' || s0[4] != 'u'
            || s0[5] != 'T' || s0[6] != 'O' || s0[7] != 'S') {
            /* Not "did not come up" -- erased. The slot has no
             * ground-first pins and these carts have no supervisor
             * IC, so mating with a live bus back-powers the SRAM
             * through its protection diodes and blanks the array to
             * its power-up pattern before any code runs. See
             * docs/ramcart-hotswap.md for the whole case. */
            con_ws("\r\nNo filesystem here. A live insert can\r\n");
            con_ws("ERASE a battery cart -- the slot was\r\n");
            con_ws("never built for it, and whatever this\r\n");
            con_ws("cart held may be gone now. Format away\r\n");
            con_ws("and use S: freely, but it forgets at\r\n");
            con_ws("power-off. To keep data on this cart,\r\n");
            con_ws("seat it before power-on instead.\r\n");
        }
    }
    con_ws("\r\nPress any key.\r\n");
    return 0;
}
