/* Staging a native payload, shared by the two things that do it.
 *
 * NATIVE.PRG is a menu: it lists the .MDP files in a folder and runs the
 * one you pick, and it says what it is doing while it does it. SONIC.ACC
 * is a desk accessory: it runs one payload, from the Desk menu, over the
 * desktop that is already on the screen, and it must say nothing at all
 * -- a single character of console output would be printed straight into
 * the picture Sonic is about to walk on.
 *
 * So the loading is here and the talking is not. Define PAYLOAD_QUIET
 * before including this and every message compiles away to nothing.
 *
 * docs/payload.md is the contract on the other side. Nothing in here
 * knows what any payload does.
 */

#ifndef PAYLOAD_H
#define PAYLOAD_H

#include "scdapi.h"

void con_ws(const char *s);
long dos_fopen(const char *name, long mode);
long dos_fread(long handle, long count, void *buf);
long dos_fclose(long handle);
long xbios_supexec(long func);

/* The payload header, as docs/payload.md defines it. Read here only to
 * refuse a file that is plainly not one before spending twenty seconds
 * feeding it across; the servant checks it again and is the authority. */
struct hdr {
    ULONG magic;
    UWORD version, flags;
    ULONG entry, length, workspace;
    UBYTE name[8];
};

#define PAYLOAD_MAX 32000UL

/* Bulk data lands in whatever PRG RAM the C: ramdisk did not take: the
 * driver knows where that starts and how much of it there is, and is
 * asked rather than guessed at. docs/payload.md, "Bulk data".
 *
 * This used to be a constant, 0x60000, and 0x60000 is where the ramdisk
 * begins. Loading fifty kilobytes there overwrote the boot drive.
 *
 * ...and when the file is *on* the ramdisk there is nothing to copy at
 * all. The ramdisk is PRG RAM, in the same window bank, and a file in it
 * is already at an address the servant can hand to the payload. That is
 * not an optimisation: a 52KB file and a 52KB arena do not both fit in
 * the 112KB the region has, so a machine that boots from a cartridge --
 * where C: is the only drive there is -- could not run Sonic any other
 * way. */
static ULONG bulk_base, bulk_len, disk_base, disk_len;
static ULONG arena[4];
static UBYTE block[512];

static struct scd_api *g_api;
static long sup_op, sup_a, sup_b;

#ifdef PAYLOAD_QUIET
#define say(s)   ((void)0)
#define sayn(v)  ((void)0)
#else
#define say(s)   con_ws(s)
#define sayn(v)  pl_putn(v)
static void pl_putn(long v)
{
    static const long p10[6] = { 100000L, 10000L, 1000L, 100L, 10L, 1L };
    int i, started = 0;
    for (i = 0; i < 6; i++) {
        int d = 0;
        while (v >= p10[i]) { v -= p10[i]; d++; }
        if (d || started || i == 5) {
            char s[2]; s[0] = (char)('0' + d); s[1] = 0;
            con_ws(s); started = 1;
        }
    }
}
#endif

static long pl_sup_control(void)
{
    g_api->control(sup_op, sup_a, sup_b, 0);
    return 0;
}

static void ctl(long op, long a, long b)
{
    sup_op = op; sup_a = a; sup_b = b;
    xbios_supexec((long)pl_sup_control);
}

/* The driver, out of the cookie jar. Zero if it is not there or is not
 * the version this was built against. */
static int payload_open(void)
{
    unsigned long * volatile *p_cookies = (unsigned long * volatile *)0x5A0L;
    unsigned long *jar = *p_cookies;
    g_api = 0;
    if (jar)
        for (; jar[0]; jar += 2)
            if (jar[0] == SCD_COOKIE) { g_api = (struct scd_api *)jar[1]; break; }
    return g_api && g_api->version == SCD_VERSION;
}

static int pl_same(const UBYTE *p, const UBYTE *q, int n)
{
    while (n--) if (*p++ != *q++) return 0;
    return 1;
}

/* Is this file already sitting in the ramdisk, and where?
 *
 * A file on C: is a run of sectors in an image that is itself PRG RAM,
 * so if the run is contiguous its bytes are already at one address in
 * the window. Rather than walk the FAT to find out -- which would mean
 * this program knowing the filesystem, and trusting it -- it looks for
 * the file's first block in the image and then reads the whole file
 * through, comparing. What comes back is either an address that is
 * proved correct byte for byte, or nothing.
 *
 * Candidates are 512 apart because that is what a FAT data area is
 * aligned to, and there are at most a couple of hundred of them.
 */
static ULONG payload_in_place(const char *name, ULONG *len_out)
{
    ULONG cand[8];
    int ncand = 0, i;
    long fh, n;
    ULONG off;

    if (!disk_len) return 0;

    fh = dos_fopen(name, 0);
    if (fh < 0) return 0;
    n = dos_fread(fh, 512L, block);
    dos_fclose(fh);
    if (n <= 0) return 0;

    for (off = 0; off + 512UL <= disk_len && ncand < 8; off += 512UL)
        if (pl_same((const UBYTE *)(disk_base + off), block, (int)n))
            cand[ncand++] = disk_base + off;

    for (i = 0; i < ncand; i++) {
        ULONG done = 0;
        int ok = 1;
        fh = dos_fopen(name, 0);
        if (fh < 0) return 0;
        for (;;) {
            n = dos_fread(fh, 512L, block);
            if (n <= 0) break;
            if (cand[i] + done + (ULONG)n > disk_base + disk_len
                || !pl_same((const UBYTE *)(cand[i] + done), block, (int)n)) {
                ok = 0; break;
            }
            done += (ULONG)n;
        }
        dos_fclose(fh);
        if (ok && done) { *len_out = done; return cand[i]; }
    }
    return 0;
}

/* The companion .MDD, if there is one: either found where it already is
 * or copied into the arena above the ramdisk. Either way the servant is
 * told the address, and the payload is handed it.
 *
 * Absent is not an error. Most payloads have nothing to put here. */
static int payload_bulk(const char *name)
{
    char dname[20];
    long fh, n;
    ULONG done = 0;
    int i;

    bulk_base = bulk_len = disk_base = disk_len = 0;

    for (i = 0; i < 16 && name[i]; i++) dname[i] = name[i];
    dname[i] = 0;
    while (i > 0 && dname[i - 1] != '.') i--;
    if (i < 2) return 1;                /* no extension: no companion */
    dname[i] = 'M'; dname[i + 1] = 'D'; dname[i + 2] = 'D'; dname[i + 3] = 0;

    fh = dos_fopen(dname, 0);
    if (fh < 0) return 1;               /* absent is not an error */
    dos_fclose(fh);

    ctl(SCD_BULK_INFO, (long)arena, 0);
    bulk_base = arena[0];
    bulk_len  = arena[1];
    disk_base = arena[2];
    disk_len  = arena[3];

    /* Where it already is, if it is anywhere. */
    {
        ULONG at = payload_in_place(dname, &done);
        if (at) {
            say(dname); say(" is already in memory: ");
            sayn((long)done); say(" bytes\r\n");
            ctl(SCD_PAYLOAD_BULK, (long)at, (long)done);
            return 1;
        }
    }

    if (!bulk_len) {
        say("No room: the ramdisk fills PRG RAM and\r\n");
        say(dname);
        say(" is not on it. See tools/build-iso.sh,\r\nADISK_SIZE.\r\n");
        return 0;
    }

    fh = dos_fopen(dname, 0);
    if (fh < 0) return 1;
    say("loading "); say(dname);
    for (;;) {
        if (done >= bulk_len) { say(" -- will not fit\r\n"); return 0; }
        n = dos_fread(fh, 8192L, (void *)(bulk_base + done));
        if (n <= 0) break;
        done += (ULONG)n;
        say(".");
    }
    dos_fclose(fh);
    say(" "); sayn((long)done); say(" bytes\r\n");

    ctl(SCD_PAYLOAD_BULK, (long)bulk_base, (long)done);
    return 1;
}

/* Feed one file across and ask for it. The servant acknowledges each
 * block on the same register the sector path uses, so this waits the
 * same way the block layer does. */
static int payload_send(const char *name)
{
    long fh, n;
    ULONG done = 0;
    struct hdr h;

    fh = dos_fopen(name, 0);
    if (fh < 0) { say("will not open.\r\n"); return 0; }

    n = dos_fread(fh, (long)sizeof h, &h);
    if (n != (long)sizeof h || h.magic != 0x4D44504CUL || h.version != 1) {
        dos_fclose(fh);
        say("not a payload.\r\n");
        return 0;
    }
    if (h.length + h.workspace > PAYLOAD_MAX) {
        dos_fclose(fh);
        say("too big for the 32000 bytes there are.\r\n");
        return 0;
    }

    /* Back to the start: the header is part of the image, and the
     * servant wants the file laid down whole from block zero. */
    dos_fclose(fh);
    fh = dos_fopen(name, 0);
    if (fh < 0) return 0;

    say("sending");
    while (done < h.length) {
        int k;
        for (k = 0; k < 512; k++) block[k] = 0;
        n = dos_fread(fh, 512, block);
        if (n <= 0) break;
        ctl(SCD_PAYLOAD_BLOCK, (long)(done / 512UL), (long)block);
        done += 512UL;
        if ((done & 0x0FFFUL) == 0) say(".");
    }
    dos_fclose(fh);
    say("\r\n");

    if (done < h.length) { say("short read -- not running it.\r\n"); return 0; }
    return 1;
}

/* Bulk, image, hand over. The servant acknowledges the run before it
 * jumps, so this returns at once and the payload runs behind it. */
static int payload_start(const char *name)
{
    if (!payload_bulk(name)) return 0;
    if (!payload_send(name)) return 0;
    ctl(SCD_PAYLOAD_RUN, 0, 0);
    return 1;
}

#endif /* PAYLOAD_H */
