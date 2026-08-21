/* SRAMTOOL.PRG — lift the console's internal backup RAM onto the
 * cartridge, as a file.
 *
 * This is boot/m1tool.S grown up. That was a Mode 1 cartridge ROM whose
 * whole existence was to copy the Mega CD's 8 KB of internal backup RAM
 * into cart save RAM, so a flash cart could carry it out to an SD card:
 * a separate ROM, a separate boot, a bespoke on-cart format, and a
 * host-side script that knew that format by heart.
 *
 * None of that is needed now. On a Mode 1 boot both memories are drives
 * at the same time — B: the console's internal 8 KB, C: the
 * cartridge's save RAM — so lifting one into the other is a program you
 * run from the desktop, and what comes out the other end is an ordinary
 * file on an ordinary filesystem.
 *
 * The raw image is the point. B: is usually a FAT12 volume we put
 * there ourselves, in which case the desktop can copy files off it and
 * this tool is a convenience. But the interesting case is the one where
 * it is not: a console that has been played holds Sega-format game
 * saves, which no filesystem on this machine can read and which are
 * exactly what someone would want off the machine before anything
 * reformats them. Sector-level reads see those; a file copy cannot.
 *
 * Physical mode, for the same reason FORMAT.PRG uses it: logical mode
 * needs the volume to be loggable, and a volume this tool exists to
 * rescue is one GEMDOS has already refused to log.
 */

typedef unsigned char UBYTE;
typedef unsigned long ULONG;

/* Handed to the BIOS, so it must be even -- see progs/cdtest.c. */
#define OSBUF __attribute__((aligned(4)))

void con_ws(const char *s);
long con_in(void);
long dos_cconis(void);
long bios_rwabs(long rw, void *buf, long count, long recno, long dev);
long dos_fcreate(const char *name, long attr);
long dos_fwrite(long handle, long count, const void *buf);
long dos_fclose(long handle);

#define RW_READ     0
#define RW_PHYS     (8 | 2)     /* RW_NOTRANSLATE | RW_NOMEDIACH */

#define SRC         8L          /* I: the internal backup RAM */
#define SECSIZE     512
#define MAXSECS     64          /* 32 KB: past anything the console has */

static const char outname[] = "S:\\BRAM.BIN";
static UBYTE buf[SECSIZE] OSBUF;

/* Decimal by repeated subtraction: a 32-bit divide pulls in libgcc, and
 * libgcc is not built -mpcrel — its helpers call each other absolutely,
 * which lands in low memory once the program is loaded anywhere but its
 * link address. Same reasoning as FORMAT.PRG, same four digits. */
static void putnum(unsigned short v)
{
    static const unsigned short pow10[5] = { 10000, 1000, 100, 10, 1 };
    char t[6];
    int i, j = 0, started = 0;

    for (i = 0; i < 5; i++) {
        int d = 0;
        while (v >= pow10[i]) { v -= pow10[i]; d++; }
        if (d || started || i == 4) { t[j++] = (char)('0' + d); started = 1; }
    }
    t[j] = 0;
    con_ws(t);
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
    long fh;
    int sec;

    con_ws("\r\nSRAMTOOL — internal backup RAM to the cartridge\r\n\r\n");

    /* Size it by reading, not by being told. The console's internal
     * memory is 8 KB, but this program has no way to know it is running
     * on the machine it was written for, and a read that fails is the
     * same answer either way. */
    for (sec = 0; sec < MAXSECS; sec++)
        if (bios_rwabs(RW_READ | RW_PHYS, buf, 1, sec, SRC) != 0)
            break;

    if (sec == 0) {
        con_ws("B: did not answer a single sector.\r\n"
               "There is no internal backup RAM to read.\r\n\r\n"
               "Press any key.\r\n");
        waitkey();
        return 0;
    }

    con_ws("B: holds ");
    putnum((unsigned short)(sec / 2));
    con_ws(" KB. Writing C:\\BRAM.BIN ...\r\n");

    fh = dos_fcreate(outname, 0);
    if (fh < 0) {
        con_ws("Cannot create the file on C: (");
        putnum((unsigned short)-fh);
        con_ws(").\r\nIs the cartridge fitted and formatted?\r\n\r\n"
               "Press any key.\r\n");
        waitkey();
        return 0;
    }

    {
        int i;
        for (i = 0; i < sec; i++) {
            if (bios_rwabs(RW_READ | RW_PHYS, buf, 1, i, SRC) != 0) {
                /* It answered a moment ago during the sizing pass, so
                 * this is a real fault and not the end of the memory.
                 * Say which sector: a short file that stops in the same
                 * place twice is a different diagnosis from one that
                 * does not. */
                con_ws("Read failed at sector ");
                putnum((unsigned short)i);
                con_ws(".\r\n");
                break;
            }
            if (dos_fwrite(fh, SECSIZE, buf) != SECSIZE) {
                con_ws("C: ran out of room at sector ");
                putnum((unsigned short)i);
                con_ws(".\r\n");
                break;
            }
        }
        dos_fclose(fh);

        if (i == sec) {
            con_ws("Done. ");
            putnum((unsigned short)(sec / 2));      /* 512-byte sectors */
            con_ws(" KB written.\r\n\r\n"
                   "Power off and read the cartridge's save file on a\r\n"
                   "host: BRAM.BIN is the raw image, Sega-format saves\r\n"
                   "and all.\r\n");
        }
    }

    con_ws("\r\nPress any key.\r\n");
    waitkey();
    return 0;
}
