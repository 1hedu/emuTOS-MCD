/* BRAMRW.PRG -- can a Backup RAM file be written in place?
 *
 * THIS PROGRAM WRITES TO BACKUP RAM. It creates one file called
 * BRMPROBE, alters a byte under it, and deletes it again. It is
 * emulator-only and build-rom.sh refuses to put it on a cartridge.
 *
 * The question it settles decides the whole shape of putting our
 * filesystem inside Sega's format. BRMSERCH hands back the file's start
 * address in backup RAM, so *reading* a sector out of a file is a
 * pointer dereference and costs nothing. Writing is the open question:
 * BRMWRITE takes a whole file at once, which for a 128 KB volume is not
 * a sector write, it is a rewrite of everything.
 *
 * So: write a file of known bytes, find it, read it back through the
 * pointer, change one byte through the pointer, and then ask the
 * manager -- BRMVERIFY and BRMREAD -- what it thinks. Either
 *
 *   the altered byte comes back and BRMVERIFY reports a mismatch
 *       -- the data is stored plainly and in the clear, a sector can be
 *          written straight through the pointer, and BRMWRITE is only
 *          needed to create and resize; or
 *
 *   the altered byte does not come back, or something else breaks
 *       -- the manager keeps its own accounting over file data and the
 *          design has to go the long way round.
 *
 * Both answers are useful. Guessing between them is not.
 */
typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;

#include "scdapi.h"

void con_ws(const char *s);
long con_in(void);
long xbios_supexec(long func);

/* Supervisor mode, always -- see progs/bramtest.c. */
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

static const char hexd[] = "0123456789ABCDEF";
static void puthex2(unsigned long v)
{
    char t[3];
    t[0] = hexd[(v >> 4) & 15]; t[1] = hexd[v & 15]; t[2] = 0;
    con_ws(t);
}
static void puthex8(unsigned long v)
{
    int i;
    char t[9];
    for (i = 0; i < 8; i++) t[i] = hexd[(v >> (28 - 4 * i)) & 15];
    t[8] = 0;
    con_ws(t);
}

/* The call block and every buffer are statics of this .PRG, which is
 * loaded well above 0x6000, so the driver will accept them. */
static struct brmcall c;
static char  fname[12]  = "BRMPROBE\0\0\0";
static UBYTE info[14];                  /* filename[11], mode, size.w */
static UBYTE data[64];                  /* one 0x40 block, normal mode */
static UBYTE back[64];

static int call(unsigned short fn)
{
    c.fn = fn;
    if (ctl(SCD_BRAM_CALL, (long)&c) != 0L) {
        con_ws("  driver refused the call\r\n");
        return 0;
    }
    return 1;
}

int pmain(void);
int pmain(void)
{
    int i;
    UBYTE *p;
    int stride = 0, base;

    con_ws("BRAMRW: can a backup RAM file be written in place?\r\n");
    con_ws("*** this writes to backup RAM ***\r\n\r\n");

    g_api = find_api();
    if (!g_api) { con_ws("no 'SgCD' cookie\r\n"); goto done; }

    /* --- 1. BRMINIT, which everything else needs first --- */
    c.d1in = 0; c.a0in = 0; c.a1in = 0;
    if (!call(BRM_INIT)) goto done;
    con_ws("init    cs="); puthex2(c.cs);
    con_ws(" size="); puthex2(c.d0out);
    con_ws("\r\n");
    if (c.cs) { con_ws("not a Sega volume; stopping.\r\n"); goto done; }

    /* --- 2. write one block of known bytes --- */
    for (i = 0; i < 11; i++) info[i] = (UBYTE)fname[i];
    info[11] = 0;                       /* normal mode: 0x40 per block */
    info[12] = 0; info[13] = 1;         /* one block */
    for (i = 0; i < 64; i++) data[i] = (UBYTE)(0xA0 + i);

    c.d1in = 0; c.a0in = (ULONG)info; c.a1in = (ULONG)data;
    if (!call(BRM_WRITE)) goto done;
    con_ws("write   cs="); puthex2(c.cs); con_ws("\r\n");
    if (c.cs) { con_ws("the write failed; stopping.\r\n"); goto done; }

    /* --- 3. find it --- */
    c.d1in = 0; c.a0in = (ULONG)fname; c.a1in = 0;
    if (!call(BRM_SERCH)) goto done;
    con_ws("serch   cs="); puthex2(c.cs);
    con_ws(" blocks="); puthex2(c.d0out);
    con_ws(" at="); puthex8(c.a0out);
    con_ws("\r\n");
    if (c.cs) { con_ws("not found after writing it; stopping.\r\n"); goto done; }

    /* --- 4. what is under that pointer? ---
     *
     * Backup RAM is odd bytes of the sub CPU's bus, so a byte array
     * there has a stride of two. Both are printed rather than assumed:
     * whichever one shows the bytes we wrote is the answer. */
    p = (UBYTE *)c.a0out;
    con_ws("raw     ");
    for (i = 0; i < 8; i++) puthex2(p[i]);
    con_ws("\r\nwrote   ");
    for (i = 0; i < 8; i++) puthex2(data[i]);
    con_ws("\r\n");

    /* Backup RAM is the odd byte of each word on the sub CPU's bus --
     * our own bram_rd() has always read it as base + i*2 + 1 -- so the
     * address the manager hands back is the even one and the data is a
     * byte further on, two apart. Both the offset and the step are
     * searched for rather than assumed, because assuming offset 0 is
     * what made the first run of this print "neither matches" over a
     * perfectly good FF A0 FF A1. */
    base = -1;
    for (i = 0; i < 4 && base < 0; i++) {
        int off = i & 1, st = (i < 2) ? 1 : 2;
        int k, ok = 1;
        for (k = 0; k < 8; k++)
            if (p[off + st * k] != data[k]) { ok = 0; break; }
        if (ok) { base = off; stride = st; }
    }
    if (base < 0) { con_ws("data not found under the pointer\r\n"); goto done; }
    con_ws("layout  offset "); puthex2(base);
    con_ws(" step "); puthex2(stride); con_ws("\r\n\r\n");

    /* --- 5. verify before touching anything --- */
    c.d1in = 0; c.a0in = (ULONG)info; c.a1in = (ULONG)data;
    if (!call(BRM_VERIFY)) goto done;
    con_ws("verify  cs="); puthex2(c.cs);
    con_ws(" d0="); puthex2(c.d0out); con_ws("  (before)\r\n");

    /* --- 6. alter one byte in place --- */
    p[base + 5 * stride] ^= 0xFF;
    con_ws("altered byte 5 through the pointer\r\n");

    /* --- 7. and ask again --- */
    c.d1in = 0; c.a0in = (ULONG)info; c.a1in = (ULONG)data;
    if (!call(BRM_VERIFY)) goto done;
    con_ws("verify  cs="); puthex2(c.cs);
    con_ws(" d0="); puthex2(c.d0out); con_ws("  (after)\r\n");

    for (i = 0; i < 64; i++) back[i] = 0;
    c.d1in = 0; c.a0in = (ULONG)fname; c.a1in = (ULONG)back;
    if (!call(BRM_READ)) goto done;
    con_ws("read    cs="); puthex2(c.cs); con_ws("  ");
    for (i = 0; i < 8; i++) puthex2(back[i]);
    con_ws("\r\n");

    con_ws("\r\nbyte 5: wrote "); puthex2(data[5]);
    con_ws(", read back "); puthex2(back[5]);
    con_ws("\r\n");
    if (back[5] == (UBYTE)(data[5] ^ 0xFF))
        con_ws("IN PLACE WORKS: the change came back.\r\n");
    else if (back[5] == data[5])
        con_ws("NOT IN PLACE: the manager did not see the change.\r\n");
    else
        con_ws("neither: something else is going on.\r\n");

    /* --- 8. put the volume back --- */
    c.d1in = 0; c.a0in = (ULONG)fname; c.a1in = 0;
    if (call(BRM_DEL))
        { con_ws("delete  cs="); puthex2(c.cs); con_ws("\r\n"); }

done:
#ifdef BRAMRW_AUTO
    for (;;)
        ;
#else
    con_ws("\r\nPress a key.\r\n");
    con_in();
#endif
    return 0;
}
