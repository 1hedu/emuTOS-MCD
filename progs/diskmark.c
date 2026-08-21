/* DISKMARK.PRG -- sequential read speed of D:, with every byte checked.
 *
 * FILLER.BIN is a deterministic LCG stream, so the verify needs no
 * stored checksums: the expected bytes are recomputed here. A benchmark
 * that does not verify would call a broken read fast.
 */
void con_ws(const char *s);
long dos_fopen(const char *name, long mode);
long dos_fread(long handle, long count, void *buf);
long dos_fclose(long handle);

#define HZ200 (*(volatile unsigned long *)0x000004BAL)
#define CHUNK 16384L

static unsigned char buf[CHUNK] __attribute__((aligned(4)));

/* Bitwise long division: no libgcc, whose absolute jumps break in a
 * relocated .PRG. */
static unsigned long udiv(unsigned long n, unsigned long d)
{
    unsigned long q = 0, r = 0;
    int i;
    if (!d) return 0;
    for (i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= 1UL << i; }
    }
    return q;
}

static void putu(unsigned long v)
{
    static const unsigned long p10[10] = {
        1000000000UL,100000000UL,10000000UL,1000000UL,100000UL,
        10000UL,1000UL,100UL,10UL,1UL };
    char t[2]; int i, st = 0;
    t[1] = 0;
    for (i = 0; i < 10; i++) {
        int d = 0;
        while (v >= p10[i]) { v -= p10[i]; d++; }
        if (d || st || i == 9) { t[0] = (char)('0'+d); con_ws(t); st = 1; }
    }
}

/* The build's LCG, bit for bit: x = (1103515245 x + 12345) & 0x7FFFFFFF,
 * byte = ((x >> 16) & 0xFF) or 0x5A. The multiply is shift-and-add so
 * no libgcc helper is pulled in (its absolute jumps break in a .PRG
 * loaded off its link address). 1103515245 = 0x41C64E6D. */
static unsigned long lcg;
static unsigned char lcg_next(void)
{
    unsigned long x = lcg, acc = 0, m = 0x41C64E6DUL;
    while (m) {
        if (m & 1) acc += x;
        x <<= 1; m >>= 1;
    }
    lcg = (acc + 12345UL) & 0x7FFFFFFFUL;
    {
        unsigned char b = (unsigned char)((lcg >> 16) & 0xFF);
        return b ? b : 0x5A;
    }
}

int pmain(void)
{
    long h, got, i;
    unsigned long t0, ms, total = 0, bad = 0, kps;

    /* DISKMARK_AUTO: emulator only. Runs from AUTO, before the desktop, so
     * a headless run can answer "does D: read?" with no hand on the mouse,
     * and stops after that many bytes so the run is bounded.
     *
     * It exists because that answer had been carried in notes instead of
     * measured. docs/CHECKPOINT.md said D: was intermittent -- the
     * directory reading and the file's own data sector failing -- and that
     * was written on 13 August against the CD branch, before the Mode 1
     * work landed. It then survived a week of being copied forward without
     * once being re-run. This is what re-running it looks like. */
    con_ws("DISKMARK: reading D:\\FILLER.BIN, verifying every byte.\r\n");
    h = dos_fopen("D:\\FILLER.BIN", 0);
    if (h < 0) { con_ws("Cannot open FILLER.BIN.\r\n"); goto done; }

    lcg = 0x1234567UL;
    t0 = HZ200;
    while ((got = dos_fread(h, CHUNK, buf)) > 0) {
        for (i = 0; i < got; i++)
            if (buf[i] != lcg_next())
                bad++;
        total += (unsigned long)got;
        con_ws(".");
#ifdef DISKMARK_AUTO
        if (total >= (unsigned long)DISKMARK_AUTO) break;
#endif
    }
    ms = (HZ200 - t0) * 5;
    dos_fclose(h);
    if (!ms) ms = 1;

    con_ws("\r\nRead ");  putu(total);
    con_ws(" bytes in "); putu(ms);
    con_ws(" ms = ");
    kps = udiv(total >> 10, ms >= 1000 ? udiv(ms, 1000) : 1);
    putu(kps); con_ws(" KB/s\r\n");
    con_ws("Bad bytes: "); putu(bad);
    con_ws(bad ? "  *** FAILED ***\r\n" : "  (all verified)\r\n");
done:
#ifdef DISKMARK_AUTO
    /* Hold, so the result is still on the screen when the frame is dumped.
     * Returning would hand the boot on to the desktop and the desktop would
     * paint over the only thing this program produced. */
    for (;;)
        ;
#endif
    return 0;
}
