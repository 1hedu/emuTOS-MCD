/* CDTEST.PRG — the disc diagnostic, as a program.
 *
 * This was 222 lines inside biosmain, run by holding Down at power-on.
 * That was the wrong shape for it three times over: it could only be
 * asked for at power-on, it ran instead of the desktop rather than
 * alongside it, and a fault in it took the boot down — which is exactly
 * what happened the first time the drive-wake was added in front of it.
 *
 * Everything it prints is the CD driver's private counters, and that is
 * the only reason it had to live inside the OS. The driver publishes
 * them now: a pointer to a struct scd_api in the cookie jar under
 * 'SgCD', one call that fills a snapshot and one that pulls the levers.
 * So this is an ordinary TOS program.
 *
 * Each round configures itself, prints what it set, and prints the raw
 * button word — no levers held by a person, because a lever whose
 * detection is not displayed produces a result that cannot be told from
 * the lever not working. Eight rounds cover the matrix:
 *
 *   lead  command the read 20 frames early (this driver's habit) or 2
 *         (the CDBIOS's, measured in a trace of the console)
 *   hold  send command 6 after each fill, or nothing
 *   late  hold the decoder's second CTRL pair back until the drive locks
 *   drvi  wake the parked firmware with DRV_INIT, or go straight at it
 *   ts    hand the disc to the firmware at all
 *
 * The log is rewritten whole to C:\CDLOG.TXT after every round, so a
 * power-cut leaves the rounds that finished. C: is the cartridge's save
 * RAM, which the flash cart writes to an SD card by itself.
 */

typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;

#include "scdapi.h"

void con_ws(const char *s);
long con_in(void);
long dos_cconis(void);
long dos_fcreate(const char *name, long attr);
long dos_fwrite(long handle, long count, const void *buf);
long dos_fclose(long handle);
long dos_dfree(void *buf, long drv);
long dos_dsetpath(const char *path);
long dos_fsetdta(void *dta);
long dos_fsfirst(const char *spec, long attr);
long dos_fopen(const char *name, long mode);
long dos_fread(long handle, long count, void *buf);
long bios_setexc(long num, long vec);
#define HZ200 (*(volatile unsigned long *)0x000004BAL)
long bios_getbpb(long dev);
void critic_fail(void);         /* tosbind.S */

/* etv_critic lives at 0x404, which is Setexc vector 0x101. */
#define VEC_CRITIC  0x101L

/* Start, read straight off the pad.
 *
 * IOFW publishes the raw pad word in the top byte of GA_CMD3
 * (0xA12016), which the sub sees at 0xFF8016; the low two bits of that
 * word are the mouse buttons. Bit 7 of the pad is Start, so bit 15
 * here: iofw/input.c assembles the TH-low half into bits 6 and 7, A and
 * Start. It used to be GA_CMD1, until that register turned out to be
 * carrying the boot flags as well and the two were overwriting each
 * other depending on which boot path got there first.
 *
 * A key through GEMDOS would be the obvious way to ask a program to
 * stop, and it is the wrong one here. Reaching Cconin means opening the
 * on-screen keyboard and picking a letter, and the OSK is toggled by
 * Start -- so the button that means "get me out of this" would first
 * have to be spent putting a keyboard on the screen. Reading the pad
 * costs one word and skips all of it. */
#define PADWORD     SCD_PADWORD         /* both from scdapi.h now */
#define PAD_START   SCD_PAD_START

static int start_held(void)
{
    return (PADWORD & PAD_START) != 0;
}

/* The disc's drive letter. C:, then D:, and now B: -- the drive letters
 * were rearranged so that A: and B: are the removable media a TOS user
 * expects to find there. One place to change is the whole point of
 * naming it. CDDRV_NO is the bit in drvbits, not the drive number. */
#define CDDRV     "D"
#define CDDRV_NO  8L

#define ROUNDS    8
#define LOGMAX    26000         /* what C: holds, less a margin */


/* Anything the operating system writes into must be even.
 *
 * Aligning the sections was not enough and the second panic proved it:
 * a one-byte `char logwhere` in bss pushed every array after it onto an
 * odd address, Fsfirst wrote the DTA, and EmuTOS took the address error
 * on the caller's behalf -- A0 in the dump was this program's `dta`, to
 * the byte. A char array has alignment 1 and gcc is right to give it
 * that; a buffer handed across a system call is the one place that is
 * not good enough. */
#define OSBUF __attribute__((aligned(4)))

static char logbuf[LOGMAX] OSBUF;
static unsigned short loglen;
static char full;

static struct scd_snap snap;
static char dta[44] OSBUF;
static char buf[512] OSBUF;

/* ---- output: everything goes to the screen and the log at once ---- */

static void put1(char c)
{
    char t[2];
    t[0] = c; t[1] = 0;
    con_ws(t);
    if (loglen < LOGMAX) logbuf[loglen++] = c;
    else full = 1;
}

static void puts2(const char *s)
{
    while (*s) put1(*s++);
}

/* Decimal by repeated subtraction, hex by nibble: a 32-bit divide pulls
 * in libgcc, and libgcc is not built -mpcrel — its helpers reach each
 * other with absolute jumps, which land in low memory once the program
 * is loaded anywhere but its link address. */
static void putu(unsigned long v)
{
    static const unsigned long p10[10] = {
        1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL,
        10000UL, 1000UL, 100UL, 10UL, 1UL
    };
    int i, started = 0;

    for (i = 0; i < 10; i++) {
        int d = 0;
        while (v >= p10[i]) { v -= p10[i]; d++; }
        if (d || started || i == 9) { put1((char)('0' + d)); started = 1; }
    }
}

static void putd(long v)
{
    if (v < 0) { put1('-'); v = -v; }
    putu((unsigned long)v);
}

static void putx(unsigned long v, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;
    for (i = digits - 1; i >= 0; i--)
        put1(hex[(v >> (i * 4)) & 0xF]);
}

static void nl(void) { put1('\r'); put1('\n'); }

/* ---- the log file ---- */

/* A:, the cartridge -- the one writable store that survives the power
 * going off, which is the whole point of a log read back on a PC. */
static char logpath[] = "S:\\CDLOG.TXT";
static char logwhere;

static long logsave(void)
{
    long h, rc;
    int i;

    /* S: first, because it survives the power going off, then C:. It
     * used to try C: then B:, which was the old lettering -- B: has not
     * existed since the drives were renamed, so the fallback was a
     * drive that could never answer. */
    for (i = 0; i < 2; i++) {
        logpath[0] = i ? 'C' : 'S';
        h = dos_fcreate(logpath, 0);
        if (h < 0) continue;
        rc = dos_fwrite(h, (long)loglen, logbuf);
        dos_fclose(h);
        logwhere = logpath[0];
        return rc;
    }
    logwhere = '?';
    return -1;
}

/* ---- calling the driver ----
 *
 * In supervisor mode, always. The cookie gives a program a pointer it
 * can call directly, and a direct call keeps the caller's mode -- so a
 * .PRG reaches the driver in user mode, and the moment one of these
 * ops touches a privileged instruction the machine takes a privilege
 * violation with the program's own name nowhere near it.
 *
 * Which is exactly what happened the first time the Mode 1 park
 * worked: bios_parked() went true, segacd_bios_probe() stopped
 * short-circuiting on 0xFFFF, and the call reached
 * _segacd_bios_call -- panic, sr=0010, pc ten bytes into it. The park
 * was fine. The path it opened had simply never run from user mode
 * before, because on Mode 1 it had never run at all.
 *
 * Supexec is the answer TOS already has for this. Arguments go through
 * globals because Supexec takes a function of no arguments. */
static struct scd_api *g_api;
static long sup_op, sup_a, sup_b, sup_c;
static unsigned long sup_ret;

long xbios_supexec(long func);

static long sup_control(void)
{
    sup_ret = g_api->control(sup_op, sup_a, sup_b, sup_c);
    return 0;
}

static long sup_snapshot(void)
{
    g_api->snapshot(&snap);
    return 0;
}

static unsigned long ctl(long op, long a, long b, long c)
{
    sup_op = op; sup_a = a; sup_b = b; sup_c = c;
    xbios_supexec((long)sup_control);
    return sup_ret;
}

static void take_snapshot(void)
{
    xbios_supexec((long)sup_snapshot);
}

/* Is there a snapshot to take at all?
 *
 * A driver built without CONF_WITH_SCD_DIAG publishes a NULL here
 * rather than a stub that fills the struct with zeros, because every
 * field in this program would then read as a machine in trouble and
 * none of it would be true. Checked once, said plainly. */
static int have_snapshot(void)
{
    return g_api && g_api->snapshot;
}

/* ---- finding the driver ---- */

static struct scd_api *find_api(void)
{
    /* _p_cookies at 0x5A0. There is no MMU on this machine, so a user
     * mode read of low memory is a read like any other. */
    unsigned long * volatile * p_cookies = (unsigned long * volatile *)0x5A0L;
    unsigned long *jar = *p_cookies;

    if (!jar) return 0;
    while (jar[0]) {
        if (jar[0] == SCD_COOKIE) {
            struct scd_api *a = (struct scd_api *)jar[1];
            /* Refuse a driver built against a different struct rather
             * than read the wrong offsets confidently. */
            if (a && a->version == SCD_VERSION
                  && a->size == sizeof(struct scd_api))
                return a;
            return 0;
        }
        jar += 2;
    }
    return 0;
}


/* Wait for a key, having first thrown away the ones already waiting.
 *
 * This is why both format programs looked like they did nothing: the
 * desktop leaves something in the keyboard buffer when it launches a
 * program, Cconin returned it immediately, it was not Y, and the cancel
 * path shut the window before a word could be read. The prompt was
 * fine; it was being answered before it was asked.
 *
 * Cconis (GEMDOS 0x0B) reports whether a character is waiting, so the
 * queue can be emptied before anything is asked of the user. The
 * on-screen keyboard supplies the answer -- Start toggles it. */
static long waitkey(void)
{
    while (dos_cconis())
        con_in();
    return con_in() & 0xFF;
}

int pmain(void)
{
    /* The firmware arms come first now, and the reason is that the
     * matrix below them has been answering a question that is no
     * longer the question.
     *
     * "1st 0@1 seen 00C1": the first drive state EmuTOS heard was 0, on
     * its very first exchange, and in two rounds the drive visited only
     * states 0, 6 and 7 -- all three of them the BIOS's "no disc
     * information" group. It has never once been in a state a read may
     * start from. Meanwhile the loader's own letter reads R or Z: Sega's
     * firmware read LBA 1600 off this disc seconds earlier, with the
     * drive in state 2.
     *
     * So the drive is lost across the handover, before this driver
     * sends its first command, and no setting of lead, hold or the
     * decoder's control pair can matter while that is true. Six of the
     * eight arms below are tuning a read that is refused upstream of
     * everything they touch.
     *
     * T1 hands the disc to the parked firmware -- the same firmware,
     * the same DRV_INIT, just after the reset instead of before it. If
     * it reads, the drive is recoverable and the fix is to re-init it
     * at startup. If the firmware cannot read it either, the handover
     * broke the drive for everyone and the fix is in the loader. One
     * round either way, and it is the only round that can currently
     * tell us anything.
     *
     * Round 1 has now run: stage 3, state 2. Reaching stage 3 means the
     * firmware's brd_wait_init passed, which requires the drive to have
     * reported 1, 2 or 4 -- so with DRV_INIT on, the drive is live and
     * stays live, and it is only the read that fails. The next two
     * rounds separate the two things that could mean.
     *
     *   2  T1 I0  the firmware without DRV_INIT. If this also reaches
     *             stage 3 at state 2, DRV_INIT is not what revived the
     *             drive and it was never dead -- our CDD conversation
     *             is simply seeing something different from Sega's. If
     *             it fails at stage 2 instead, DRV_INIT is the thing.
     *   3  T0     our own path, run after the firmware has had two goes
     *             at waking the drive. "1st" is frozen at the first
     *             state of the session, but the seen-mask keeps
     *             growing: a 2 or a 4 appearing in it here says our
     *             driver can see a live drive once something else has
     *             woken it, which makes "DRV_INIT at startup" the fix. */
    static const UWORD a_lead[ROUNDS] = { 20, 2, 20, 2, 20, 2, 20, 2 };
    static const UWORD a_hold[ROUNDS] = {  0, 0,  1, 1,  0, 0,  1, 1 };
    static const UWORD a_late[ROUNDS] = {  0, 0,  1, 1,  0, 0,  1, 1 };
    static const UWORD a_drvi[ROUNDS] = {  1, 0,  0, 1,  0, 1,  0, 1 };
    static const UWORD a_ts[ROUNDS]   = {  1, 1,  0, 0,  0, 0,  0, 0 };
    struct scd_api *api;
    unsigned long run;
    long h, rc, oldcritic;
    int i, stop = 0;

    con_ws("\033E");
    api = find_api();
    g_api = api;
    if (!api) {
        con_ws("No 'SgCD' cookie: this EmuTOS has no CD driver, or\r\n"
               "it was built against a different scdapi.h.\r\n\r\n"
               "Press any key.\r\n");
        waitkey();
        return 0;
    }
    if (!have_snapshot()) {
        con_ws("This EmuTOS was built without CONF_WITH_SCD_DIAG, so the\r\n"
               "driver publishes no snapshot and there is nothing for this\r\n"
               "program to read. Rebuild it with:\r\n\r\n"
               "    make segacd SCD_DIAG=1\r\n\r\n"
               "Press any key.\r\n");
        waitkey();
        return 0;
    }

    /* Every failed question about D: reaches etv_critic, and when this
     * is launched from the desktop the AES has hung a modal alert off
     * that vector -- "the disk in drive D may be damaged". Four
     * questions a round, four alerts a round, each dismissed with a
     * d-pad, for a drive whose failing is the thing being measured. The
     * answers are already printed and logged; the alerts add nothing
     * and cost everything. */
    oldcritic = bios_setexc(VEC_CRITIC, (long)critic_fail);

    /* Half the matrix cannot do anything on a Mode 1 boot, and it took
     * a log full of identical rounds to see it. The I and T arms both
     * work by calling the Sega CDBIOS, which reaches Word RAM only
     * because a CD boot parks it there (boot/sp.S). A cartridge boot
     * parks nothing, so bios_parked() is false, and every CDBIOS arm
     * short-circuits to 0xFFFF before it touches the drive -- which is
     * exactly what 'probe FFFF near FFFF far FFFF sw 0' was saying.
     * Say it in words, once, rather than leave it as four hex fields
     * that look like results. */
    /* What is actually at the park slot, rather than the driver's
     * verdict on it.
     *
     * The Mode 1 loader now copies the firmware to sub 0xBA000 and
     * checks the copy from the main side before EmuTOS loads -- and
     * the emulator agrees the write lands, vector 1 reading 0x200.
     * EmuTOS still says there is nothing there. One of those readings
     * is wrong and a boolean cannot say which, so print the two
     * longwords bios_parked() judges: zeros mean it was never written
     * or has been overwritten, anything else means it is there and the
     * test or the address is at fault. No API change -- there is no
     * MMU, so a program can read 0xBA000 as easily as the driver. */
    puts2("park "); putx(*(volatile unsigned long *)0x000BA000L, 8);
    put1(' ');     putx(*(volatile unsigned long *)0x000BA004L, 8);
    nl();
    /* SCD_BIOS_PROBE, not SCD_BIOS_SWAPS.
     *
     * SWAPS returns segacd_bios_swapcnt -- how many exchanges have run
     * -- which is zero at startup no matter what is parked, and
     * scdapi.h says exactly that on the line that defines it. Using it
     * as a presence test meant this printed "No parked CDBIOS" on every
     * boot ever, including the one where the park slot demonstrably
     * held 00005E80 00000200. PROBE is the presence test: 0xFFFF is
     * bios_parked() saying no, anything else is CDBCHK's answer. */
    if (ctl(SCD_BIOS_PROBE, 0, 0, 0) == 0xFFFFUL)
        puts2("No parked CDBIOS: I and T arms inert.\r\n");
    /* drvbits, straight out of Atari low memory at 0x4C2. Says whether
     * D: exists as a drive letter at all, which is the one thing
     * upstream of Getbpb that nothing here was reporting. The CD unit
     * is flagged removable, so disk_init_one() gives it a "BGM"
     * partition even when the boot probe reads nothing -- meaning bit 3
     * set does not mean the disc was read, only that the letter is
     * there and Getbpb's answer is about the sector and not about the
     * drive map. Bit 3 clear would mean the opposite, and would make
     * every other number on this screen meaningless. */
    puts2("drvbits "); putx(*(volatile unsigned long *)0x000004C2L, 8);
    nl();
    {   /* The servant's cartridge probe, verbatim, because "there is no
         * S: drive" is the same symptom whether the cart is absent, its
         * ID reads as open bus, or it answered its ID and then refused
         * to hold a byte because the write enable went somewhere it
         * does not decode. id 0 means it never answered at all; a good
         * id with hold 0 means it answered and would not take a write.
         *
         * Low eleven bits the sector count, 11-13 the raw ID, 14 the
         * write enable read back, 15 the write/read-back test. */
        unsigned short w = *(volatile unsigned short *)0x00FF801AL;
        puts2("cart "); putx(w, 4);
        puts2(" sec "); putu(w & 0x07FF);
        puts2(" id "); putu((w >> 11) & 7);
        puts2(" wp "); putu((w >> 14) & 1);
        puts2(" hold "); putu((w >> 15) & 1);
        nl();
    }
    /* Which build this is. Three carts have been in the air at once
     * and a log cannot say which one produced it; SCD_VERSION is
     * already checked against the driver, so printing it costs nothing
     * and settles the question. */
    puts2("api v"); putu(SCD_VERSION); nl();
    puts2("Start stops it.\r\n\r\n");

    for (run = 0; ; run++) {
        int arm = (int)(run & (ROUNDS - 1));

        /* Unlatch the drive before anything else in the round.
         *
         * segacd_cd_rw() has a dead-drive latch: three consecutive
         * failed blocks and it stops asking the drive and answers
         * EREADF from the block layer. disk_init_all() probes the CD
         * unit at boot, that probe fails on Mode 1 today, and so the
         * latch is already on by the time the desktop appears -- every
         * single boot, before CDTEST has run one round.
         *
         * Which means every round of every run so far measured the
         * latch. Getbpb 0 because the read was refused; ring 0, dsr 0,
         * rd 0, rc 0000 because no request reached the driver, let
         * alone the drive. Eight tuning arms, none of them tried.
         *
         * A diagnostic whose job is to retry the drive under different
         * settings has to clear that first, and has to clear it every
         * round, because a round that fails sets it again for the
         * next. */
        ctl(SCD_REVIVE, 0, 0, 0);

        ctl(SCD_TUNE, a_lead[arm], a_hold[arm], a_late[arm]);
        ctl(SCD_TS_TUNE, a_drvi[arm], 0, 0);
        ctl(SCD_TS_ARM, a_ts[arm], 0, 0);
        ctl(SCD_TS_REARM, 0, 0, 0);

        /* Short, because the line is forty columns and the last field
         * used to fall off the edge of the tube. */
        puts2(CDDRV ": T"); putu(run + 1);
        puts2("  L"); putu(a_lead[arm]);
        puts2(" H");   putu(a_hold[arm]);
        puts2(" A");   putu(a_late[arm]);
        puts2(" I");   putu(a_drvi[arm]);
        puts2(" T");   putu(a_ts[arm]);
        puts2(" P");   putx(PADWORD, 4);      /* what Start looks like */
        nl();

        /* Getbpb first, because it is upstream of everything else here.
         *
         * Dfree -1 is xgetfree returning ERR because ckdrv failed --
         * the volume could not be logged -- and GEMDOS then issues
         * nothing else at all. So on a round where the volume will not
         * log, "ring 0" and "dsr 0 edt 0" and the timeshare's "rd 0
         * bad 0" do not mean the CD failed. They mean nothing was
         * asked. Three sessions of logs have been read as though the
         * read path were being exercised on every round.
         *
         * Getbpb goes straight at the block layer and says whether the
         * boot sector can be read and parsed: nonzero is a BPB, 0 is
         * "no". That is the gate, and it is the one number that was
         * missing. */
        /* Each operation stamped with hz_200 (5 ms units), because
         * "the folder takes tens of seconds" needs an owner: the boot
         * sector, the FAT walk (Dfree), the directory (Fsfirst), or
         * the file itself. One number each and the argument is over. */
        {
            unsigned long t0 = HZ200;
            unsigned long g = (unsigned long)bios_getbpb(CDDRV_NO - 1);
            puts2(" Getbpb   "); putx(g, 8);
            puts2("  t"); putu((HZ200 - t0) * 5); puts2("ms"); nl();
        }

        {
            unsigned long t0 = HZ200;
            rc = dos_dfree(buf, CDDRV_NO);
            puts2(" tDfree "); putu((HZ200 - t0) * 5); puts2("ms"); nl();
        }
        puts2(" Dfree "); putd(rc); put1(' ');
        putu(*(unsigned long *)buf); put1('/');
        putu(*((unsigned long *)buf + 1)); puts2(" free"); nl();

        rc = dos_dsetpath(CDDRV ":\\");
        puts2(" Dsetpath "); putd(rc); nl();

        dos_fsetdta(dta);
        {
            unsigned long t0 = HZ200;
            rc = dos_fsfirst(CDDRV ":\\*.*", 0x17);
            puts2(" tFsfirst "); putu((HZ200 - t0) * 5); puts2("ms"); nl();
        }
        puts2(" Fsfirst  "); putd(rc); puts2("   ["); puts2(dta + 30);
        put1(']'); nl();

        {
            unsigned long t0 = HZ200;
            h = dos_fopen(CDDRV ":\\READCD.TXT", 0);
            puts2(" tFopen "); putu((HZ200 - t0) * 5); puts2("ms"); nl();
        }
        puts2(" Fopen    "); putd(h); nl();
        if (h >= 0) {
            rc = dos_fread(h, 32L, buf);
            dos_fclose(h);
            puts2(" Fread    "); putd(rc); puts2("   [");
            for (i = 0; i < 24; i++)
                put1((buf[i] < 32 || buf[i] > 126) ? '.' : buf[i]);
            put1(']'); nl();
            /* And the same bytes as numbers: a sanitised string turns
             * every wrong answer into the same row of dots, and the
             * wrong answers need telling apart. */
            puts2(" raw ");
            for (i = 0; i < 8; i++) putx((unsigned char)buf[i], 2);
            nl();
        }

        take_snapshot();
        if (snap.size != sizeof(snap) || snap.version != SCD_VERSION) {
            puts2(" snapshot size/version mismatch -- stopping"); nl();
            break;
        }

        /* Was the drive asked? Printed before anything that describes
         * what the drive did, because if dead is 1 the round ended in
         * the block layer and none of the rest of these numbers is
         * about this round at all. */
        puts2(" dead "); putu(snap.dead);
        puts2(" run "); putu(snap.deadrun);
        puts2(" boot "); putu(snap.booting);
        puts2(" storm "); putu(snap.stormed);
        puts2(" tick "); putu(snap.ticks); nl();

        /* ok/16 says whether "drive" is a number or a coin toss: it is
         * a nibble out of the CDD's status packet, and a packet that
         * failed its checksum has nibbles too. */
        puts2(" drive "); putu(snap.state);
        put1(snap.cddok ? '.' : '?');
        put1('/'); putu(snap.cddscore);
        /* Every state seen this session, and the first one: the loader
         * hands over a drive in state 2 that has just read a sector. */
        puts2(" 1st "); putu(snap.cddfirst);
        put1('@'); putu(snap.cddftick);
        puts2(" seen "); putx(snap.cddmask, 4);
        /* toc 0 with sub F is a drive answering "not available" to
         * every question about the disc. */
        puts2(" woke "); putu(snap.woke);
        puts2(" toc "); putu(snap.cddtoc);
        put1('.'); putx(snap.cddsub, 1);
        puts2("  seek "); putu(snap.seeks);
        puts2("  wait "); putu(snap.hits);
        puts2("  err ");  putu(snap.errs);
        puts2("  why ");  putx(snap.why, 4); nl();

        puts2(" torn "); putu(snap.torn);
        puts2(" bcd ");  putu(snap.bad);
        puts2(" wr ");   putu(snap.wrong);
        puts2(" l");     putd(snap.wdelta);
        puts2(" fx ");   putu(snap.fix);
        puts2(" fl ");   putu(snap.flag); nl();

        puts2(" lastfail "); putu(snap.flba);
        puts2(" x"); putu(snap.fsame);
        puts2("  CDBIOS rd "); putu(snap.ts);
        puts2(" bad "); putu(snap.tsf);
        put1(' '); putx(snap.tsrc, 4); nl();

        puts2(" timeout: int "); putu(snap.lints);
        puts2("  d "); putd(snap.lshort); nl();

        /* first says which byte the rejects started with and same how
         * many matched it. same == mode means the decoder is handing
         * back one constant, which is a header read from the wrong
         * place; same well below mode means they are varied, and varied
         * means real non-data sectors we are right to be dropping. */
        puts2(" mode "); putu(snap.badmode);
        puts2(" h3 ");   putx(snap.lasth3, 2);
        puts2(" f");     putx(snap.h3first, 2);
        puts2(" x");     putu(snap.h3same); nl();

        puts2(" ring "); putu(snap.ringok);
        puts2("  new "); putd((long)snap.newest); nl();

        puts2(" dsr "); putu(snap.dsr);
        puts2(" edt "); putu(snap.edt);
        puts2("  nz "); putu(snap.nz); nl();

        puts2(" blk "); putu(snap.blba);
        put1('+'); putu(snap.boff);
        puts2(" s");  putu(snap.bsrc);
        puts2(" nz "); putu(snap.bnz); nl();

        puts2(" slot "); putu(snap.slotbad);
        puts2("  want "); putu(snap.swant);
        puts2(" got ");   putu(snap.sgot); nl();

        puts2(" base "); putu(snap.sbase);
        puts2(" have "); putu(snap.shave);
        puts2(" src ");  putu(snap.ssrc); nl();

        /* STAT0 bit 7 against the mode byte. 11 both say real, 00 both
         * say not, 10 and 01 are where they disagree. Near-zero
         * disagreement means the flag IS the mode test. */
        puts2(" v11 "); putu(snap.v[0]);
        puts2(" v10 "); putu(snap.v[1]);
        puts2(" v01 "); putu(snap.v[2]);
        puts2(" v00 "); putu(snap.v[3]); nl();

        /* 'xfer', not 'hdr'. This is cdc_hdr[] -- segacd.c:1102, "header
         * that arrived with the data" -- and segacd_cdc_read is the only
         * thing that writes it, so it is zero until a transfer actually
         * completes. It was labelled 'hdr' and read for three sessions
         * as "the decoder is not decoding", which it never meant: it
         * says no sector was transferred, and dsr/edt/nz say that
         * already. */
        puts2(" xfer ");
        for (i = 0; i < 4; i++) putx(snap.hdr[i], 2);
        puts2(" st4 ");
        for (i = 0; i < 4; i++) putx(snap.st4[i], 2);
        nl();

        /* The decoder's own header, which is the one that was missing.
         * cdc_head[] -- segacd.c:1090, "header of the one it reported
         * last" -- written on every clean interrupt from HEAD0..HEAD3,
         * double-read for tearing, latched in the same breath as
         * cdc_pt_last. Against 'want': equal means the matching is at
         * fault, different means the drive is elsewhere, zero means
         * nothing is decoding, and a value that never changes while
         * pt_last does means the header registers are being read from
         * the wrong place. */
        /* Which decoder configurations have been tried, and which is
         * live. A ladder that never left rung 0 and a ladder whose
         * sampling aliases look identical in a single reading. */
        puts2(" rung "); putu((snap.rep[22] >> 6) & 3);
        puts2(" tried "); putx((snap.rep[22] >> 1) & 7, 1);
        puts2(" torn "); putu(snap.torn); nl();

        puts2(" head "); putx(snap.rep[2], 4); putx(snap.rep[3], 4);
        puts2(" want ");
        putx(snap.rep[16], 4);                  /* M, S */
        putx(snap.rep[17] >> 8, 2);             /* F */
        puts2(" s"); putu(snap.rep[17] & 0xFF);
        nl();

        /* And the block whole, four lines of eight words, so a field
         * nobody has thought to name yet is still in the log. The
         * status line renders this same block on the main CPU. */
        for (i = 0; i < 32; i += 8) {
            int j;
            put1(' ');          /* 1 + 8*4 + 7 separators = 40 exactly */
            for (j = 0; j < 8; j++) {
                if (j) put1(' ');
                putx(snap.rep[i + j], 4);
            }
            nl();
        }

        if (run == 0) {
            UWORD br = ctl(SCD_BIOS_PROBE, 0, 0, 0);
            puts2(" CDBIOS probe "); putx(br, 4);
            puts2(" near "); putx(ctl(SCD_BIOS_NEARREAD, 0,0,0), 4);
            puts2(" far ");  putx(ctl(SCD_BIOS_READTEST, 0,0,0), 4);
            puts2(" sw ");   putu(ctl(SCD_BIOS_SWAPS, 0,0,0)); nl();
            puts2(" i4 "); putu(snap.own); put1('/'); putu(snap.i4);
            puts2(" v4 "); putx(snap.vec4, 6);
            puts2(" ctl "); putx(snap.ctrl, 4);
            puts2(" ls ");  putx(snap.lost, 2); nl();
            puts2(" st "); putx(snap.st, 8);
            puts2(" cm "); putx(snap.cm, 8);
            puts2(" x ");  putx(snap.ctl2, 4); nl();
        }
        nl();

        /* Then put the round somewhere that survives a power cut. The
         * return code goes on screen only: it changes every round and
         * would double the size of the file. */
        if (!full) {
            long lrc = logsave();
            con_ws(" log ");
            /* screen only, so straight to the console */
            { char t[2]; t[0] = logwhere; t[1] = 0;
              con_ws(t); }
            con_ws(lrc < 0 ? " FAILED\r\n" : "\r\n");
        } else {
            con_ws(" LOG FULL -- safe to power off\r\n");
        }

        /* Long enough to read a round before the next scrolls it away,
         * and long enough that the drive is not being flogged by the
         * diagnostic meant to observe it. Start, sampled through the
         * wait rather than only between rounds, so the button answers
         * while the pause is on rather than a round and a half later. */
        {
            volatile long t;
            long n;
            for (n = 0; n < 256; n++) {
                for (t = 0; t < 3500L; t++)
                    ;
                if (start_held()) { stop = 1; break; }
            }
        }
        if (stop) {
            /* Wait for the button to come up first. Start is also the
             * OSK toggle, so the press that stops the test is still
             * travelling; asking for a key while it is down means the
             * same press answers the prompt below and the window shuts
             * before anything can be read.
             *
             * Bounded, because an unbounded wait on a button is a hang
             * with a good excuse -- and this one is easy to hit: a
             * wedged pad, or a test harness that holds Start to the end
             * of the run, which is how it was first seen. About a
             * second, then carry on regardless. */
            {
                volatile long t;
                long n;
                for (n = 0; n < 180 && start_held(); n++)
                    for (t = 0; t < 3500L; t++)
                        ;
            }
            puts2("\r\nStopped.\r\n");
            break;
        }
    }

    bios_setexc(VEC_CRITIC, oldcritic);
    con_ws("\r\nPress any key.\r\n");
    waitkey();
    return 0;
}
