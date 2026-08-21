/* BRAMTEST.PRG -- what does Sega's own Backup RAM manager see?
 *
 * This port has always laid its own FAT12 volume over the whole of the
 * backup memory, which is why a cartridge cannot carry our files and a
 * game's saves at the same time: Sega's volume keeps its footer in the
 * last 0x40 bytes and our data area runs straight through it.
 *
 * The first step out of that is not to write anything. It is to find
 * out whether we can talk to the manager at all -- BRMINIT and BRMSTAT,
 * through the CDBIOS parked in Word RAM, which is the same door the CD
 * timeshare already uses. Both are read-only. Nothing here formats,
 * writes or deletes, and until the answers below are known, nothing
 * should.
 *
 * BRAM_AUTO builds it to run from AUTO and hold, for a headless run.
 */
typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;

#include "scdapi.h"

void con_ws(const char *s);
long con_in(void);
long xbios_supexec(long func);

/* In supervisor mode, always.
 *
 * The cookie hands a program a pointer it can call directly, and a
 * direct call keeps the caller's mode -- so a .PRG arrives in the
 * driver in user mode, and the first privileged instruction on the path
 * takes a privilege violation with this program's name nowhere near it.
 * SCD_BRAM_INFO reaches segacd_buram_call, which masks interrupts with
 * `ori.w #0x0700,sr` before it exchanges low memory, so it is one of
 * the ops that cannot be called any other way. Asking for it from user
 * mode panics at sr=0010 -- which is written down in progs/cdtest.c,
 * about the identical mistake, and was made again here anyway.
 *
 * Supexec is TOS's own answer. Arguments go through globals because it
 * takes a function of no arguments. */
static struct scd_api *g_api;
static long sup_op, sup_a;
static unsigned long sup_ret;

static long sup_control(void)
{
    sup_ret = g_api->control(sup_op, sup_a, 0, 0);
    return 0;
}

static unsigned long ctl(long op, long a)
{
    sup_op = op; sup_a = a;
    xbios_supexec((long)sup_control);
    return sup_ret;
}

static struct scd_api *find_api(void)
{
    unsigned long * volatile * p_cookies = (unsigned long * volatile *)0x5A0L;
    unsigned long *jar = *p_cookies;

    if (!jar) return 0;
    while (jar[0]) {
        if (jar[0] == SCD_COOKIE) {
            struct scd_api *a = (struct scd_api *)jar[1];
            if (a && a->version == SCD_VERSION
                  && a->size == sizeof(struct scd_api))
                return a;
            return 0;
        }
        jar += 2;
    }
    return 0;
}

static void puthex2(unsigned long v)
{
    static const char h[] = "0123456789ABCDEF";
    char t[3];
    t[0] = h[(v >> 4) & 15];
    t[1] = h[v & 15];
    t[2] = 0;
    con_ws(t);
}

/* Repeated subtraction, because a .PRG here is linked without libgcc --
 * its helpers call each other absolutely and cannot survive being
 * loaded off their link address -- so there is no __udivsi3 to divide
 * by ten with. Same shape as progs/diskmark.c's. */
static void putu(unsigned long v)
{
    static const unsigned long p10[10] = {
        1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL,
        10000UL, 1000UL, 100UL, 10UL, 1UL };
    char t[2];
    int i, st = 0;
    t[1] = 0;
    for (i = 0; i < 10; i++) {
        int d = 0;
        while (v >= p10[i]) { v -= p10[i]; d++; }
        if (d || st || i == 9) { t[0] = (char)('0' + d); con_ws(t); st = 1; }
    }
}

int pmain(void);
int pmain(void)
{
    unsigned long r;
    unsigned long st, sz, fr, nf;

    con_ws("BRAMTEST: asking Sega's Backup RAM manager.\r\n\r\n");

    g_api = find_api();
    if (!g_api) {
        con_ws("No 'SgCD' cookie, or it is a different version than\r\n"
               "this program was built against.\r\n");
        goto done;
    }

    r = ctl(SCD_BRAM_INFO, 1);          /* 1: probe now, not cached */
    st = (r >> 24) & 0xFF;
    sz = (r >> 16) & 0xFF;
    fr = (r >> 8) & 0xFF;
    nf = r & 0xFF;

    con_ws("raw ");   puthex2(st); puthex2(sz); puthex2(fr); puthex2(nf);
    con_ws("\r\n\r\n");

    con_ws("status  ");
    switch (st) {
    case 0:    con_ws("no backup RAM answered");            break;
    case 1:    con_ws("present, but unformatted");          break;
    case 2:    con_ws("another format (ours looks like this)"); break;
    case 3:    con_ws("Sega formatted");                    break;
    case 0xFF: con_ws("no firmware parked (Mode 1 has none)"); break;
    default:   con_ws("unexpected: "); putu(st);            break;
    }
    con_ws("\r\n");

    if (st == 3 || st == 2 || st == 1) {
        con_ws("size    "); putu(sz);
        con_ws(" blocks of 4096 = "); putu(sz * 4096UL);
        con_ws(" bytes\r\n");
    }
    if (st == 3) {
        con_ws("free    "); putu(fr); con_ws(" blocks of 64 = ");
        putu(fr * 64UL); con_ws("\r\n");
        con_ws("files   "); putu(nf); con_ws("\r\n");
    }

    con_ws("\r\nNothing written: this call only reads.\r\n");

done:
#ifdef BRAM_AUTO
    for (;;)
        ;
#else
    con_ws("\r\nPress a key.\r\n");
    con_in();
#endif
    return 0;
}
