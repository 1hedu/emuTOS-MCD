/* NEO.PRG -- NEOchrome 1.0 on this machine.
 *
 * NEOchrome is one program, NEONEW.PRG, and nearly all of it is
 * ordinary TOS: GEMDOS for files, Setscreen, Line-A for drawing and the
 * mouse. Three things in it are ST hardware, and each one is something
 * this machine does differently:
 *
 *   - its VBL routine goes in vector $70, which here is the CD drive's
 *     interrupt (the VBL is level 2). That routine copies NEOchrome's
 *     palette to the shifter every frame, counts frames for its timing,
 *     and starts MFP Timer B for the toolbox's colour bands.
 *   - it enables Timer B in the MFP. The MFP's addresses alias the gate
 *     array on the sub CPU, so those writes land in its registers.
 *   - it reads the shifter's palette at $FF8240 to put it back at exit.
 *
 * So this launcher loads NEONEW.PRG without starting it (Pexec 3),
 * patches those places in memory, and then starts it (Pexec 4). The VBL
 * work runs from the OS's VBL queue instead, here, in the launcher,
 * which stays resident underneath it. The colours Timer B changed down
 * the toolbox -- its text colours from line 109, and the colour
 * picker's seventeen bands of thirteen, five lines each, from 114 --
 * go to EmuTOS as a raster table (SCD_RASTER), and the servant makes
 * the same changes on the same lines from the VDP's line interrupt.
 *
 * And it chooses where NEOchrome's two screens go. NEOchrome lays out
 * its buffers back to back, 32000 bytes apart, and this machine needs
 * two things of a screen that an ST does not: 768 free bytes past it,
 * where EmuTOS and the servant keep the palette and pointer blocks, and
 * not crossing a 128 KB boundary in sub RAM, because the servant reads
 * the screen through one 128 KB bank. The launcher computes the layout
 * itself and skips NEOchrome's own; if this load address leaves no room
 * for one, it loads NEOchrome a little higher and tries again.
 *
 * Every patch site is checked against the bytes NEOchrome 1.0 has there
 * before anything is written; any other NEONEW.PRG is refused.
 * tools/install-neochrome.sh puts both programs on D:.
 */
typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;

#include "scdapi.h"

void con_ws(const char *s);
long con_in(void);
long dos_pexec(long mode, const char *name, const void *cmd, const void *env);
long dos_mfree(void *block);
long dos_malloc(long bytes);
long xbios_setcolor(long pen, long colour);
extern void *prg_basepage;

/* What NEONEW.PRG of NEOchrome 1.0 is, from its header. */
#define NEO_TLEN 54804UL
#define NEO_DLEN 880UL
#define NEO_BLEN 148676UL

/* NEOchrome's variables, as offsets from the start of its text; its
 * data and BSS follow the text in memory. */
#define V_WORK     0xDE08UL   /* long: a 32000-byte work buffer */
#define V_SCR_A    0xDE0CUL   /* long: the screen shown */
#define V_SCR_B    0xDE10UL   /* long: the screen drawn behind it */
#define V_BUF_C    0xDE14UL   /* long: buffer after the screens */
#define V_BUF_D    0xDE18UL   /* long: and the one after that */
#define V_PALPTR   0xD95EUL   /* long: the palette its VBL copies */
#define V_TICK1    0xD978UL   /* byte: counts up from negative, per VBL */
#define V_TICK2    0xD979UL   /* byte: the same */
#define V_FRAMES   0x31AF6UL  /* word: frames, for its timing */
#define V_TOOLBOX  0x31B56UL  /* word: nonzero while the toolbox shows */
#define V_TOOLCOL  0x31B6AUL  /* word: the toolbox's colour 14 */
#define V_BANDS    0x31B6EUL  /* 17 x 13 words: the picker's bands */
#define V_REGION   0x10360UL  /* where its own layout starts, unaligned */

#define SCREEN     32000UL
#define SCREEN_PAD 32256UL    /* + the blocks, and still 256-aligned */
#define REGION     99840UL    /* its two screens, work buffer and gap */
#define BANK       0x20000UL

#define NVBLS    (*(volatile UWORD *)0x454)
#define VBLQUEUE (*(void (** volatile *)(void))0x456)
#define COLORPTR (*(UWORD * volatile *)0x45AUL)

static UBYTE *neo;                  /* its text */
static struct scd_api *api;

/* The raster tables, two so that the one the servant may be reading is
 * never the one being rewritten: line 109's colours 14 and 15, then the
 * seventeen bands of colours 1-13 from line 114, five lines apart.
 * 2 + 4 + 17 * 15 words. */
#define RAS_WORDS (2 + 4 + 17 * 15)
static UWORD ras[2][RAS_WORDS];
static UWORD ras_cur;
static UWORD start_pal[16];         /* the screen's, for it to restore */
static UWORD out[16];
static UBYTE have_out;
static void (**slot)(void);

#define W(off)  (*(volatile UWORD *)(neo + (off)))
#define L(off)  (*(volatile ULONG *)(neo + (off)))
#define B(off)  (*(volatile UBYTE *)(neo + (off)))

/* The toolbox's raster, as Timer B drew it: from line 109 colour 15
 * the opposite of the background and 14 the toolbox's own, then band k
 * of the picker from line 114 + 5k. (Timer B ran an eighteenth time,
 * at line 199, from past the end of the table; that line is left as
 * band 16 left it.) No toolbox, no entries. */
static void neo_raster(const UWORD *pal)
{
    UWORD *t = ras[ras_cur ^ 1];
    const UWORD *cur = ras[ras_cur];
    UWORD *w = t + 2;
    int k, i, n = 0;

    if (W(V_TOOLBOX)) {
        *w++ = 109; *w++ = 0xC000;
        *w++ = W(V_TOOLCOL) & 0x777;
        *w++ = (UWORD)~pal[0] & 0x777;
        n++;
        for (k = 0; k < 17; k++) {
            const volatile UWORD *b =
                (const volatile UWORD *)(neo + V_BANDS + 26UL * (ULONG)k);
            *w++ = (UWORD)(114 + 5 * k); *w++ = 0x3FFE;
            for (i = 0; i < 13; i++) *w++ = b[i] & 0x777;
            n++;
        }
    }
    t[1] = (UWORD)n;
    for (i = 1; i < (int)(w - t); i++)
        if (t[i] != cur[i])
            break;
    if (i == (int)(w - t) && cur[1] == n)
        return;                         /* nothing moved */
    t[0] = (UWORD)(cur[0] + 1);
    ras_cur ^= 1;
    if (api)
        api->control(SCD_RASTER, (long)t, 0, 0);
}

/* What its VBL did, less the hardware. */
static void neo_vbl(void)
{
    const UWORD *p = (const UWORD *)L(V_PALPTR);
    int i, changed = !have_out;

    W(V_FRAMES)++;
    if (B(V_TICK1) & 0x80) B(V_TICK1)++;
    if (B(V_TICK2) & 0x80) B(V_TICK2)++;
    if (!p)
        return;
    for (i = 0; i < 16; i++) {
        UWORD c = p[i] & 0x777;
        if (c != out[i]) {
            out[i] = c;
            changed = 1;
        }
    }
    if (changed) {
        have_out = 1;
        COLORPTR = out;
    }

    /* The table only when something in it can have moved. This runs in
     * the VBL, and the sub CPU has little time to spare: the servant
     * holds its bus for most of every frame while it reads the screen,
     * and rebuilding 261 words each frame was enough to leave
     * NEOchrome's own loop no time at all. Its toolbox flag and the two
     * text colours are checked every frame; the picker's bands, which
     * NEOchrome sets up once, every 64th. */
    {
        static UWORD tb, c14, c15;
        static UBYTE tick;
        UWORD ntb = W(V_TOOLBOX), n14 = W(V_TOOLCOL), n15 = p[0];
        if (ntb != tb || n14 != c14 || n15 != c15 || !(++tick & 63)) {
            tb = ntb; c14 = n14; c15 = n15;
            neo_raster(p);
        }
    }
}

/* Called by NEOchrome, in supervisor mode, where it hooked $70 and
 * where it put $70 back. */
/* The Sega CD driver's API, from its cookie. Without it -- an older
 * EmuTOS -- NEOchrome still runs, with one palette per frame. */
static struct scd_api *find_api(void)
{
    ULONG *jar = *(ULONG * volatile *)0x5A0L;

    if (!jar) return 0;
    for (; jar[0]; jar += 2)
        if (jar[0] == SCD_COOKIE) {
            struct scd_api *a = (struct scd_api *)jar[1];
            return (a && a->version >= 36) ? a : 0;
        }
    return 0;
}

__attribute__((used, noinline)) static void vbl_install(void)
{
    void (**q)(void) = VBLQUEUE;
    UWORD i, n = NVBLS;

    api = find_api();

    for (i = 0; i < n; i++)
        if (!q[i]) {
            q[i] = neo_vbl;
            slot = &q[i];
            return;
        }
}

__attribute__((used, noinline)) static void vbl_remove(void)
{
    if (api)
        api->control(SCD_RASTER, 0, 0, 0);
    if (slot) {
        *slot = 0;
        slot = 0;
    }
}

/* Both reached by jsr from NEOchrome's own code, so they must keep
 * every register it might hold: d2-d7/a2-a6 the C ABI saves already;
 * d0-d1/a0-a1 are saved here. */
__asm__(
    "neo_install:\n\t"
    "movem.l %d0-%d1/%a0-%a1,-(%sp)\n\t"
    "bsr     vbl_install\n\t"
    "movem.l (%sp)+,%d0-%d1/%a0-%a1\n\t"
    "rts\n"
    "neo_remove:\n\t"
    "movem.l %d0-%d1/%a0-%a1,-(%sp)\n\t"
    "bsr     vbl_remove\n\t"
    "movem.l (%sp)+,%d0-%d1/%a0-%a1\n\t"
    "rts\n");
void neo_install(void);
void neo_remove(void);

static int same(ULONG off, const UWORD *w, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (W(off + 2UL * (ULONG)i) != w[i])
            return 0;
    return 1;
}

static void nops(ULONG off, int words)
{
    while (words--) {
        W(off) = 0x4E71;
        off += 2;
    }
}

static void jsr_to(ULONG off, void (*fn)(void))
{
    W(off) = 0x4EB9;
    L(off + 2) = (ULONG)fn;
}

/* The patch sites, and what NEOchrome 1.0 has at each. A relocated
 * long is checked by its opcode word only. */
static const UWORD at_layout[]   = { 0x223C };                   /* move.l #region,d1 */
static const UWORD at_palread[]  = { 0x207C, 0x00FF, 0x8240 };   /* movea.l #$FF8240,a0 */
static const UWORD at_iera_off[] = { 0x0239, 0x00FE, 0x00FF, 0xFA07 };
static const UWORD at_iera_on[]  = { 0x0039, 0x0001, 0x00FF, 0xFA07 };
static const UWORD at_imra_on[]  = { 0x0039, 0x0001, 0x00FF, 0xFA13 };
static const UWORD at_imra_off[] = { 0x0239, 0x00FE, 0x00FF, 0xFA13 };
static const UWORD at_vbl_save[] = { 0x23F9, 0x0000, 0x0070 };   /* move.l $70,... */
static const UWORD at_vbl_set[]  = { 0x23FC };                   /* move.l #vbl,$70 */
static const UWORD at_vbl_back[] = { 0x23F9 };                   /* move.l ...,$70 */

static int recognised(void)
{
    return same(0x36, at_layout, 1)
        && same(0x144, at_palread, 3)
        && same(0x1AE, at_iera_off, 4)
        && same(0x1CA, at_iera_on, 4)
        && same(0x1D2, at_imra_on, 4)
        && same(0x1DA, at_vbl_save, 3)
        && same(0x1E4, at_vbl_set, 1) && L(0x1EA) == 0x70
        && same(0x1804, at_iera_off, 4)
        && same(0x180C, at_imra_off, 4)
        && same(0x181E, at_vbl_back, 1) && L(0x1824) == 0x70;
}

static void patch(void)
{
    /* Its layout code, 0x36-0x9D, is skipped: the launcher has set the
     * five pointers it would have computed. */
    W(0x36) = 0x6000;                   /* bra.w 0x9E */
    W(0x38) = 0x0066;
    /* The palette it saves at start and restores at exit. */
    L(0x146) = (ULONG)start_pal;
    /* Timer B on, at start, and off, at exit. */
    nops(0x1AE, 4);
    nops(0x1CA, 4);
    nops(0x1D2, 4);
    nops(0x1804, 4);
    nops(0x180C, 4);
    /* $70 taken, and given back. */
    jsr_to(0x1DA, neo_install);
    nops(0x1E0, 7);
    jsr_to(0x181E, neo_remove);
    nops(0x1824, 2);
}

/* Where the two screens and the work buffer go in the region its own
 * layout would have used. Offsets from the region's 256-aligned start;
 * k is where a 128 KB boundary falls in it. Zero when there is no
 * layout for this k, and *shift is then how much higher to load. */
static int layout(ULONG k, ULONG *s1, ULONG *s2, ULONG *w, ULONG *shift)
{
    if (k >= 2 * SCREEN_PAD) {                  /* both below it */
        *s1 = 0; *s2 = SCREEN_PAD; *w = 2 * SCREEN_PAD;
        return 1;
    }
    if (k <= REGION - 2 * SCREEN_PAD - SCREEN) {    /* all above it */
        *s1 = k; *s2 = k + SCREEN_PAD; *w = k + 2 * SCREEN_PAD;
        return 1;
    }
    if (k >= SCREEN && k <= REGION - 2 * SCREEN_PAD) {  /* buffer below */
        *w = 0; *s1 = k; *s2 = k + SCREEN_PAD;
        return 1;
    }
    *shift = (k < SCREEN) ? k - (REGION - 2 * SCREEN_PAD - SCREEN)
                          : k - (REGION - 2 * SCREEN_PAD);
    return 0;
}

static void fail(const char *why)
{
    con_ws("\033ENEOchrome: ");
    con_ws(why);
    con_ws("\r\n\r\nPress a key.");
    con_in();
}

static long load(void)
{
    long bp = dos_pexec(3, "NEONEW.PRG", "", 0);
    if (bp < 0)
        bp = dos_pexec(3, "D:\\NEOCHROM\\NEONEW.PRG", "", 0);
    return bp;
}

static void unload(long bp)
{
    dos_mfree(*(void **)(bp + 44));     /* p_env */
    dos_mfree((void *)bp);
}

int pmain(void)
{
    ULONG base, k, s1 = 0, s2 = 0, w = 0, shift = 0, held = 0;
    void *pad = 0;
    long bp;
    int i, tries;

    (void)prg_basepage;
    for (i = 0; i < 16; i++)
        start_pal[i] = (UWORD)xbios_setcolor(i, -1);

    for (tries = 0; ; tries++) {
        bp = load();
        if (bp < 0) {
            fail("NEONEW.PRG is not here or does not fit.");
            return 1;
        }
        neo = *(UBYTE **)(bp + 8);      /* p_tbase */
        if (*(ULONG *)(bp + 12) != NEO_TLEN
            || *(ULONG *)(bp + 20) != NEO_DLEN
            || *(ULONG *)(bp + 28) != NEO_BLEN || !recognised()) {
            unload(bp);
            fail("this NEONEW.PRG is not NEOchrome 1.0.");
            return 1;
        }
        base = ((ULONG)neo + V_REGION + 255) & ~255UL;
        k = BANK - (base & (BANK - 1));
        if (k >= REGION)
            k = REGION;                 /* no boundary inside it */
        if (layout(k, &s1, &s2, &w, &shift))
            break;
        unload(bp);
        if (pad) dos_mfree(pad);
        if (tries == 3) {
            fail("no room for its screens.");
            return 1;
        }
        /* Load it higher by holding memory below it. Rounded up, so a
         * basepage that lands a few bytes differently still clears. */
        held += (shift + 511) & ~255UL;
        pad = (void *)dos_malloc((long)held);
        if ((long)pad <= 0) {
            fail("no room for its screens.");
            return 1;
        }
    }
    if (pad)
        dos_mfree(pad);                 /* below it, and free for its Mallocs */

    L(V_SCR_A) = base + s1;
    L(V_SCR_B) = base + s2;
    L(V_WORK)  = base + w;
    L(V_BUF_C) = base + REGION;                 /* as its own layout */
    L(V_BUF_D) = base + REGION + 17920;
    patch();

    dos_pexec(4, 0, (void *)bp, 0);
    vbl_remove();                       /* if it left without its exit */
    unload(bp);
    return 0;
}
