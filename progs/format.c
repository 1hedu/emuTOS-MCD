/* FORMAT.PRG — lay a fresh filesystem on a chosen drive.
 *
 * Built twice, once per drive: FORMATB.PRG and FORMATC.PRG. It was
 * wired to B: with the letter in six separate strings, from when B: was
 * the only writable drive there was -- so somebody trying to repair the
 * cartridge ran it, watched it report success, and reformatted the
 * console's internal memory instead.
 *
 * A prompt would have fixed that too, and was the first attempt. Two
 * programs is better on a machine whose only input is a d-pad: the
 * choice is made by which icon you double-click, there is no keypress
 * to get wrong, and the icon says which drive it will erase before you
 * touch it.
 *
 * B: is the console's internal backup RAM, and it is small enough that
 * a wedged directory or a half-written file matters. Holding C at
 * power-on reformats it, but that is a poor way to offer a destructive
 * operation: you cannot see what you are about to erase, and you have
 * to reboot to reach it. This is the same job as a program you run from
 * the desktop, which is how an ST did it.
 *
 * Nothing here is Mega CD specific. It asks the drive how big it is by
 * probing for its last readable sector, then writes a FAT12 volume to
 * fit — so it works on the 8 K internal BRAM and on a backup RAM cart
 * without being told which is fitted.
 */

typedef unsigned char UBYTE;
typedef unsigned long ULONG;

/* Handed to the BIOS, so it must be even -- see progs/cdtest.c. */
#define OSBUF __attribute__((aligned(4)))

void con_ws(const char *s);
long con_in(void);
long dos_cconis(void);
long dos_dfree(void *buf, long drv);
long bios_rwabs(long rw, void *buf, long count, long recno, long dev);
long bios_getbpb(long dev);

#define RW_READ     0
#define RW_WRITE    1

/* Physical mode: no BPB translation, no media-change interlock, straight
 * to the driver. A format utility cannot use logical mode, because
 * logical mode needs the volume to be loggable -- and the only volume
 * anyone ever formats is one that is not. On a corrupt disk Getbpb
 * fails, the media-change flag never clears, and every logical access
 * answers E_CHNG for ever: the tool refuses to run on exactly the disk
 * it exists to repair. */
#define RW_PHYS     (8 | 2)     /* RW_NOTRANSLATE | RW_NOMEDIACH */

/* FORMAT_DRIVE comes from the build: 1 = B:, 2 = C:. No default, so a
 * build that forgets it fails here rather than quietly erasing B:. */
#ifndef FORMAT_DRIVE
# error "FORMAT_DRIVE must be 1 (B:) or 2 (C:)"
#endif
#if FORMAT_DRIVE == 18
# define DRVLETTER 'S'
# define DRVNAME   "S: cartridge save RAM"
#else
# define DRVLETTER 'I'
# define DRVNAME   "I: internal backup RAM"
#endif
static const long drive = FORMAT_DRIVE;     /* the logical drive */
static long unit = FORMAT_DRIVE;            /* ...and its physical unit */
static long start;                          /* ...and its offset on that unit */

/* Rwabs in physical mode takes a UNIT, not a drive letter, and here
 * they are not related at all.
 *
 * PUN_INFO is the standard answer -- _pun_ptr at 0x516 points at the
 * physical unit and partition offset behind every logical drive -- and
 * it cannot answer for this machine. PUN_MAXUNITS is 16, "cannot store
 * info for devices > P:" as EmuTOS's own blkdev.c:151 puts it, and S:
 * is drive 18. Indexing pun[2+18] does not fail, it reads into
 * partition_start[] and returns a plausible number, which is the worst
 * kind of wrong for a program whose job is to overwrite sectors.
 *
 * So this machine's own layout is used instead, which is knowable
 * exactly: all four drives are devices 0 to 3 of one bus, unit =
 * NUMFLOPPIES + DEVICES_PER_BUS * SEGACD_BUS + device = 2 + 32 + device,
 * and none of them is a partition inside anything, so the offset is
 * zero. PUN_INFO is still consulted first for any drive it can
 * describe, so this stays correct if the machine ever grows a device
 * that is partitioned.
 */
static void resolve_unit(void)
{
    unsigned char * volatile * p_pun = (unsigned char * volatile *)0x516L;
    unsigned char *pun;

    switch (drive) {
    case 2:  unit = 34; return;         /* C: the ramdisk   */
    case 3:  unit = 35; return;         /* D: the disc      */
    case 8:  unit = 36; return;         /* I: internal 8K   */
    case 18: unit = 37; return;         /* S: cartridge RAM */
    }

    if (drive >= 16)                    /* PUN_MAXUNITS */
        return;
    pun = *p_pun;
    if (!pun)
        return;
    if (pun[2 + drive] == 0xFF)         /* not a drive AHDI knows about */
        return;
    unit  = (long)pun[2 + drive] + 2;   /* pun[] counts from the floppies */
    start = *(unsigned long *)(pun + 18 + 4 * drive);
}
#define SECSIZE     512
#define ROOTENTS    32
#define ROOTSECS    (ROOTENTS * 32 / SECSIZE)

static UBYTE buf[SECSIZE] OSBUF;

static void clearbuf(void)
{
    int i;
    for (i = 0; i < SECSIZE; i++)
        buf[i] = 0;
}

/* Decimal by repeated subtraction rather than division. A 32-bit
 * divide would pull in libgcc, and libgcc is not built -mpcrel: its
 * __umodsi3 reaches __udivsi3 with an absolute jsr, which lands in low
 * memory once the program is loaded anywhere but its link address.
 * Nothing here needs more than four digits anyway. */
static void putnum(unsigned short v)
{
    static const unsigned short pow10[5] = { 10000, 1000, 100, 10, 1 };
    char t[6];
    int i, j = 0, started = 0;

    for (i = 0; i < 5; i++) {
        int d = 0;
        while (v >= pow10[i]) {
            v -= pow10[i];
            d++;
        }
        if (d || started || i == 4) {
            t[j++] = (char)('0' + d);
            started = 1;
        }
    }
    t[j] = '\0';
    con_ws(t);
}

static long rw(long op, long count, long recno)
{
    return bios_rwabs(op | RW_PHYS, buf, count, start + recno, unit);
}

/* The same access in logical mode: drive letter, no offset, and every
 * interlock in the block layer switched on. Formatting cannot use this
 * -- see RW_PHYS above -- but the media-change machinery lives up here
 * and nothing physical can reach it, so the last write of the format is
 * made this way on purpose. */
static long rw_log(long op, long count, long recno)
{
    return bios_rwabs(op, buf, count, recno, drive);
}

/* The highest sector that still reads, plus one.
 *
 * This used to try powers of two on the grounds that every geometry the
 * machine could present was one -- 16 sectors of internal BRAM, 16 << id
 * for a backup RAM cart. C: broke that: it is 63 sectors, because the
 * Mode 1 loader keeps the last one of the 64 for its boot report. The
 * old probe would have called it 32 and thrown away half the cartridge.
 *
 * A binary search costs sixteen reads and assumes nothing. */
static long capacity(void)
{
    long lo, hi;

    if (rw(RW_READ, 1, 0) != 0)
        return 0;                       /* not even sector zero */
    for (lo = 0, hi = 65536L; hi - lo > 1; ) {
        long mid = lo + ((hi - lo) >> 1);
        if (rw(RW_READ, 1, mid) == 0)
            lo = mid;                   /* readable */
        else
            hi = mid;                   /* not */
    }
    return lo + 1;
}

static long put(long sector)
{
    return rw(RW_WRITE, 1, sector);
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
    long total, fsiz, clusters, s, err;

    con_ws("\r\nFormat " DRVNAME "\r\n\r\n");

    resolve_unit();
    total = capacity();
    if (total == 0) {
        con_ws("That drive did not answer a single sector.\r\n");
        waitkey();
        return 0;
    }

    con_ws("This erases everything on it (");
    putnum((unsigned short)(total / 2));
    con_ws("K).\r\nY formats it, anything else quits: ");

#ifdef FORMAT_NOASK
    s = 'Y';                    /* test builds only: answer it and go */
#else
    s = waitkey();
#endif
    if (s != 'y' && s != 'Y') {
        con_ws("\r\n\r\nCancelled.\r\n");
        return 0;
    }
    con_ws("\r\n\r\n");

    /* One FAT sector per 341 clusters; grow it until the table can
     * actually describe the data area it leaves behind. */
    for (fsiz = 1; ; fsiz++) {
        clusters = total - 1 - 2 * fsiz - ROOTSECS;
        if (clusters < 1) {
            con_ws("Drive is too small to format.\r\n");
            waitkey();
            return 0;
        }
        if ((clusters + 2) * 3 / 2 <= fsiz * SECSIZE)
            break;
    }

    /* Wipe both FATs and the root directory first, and only then write
     * the boot sector: an interrupted format leaves a volume that still
     * looks unformatted rather than one that looks fine and is not. */
    clearbuf();
    for (s = 1; s < 1 + 2 * fsiz + ROOTSECS; s++) {
        err = put(s);
        if (err) {
            con_ws("Write failed at sector ");
            putnum((unsigned short)s);
            con_ws(".\r\n");
            waitkey();
            return 0;
        }
    }

    clearbuf();                         /* media descriptor in both FATs */
    buf[0] = 0xF8; buf[1] = 0xFF; buf[2] = 0xFF;
    put(1);
    put(1 + fsiz);

    clearbuf();
    buf[0] = 0x60; buf[1] = 0x38;       /* bra.s, as an Atari boot sector */
    buf[2] = 'E'; buf[3] = 'm'; buf[4] = 'u';
    buf[5] = 'T'; buf[6] = 'O'; buf[7] = 'S';
    buf[8] = 0x24; buf[9] = 0x08; buf[10] = 0x26;       /* serial */
    buf[11] = 0x00; buf[12] = 0x02;                     /* 512 per sector */
    buf[13] = 1;                                        /* per cluster */
    buf[14] = 1; buf[15] = 0;                           /* reserved */
    buf[16] = 2;                                        /* FATs */
    buf[17] = ROOTENTS; buf[18] = 0;
    buf[19] = (UBYTE)total; buf[20] = (UBYTE)(total >> 8);
    buf[21] = 0xF8;                                     /* media */
    buf[22] = (UBYTE)fsiz; buf[23] = 0;
    buf[24] = 16; buf[25] = 0;                          /* per track */
    buf[26] = 1; buf[27] = 0;                           /* sides */
    /* The signature the block layer looks for. atari_partition() in
     * disk.c only reaches check_for_no_partitions() when the root
     * sector ends 55 AA, and without it a volume depends entirely on
     * the force-add for removable units to get a drive letter at all.
     * The CD branch has written these two bytes since I: was found not
     * to mount without them; this branch never did. */
    buf[510] = 0x55; buf[511] = 0xAA;
    put(0);

    /* Confirm by reading the sector back rather than trusting a return
     * code, then tell GEMDOS to forget whatever BPB it had. */
    clearbuf();
    if (rw(RW_READ, 1, 0) != 0 || buf[2] != 'E' || buf[3] != 'm'
     || buf[19] != (UBYTE)total) {
        con_ws("Boot sector did not stick.\r\n");
        waitkey();
        return 0;
    }

    /* Now make GEMDOS forget the volume it thought was here.
     *
     * This line used to be Rwabs(RW_READ, NULL, 2, 0, drive), on the
     * strength of the undocumented TOS feature where a NULL buffer means
     * "set the media-change status to 'count'". The feature is real, and
     * EmuTOS implements it -- for floppies only:
     *
     *   blkdev.c:436   if ((dev < NUMFLOPPIES) && (buf == NULL)) {
     *                      blkdev[dev].mediachange = cnt;
     *
     * B: is a floppy unit, so it took that branch and returned. C: is
     * not, so the same call fell through into an ordinary logical read
     * of two sectors into address zero -- and on this machine address
     * zero is PRG-RAM holding the exception vector table. FORMATC wrote
     * a correct filesystem and then shot the sub CPU through the head,
     * which is the hourglass, the striped screen and the dead machine.
     *
     * Getbpb is the honest replacement. It rereads the boot sector we
     * just wrote, rebuilds the BPB from it, and clears both change flags
     * (blkdev.c:612). The logical write that follows then sets
     * 'forcechange' again -- blkdev.c:458 exists precisely so that
     * "altering logical sector zero causes a media change to be
     * detected" -- so the next thing to touch the drive gets E_CHNG once
     * and GEMDOS drops every buffer it was holding. The bytes written
     * are the boot sector that is already there. */
    bios_getbpb(drive);
    rw_log(RW_WRITE, 1, 0);

    /* And now ask GEMDOS, rather than announce the number this program
     * worked out for itself. Dfree goes the whole way -- log the drive,
     * read the BPB, walk the FAT -- so a figure that comes back right is
     * the media change, the BPB and the filesystem all confirmed at
     * once, by the layer that has to believe them. The computed
     * 'clusters' never proved anything: it was arithmetic on numbers
     * this program had just made up. */
    {
        long *d = (long *)buf;
        d[0] = d[1] = 0;
        err = dos_dfree(buf, drive + 1);    /* Dfree counts A: as 1 */
        if (err < 0) {
            con_ws("Formatted, but GEMDOS will not read it (");
            putnum((unsigned short)-err);
            con_ws(").\r\n");
        } else {
            con_ws("Done. ");
            putnum((unsigned short)d[0]);
            con_ws(" of ");
            putnum((unsigned short)d[1]);
            con_ws(" clusters free.\r\n");
        }
    }
#ifdef FORMAT_AUTO
    /* Emulator only: from AUTO, and hold, so a headless run can format a
     * drive and leave the result on the screen. */
    for (;;)
        ;
#else
    con_ws("\r\nPress any key.\r\n");
    waitkey();
#endif
    return 0;
}
