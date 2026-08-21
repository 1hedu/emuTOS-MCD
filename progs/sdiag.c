/* SDIAG.PRG -- why will S: not open?
 *
 * A drive that mounts and then refuses to open is the hardest kind of
 * fault to report, because nothing fails. Getbpb succeeds, the desktop
 * does nothing, and there is no error anywhere to read. Every guess
 * about it so far has been made from the outside.
 *
 * So this walks the chain a window-open actually walks, one rung at a
 * time, and prints the answer at each rung:
 *
 *   1  what the servant says is in the slot (sectors, generation)
 *   2  the raw first sector, physically -- past every cache and every
 *      interlock, straight off the cartridge
 *   3  what Getbpb makes of it, field by field
 *   4  the first root-directory sector, read from where that BPB says
 *      the root directory is -- which is the step nothing checks and
 *      the step a mismatched volume fails
 *   5  Fsfirst, the call the desktop makes, with its error code
 *   6  Dfree, which walks the FAT
 *
 * The rung that lies is the fault. If 2 shows a boot sector and 4 shows
 * nothing that looks like a directory, the volume disagrees with its
 * own header. If 2 is already noise, the sector mapping is wrong and
 * the header being valid was luck. If everything reads and 5 still
 * fails, the fault is above the driver entirely.
 *
 * Built for either drive: -DDIAG_DRIVE=18 is S:, 8 is I:.
 */
typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;

#include "scdapi.h"

void con_ws(const char *s);
long con_in(void);
long dos_cconis(void);
long dos_dfree(void *buf, long drv);
long dos_fsetdta(void *dta);
long dos_fsfirst(const char *spec, long attr);
long bios_getbpb(long dev);
long bios_rwabs(long rw, void *buf, long count, long recno, long dev);

#ifndef DIAG_DRIVE
# define DIAG_DRIVE 18
#endif
#if DIAG_DRIVE == 18
# define DRVLETTER 'S'
# define DRVUNIT   37
# define DRVSPEC   "S:\\*.*"
#else
# define DRVLETTER 'I'
# define DRVUNIT   36
# define DRVSPEC   "I:\\*.*"
#endif

#define RW_READ  0
/* RW_NOTRANSLATE, so no partition offset is applied -- and
 * RW_NOMEDIACH with it. Without the second flag a swap that bumps the
 * generation twice leaves the media-change interlock armed, and the
 * dump this program exists to take comes back -14 (E_CHNG) having read
 * nothing: a diagnostic refusing to diagnose exactly when the machine
 * is most interesting. EmuTOS defines both in bios/disk.h; asking for
 * the raw sector past the interlock is what they are for. */
#define RW_PHYS  (8 | 2)
#define CART_CNT (*(volatile unsigned short *)0x00FF801AL)
/* What the servant learned about the cartridge itself: bit 15 it
 * carries its own driver, bit 14 that driver was called, bit 13 it
 * refused, low bits the size it claimed. */
#define CART_SMART (*(volatile unsigned short *)0x00FF8010L)

/* The BPB as Getbpb returns it -- EmuTOS's own layout, bios/bpb.h. */
struct bpb {
    UWORD recsiz, clsiz, clsizb, rdlen, fsiz, fatrec, datrec, numcl, bflags;
};

static UBYTE buf[512];
static UBYTE dta[44];

static void hex2(unsigned long v)
{
    static const char h[] = "0123456789ABCDEF";
    char t[3];
    t[0] = h[(v >> 4) & 15]; t[1] = h[v & 15]; t[2] = 0;
    con_ws(t);
}

static void hex4(unsigned long v) { hex2(v >> 8); hex2(v); }

static void putu(unsigned long v)
{
    static const unsigned long p10[6] = {100000UL,10000UL,1000UL,100UL,10UL,1UL};
    char t[2]; int i, started = 0;
    t[1] = 0;
    for (i = 0; i < 6; i++) {
        int d = 0;
        while (v >= p10[i]) { v -= p10[i]; d++; }
        if (d || started || i == 5) { t[0] = (char)('0'+d); con_ws(t); started = 1; }
    }
}

/* Signed, because every one of these calls reports failure as a
 * negative TOS error and printing it unsigned turns -33 into 65503. */
static void puti(long v)
{
    if (v < 0) { con_ws("-"); v = -v; }
    putu((unsigned long)v);
}

/* Sixteen bytes, hex and then the printable ones. The eye wants both:
 * the hex says whether it is zeros or noise, the text says whether it
 * is a boot sector or a directory. */
static void dump16(const UBYTE *p)
{
    int i;
    char t[2]; t[1] = 0;
    for (i = 0; i < 16; i++) { hex2(p[i]); con_ws(" "); }
    con_ws(" ");
    for (i = 0; i < 16; i++) {
        t[0] = (p[i] >= 0x20 && p[i] < 0x7F) ? (char)p[i] : '.';
        con_ws(t);
    }
    con_ws("\r\n");
}

static long waitkey(void)
{
    while (dos_cconis()) con_in();
    return con_in() & 0xFF;
}

/* Every exit ends here.
 *
 * An AUTO program that returns hands straight to the desktop, which
 * clears the screen -- and the screen is the entire output. The first
 * version held only on the success path, so the one run that mattered
 * (Getbpb returning NULL, the early exit) wiped its own report before
 * it could be read. Emulator builds hold forever; shipped builds wait
 * for a key. */
static void finish(void)
{
#ifdef DIAG_AUTO
    con_ws("\r\n[AUTO] holding.\r\n");
    for (;;)
        ;
#else
    con_ws("\r\nPress any key.\r\n");
    waitkey();
#endif
}

#ifdef DIAG_AUTO
/* Emulator only. Built into \AUTO so the whole chain can be walked
 * with no pointer and no double-click -- steering a mouse onto an icon
 * by dead reckoning was the weak link in every previous attempt to test
 * this, and it is not a link this needs. Waits for the servant's
 * generation to move, which is the swap completing, then reports.
 * On hardware you run SWAP.PRG and then SDIAG.PRG, and nothing waits. */
#define HZ200 (*(volatile unsigned long *)0x000004BAL)
static void wait_for_swap(void)
{
    unsigned short gen0 = (unsigned short)(CART_CNT >> 11);
    unsigned long t0 = HZ200;

    con_ws("\r\n[AUTO] waiting for a cartridge change...\r\n");
    /* Two conditions, because the swap publishes twice: once when the
     * old cartridge leaves, with a sector count of zero, and again when
     * the new one has been probed. Waking on the first reports an empty
     * slot and calls it a fault -- which it is not, it is the truth at
     * that instant. Wait for a generation that also has a cartridge
     * behind it. */
    while (((unsigned short)(CART_CNT >> 11) == gen0
            || (CART_CNT & 0x07FF) == 0)
           && HZ200 - t0 < 6000UL)
        ;
    con_ws("[AUTO] gen ");
    putu(gen0);
    con_ws(" -> ");
    putu(CART_CNT >> 11);
    con_ws("\r\n");
}
#endif

int pmain(void)
{
    struct bpb *b;
    long err;
    ULONG free4[4];
    UWORD rootsec = 0;

#ifdef DIAG_AUTO
    wait_for_swap();
#endif

    con_ws("\r\nSDIAG -- why will ");
    { char t[2]; t[0] = DRVLETTER; t[1] = 0; con_ws(t); }
    con_ws(": not open?\r\n\r\n");

    /* 1. The servant's view. Sector count in the low eleven bits, a
     *    generation above it that moves on every cartridge change. */
    con_ws("1 servant: ");
    putu(CART_CNT & 0x07FF);
    con_ws(" sectors, gen ");
    putu(CART_CNT >> 11);
    con_ws("\r\n");
    {
        /* Both halves of "is this cartridge smart", separately. The
         * console's presence bit and the twelve-byte signature are
         * different questions and they fail for different reasons. */
        unsigned short sm = CART_SMART;
        con_ws("  $400001=");
        hex2(sm >> 8);
        /* Bit 7 semantics per Sega's own code, and the earlier label
         * had them backwards. $0080B0 (plain-cart detection): btst #7,
         * SET -> error, no cartridge; a plain cart holds it LOW.
         * $0003F6 (smart hook): bit 7 SET plus RAM_CARTRIDG at $400010
         * -> the cartridge carries its own driver. So set-with-
         * signature is a smart cart, clear is a plain cart, and
         * set-without-signature -- C6, what every hot insert has read
         * -- is the console's own way of saying nothing is answering:
         * a controller-based cartridge whose controller never came up. */
        /* The same byte re-read later in the probe, from the header
         * dump. The first read (above) and this one bracket the whole
         * register-forcing and alias-checking stretch, so together
         * they say whether bit 7 is a transient of the hot insert --
         * set at entry, clear once the cartridge settles -- or held
         * for the whole session. The hardware has answered: C6 first,
         * 06 later, same session. So the first read is an artifact of
         * the hot insert and only this later one is the cartridge
         * speaking; nothing may be keyed on the first. */
        con_ws(" later ");
        hex2(((const UBYTE *)0x0007F200L)[0x01]);
        /* The mirrored odd-byte id from the servant's header dump:
         * $400011, which on a plain cartridge repeats the size id
         * across the whole region. 06 there = the plain protocol,
         * id 6, 512K -- whatever the first byte at $400001 reads. */
        con_ws("  id mirror ");
        hex2(((const UBYTE *)0x0007F200L)[0x11]);
        if (sm & 0x01)
            con_ws(" SMART sig present");
        /* Its own line: forty columns, and the previous version ran
         * the rung off the right edge of the television. */
        con_ws("\r\n  ");
        if (((sm >> 5) & 7) == 7)
            con_ws("standard window, no writes");
        else {
            con_ws("ladder rung ");
            putu((sm >> 5) & 7);
        }
        con_ws("  window: ");
        con_ws((sm & 0x10) ? "real memory" : "ONE LATCH");
        /* The one-line verdict the rung already implies. Rung 7 means
         * the stored volume was read straight off the standard window;
         * any other rung means six read-only sweeps of all thirty-two
         * pages, both bus halves, never saw it. sm == 0 means the
         * Mode 2 probe never ran at all (IDIAG, or before any swap),
         * where the line would be noise. */
        if (sm && ((sm >> 5) & 7) != 7)
            con_ws("\r\n  no stored volume seen in the window");
        /* Where the probe landed. A plain backup RAM cartridge keeps its
         * memory at $600001 and nowhere else, so an address in any other
         * page means we are reading registers and calling them a disk. */
        con_ws("\r\n  at $");
        {
            const UBYTE *d = (const UBYTE *)0x0007F240L;
            int i;
            hex2(d[0]); hex2(d[1]); hex2(d[2]); hex2(d[3]);
            con_ws(d[1] == 0x60 ? " OK" : " NOT $60xxxx");
            con_ws("\r\n  $600001 also at: ");
            if (!d[4]) con_ws("nowhere else");
            else {
                if (d[4] & 0x01) con_ws("WordRAM ");
                if (d[4] & 0x02) con_ws("PRG-RAM ");
                if (d[4] & 0x04) con_ws("$000001 ");
                if (d[4] & 0x08) con_ws("$400001 ");
                if (d[4] & 0x10) con_ws("$500001 ");
                if (d[4] & 0x20) con_ws("$700001 ");
                if (d[4] & 0x40) con_ws("WordRAM-hi ");
                if (d[4] & 0x80) con_ws("$620001 ");
            }
            if (d[5]) con_ws("\r\n  SIZE WAS DOUBLE -- top half aliases");
            con_ws("\r\n  ");
            for (i = 0; i < 6; i++) { hex2(d[8+i]); con_ws(" "); }
        }
        /* Whose memory is the window? A plain cartridge is odd bytes
         * on an 8-bit bus and its even addresses hold nothing; every
         * console memory is 16-bit and holds both halves. So "even:
         * HOLDS" means the thing being formatted is console RAM, not
         * the cartridge. The M1 line samples the Mode 1 slot addresses
         * read-only -- if the map only half-moved, the real cartridge
         * still answers there, and OURS means our volume was found on
         * it. Guarded by a magic so an older servant prints nothing. */
        {
            const UBYTE *ev = (const UBYTE *)0x0007F280L;
            int i;

            if (ev[0] == 'E' && ev[1] == 'V') {
                con_ws("\r\n  even bytes $600000: ");
                con_ws(ev[2] ? "HOLD (16-bit!)" : "empty (8-bit)");
                con_ws("\r\n  $680001 ");
                con_ws(ev[3] ? "RAM" : "no");
                con_ws("  $680000 even ");
                con_ws(ev[4] ? "HOLDS" : "empty");
                con_ws("\r\n  M1 $000001=");
                hex2(ev[7]);
                con_ws(" $200001:");
                for (i = 0; i < 4; i++) hex2(ev[8 + i]);
                if (ev[5]) con_ws(" OURS!");
                if (ev[6]) con_ws(" OURS@0!");
                /* The write-protect matrix: does each half of the
                 * window hold sixteen bytes, protected and enabled?
                 * A half reading N then Y sits behind Sega's WP gate
                 * the way a real battery SRAM does; Y/Y is in front
                 * of it. And the raw bytes at the high half's base,
                 * because if the battery memory is parked there after
                 * a hot insert, whatever it still holds is on this
                 * line. */
                con_ws("\r\n  WPoff lo:");
                con_ws(ev[40] ? "Y" : "N");
                con_ws(" hi:");
                con_ws(ev[3] ? "Y" : "N");
                con_ws("  WPon lo:");
                con_ws(ev[41] ? "Y" : "N");
                con_ws(" hi:");
                con_ws(ev[42] ? "Y" : "N");
                con_ws("\r\n  $680001: ");
                for (i = 0; i < 8; i++) hex2(ev[24 + i]);
            }
        }
        con_ws("\r\n\r\n");
    }

    /* 2. Sector zero, physically. RW_PHYS goes past the logical layer
     *    entirely: no partition offset, no media-change interlock, no
     *    cached BPB. If this is wrong, nothing above it can be right. */
    con_ws("2 sector 0, physical (unit ");
    putu(DRVUNIT);
    con_ws("):\r\n");
    err = bios_rwabs(RW_READ | RW_PHYS, buf, 1, 0, DRVUNIT);
    if (err) {
        con_ws("  Rwabs failed: "); puti(err); con_ws("\r\n");
    } else {
        dump16(buf);
        dump16(buf + 16);
        con_ws("  510/511: "); hex2(buf[510]); con_ws(" "); hex2(buf[511]);
        con_ws((buf[510] == 0x55 && buf[511] == 0xAA)
               ? "  (55 AA present)\r\n" : "  (NOT 55 AA)\r\n");
    }
    con_ws("\r\n");

    /* 3. What the block layer makes of it. Getbpb checks the sector
     *    size, the cluster size and the FAT count -- and nothing else,
     *    which is exactly why a drive can mount and still be unusable. */
    con_ws("3 Getbpb(");
    putu(DIAG_DRIVE);
    con_ws("): ");
    b = (struct bpb *)bios_getbpb(DIAG_DRIVE);
    if (!b) {
        con_ws("NULL -- it did not mount at all.\r\n");
        finish();
        return 0;
    }
    con_ws("ok\r\n");
    con_ws("  recsiz "); putu(b->recsiz);
    con_ws("  clsiz ");  putu(b->clsiz);
    con_ws("  clsizb "); putu(b->clsizb); con_ws("\r\n");
    con_ws("  rdlen ");  putu(b->rdlen);
    con_ws("  fsiz ");   putu(b->fsiz);
    con_ws("  fatrec "); putu(b->fatrec); con_ws("\r\n");
    con_ws("  datrec "); putu(b->datrec);
    con_ws("  numcl ");  putu(b->numcl);
    con_ws("  bflags "); hex4(b->bflags); con_ws("\r\n\r\n");

    /* 4. The root directory, from where that BPB says it is.
     *
     * datrec is the first data sector; the root directory is the rdlen
     * sectors immediately before it. This is the read the desktop makes
     * and the one nothing validates: a header that disagrees with its
     * own volume sends it somewhere there is no directory, and every
     * layer reports success while returning nothing. */
    rootsec = (UWORD)(b->datrec - b->rdlen);
    con_ws("4 root dir at sector ");
    putu(rootsec);
    con_ws(" (");
    putu(b->rdlen);
    con_ws(" sectors):\r\n");
    err = bios_rwabs(RW_READ | RW_PHYS, buf, 1, rootsec, DRVUNIT);
    if (err) {
        con_ws("  Rwabs failed: "); puti(err); con_ws("\r\n");
    } else {
        dump16(buf);
        dump16(buf + 32);
        if (buf[0] == 0)
            con_ws("  first entry empty -- volume reads as empty\r\n");
    }
    con_ws("\r\n");

    /* 5. The call the desktop makes. */
    dos_fsetdta(dta);
    err = dos_fsfirst(DRVSPEC, 0x10);
    con_ws("5 Fsfirst(" DRVSPEC "): ");
    puti(err);
    if (err == 0) {
        con_ws("  first = ");
        con_ws((const char *)(dta + 30));
    } else if (err == -33) {
        con_ws("  (EFILNF: no match -- directory read, nothing in it)");
    } else if (err == -34) {
        con_ws("  (EPTHNF: the path itself failed)");
    } else if (err == -46) {
        con_ws("  (EDRIVE: invalid drive)");
    }
    con_ws("\r\n\r\n");

    /* 6. Dfree walks the FAT, so it fails differently from a directory
     *    read -- which separates "the FAT is wrong" from "the root
     *    directory is somewhere else". */
    con_ws("6 Dfree: ");
    err = dos_dfree(free4, DIAG_DRIVE + 1);
    if (err) {
        con_ws("failed "); puti(err); con_ws("\r\n");
    } else {
        putu(free4[0]); con_ws(" free / ");
        putu(free4[1]); con_ws(" total clusters, ");
        putu(free4[2]); con_ws(" bytes/sector, ");
        putu(free4[3]); con_ws(" sectors/cluster\r\n");
    }

    finish();
    return 0;
}
