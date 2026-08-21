/* PRNTEST.PRG -- put a short page on PRN: and let the wire have it.
 *
 * There is no printer on the end of this in the emulator and there is no
 * printer on the end of it here: what this proves is the path. The bytes
 * go Bconout(0) -> the page buffer in segacd.c -> the servant -> the
 * EXT-port UART, and the servant mirrors every transmitted byte into a
 * window the test harness can read. Two dozen bytes is enough to check a
 * sync, a mode, a length, a payload and a checksum, which is the whole
 * of the framing.
 *
 * The page ends on silence -- half a second of nothing written is what
 * says the job is over, because print_file() does not say so -- so this
 * writes its line and then waits.
 */
typedef unsigned char UBYTE;
typedef unsigned long ULONG;

void con_ws(const char *s);
long bios_bconout(long dev, long c);
long dos_super(long stack);

#define HZ200 (*(volatile unsigned long *)0x000004BAL)

static const char page[] = "EmuTOS on the Mega CD\r\n";

void pmain(void);
void pmain(void)
{
    const char *p = page;
    ULONG t;

    con_ws("PRNTEST: writing to PRN:\r\n");
    while (*p)
        bios_bconout(0, (long)(UBYTE)*p++);

    /* Wait out the idle timeout and then some of the wire. At 4800 baud
     * the frame below is 26 bytes, about a twentieth of a second. */
    con_ws("PRNTEST: waiting for the page to go\r\n");
    t = HZ200;
    while (HZ200 - t < 400UL)
        ;
    con_ws("PRNTEST: done\r\n");
}
