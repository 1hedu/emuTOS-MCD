/* pf_mcd.c -- Cyber Paint's PF.ASM, for EmuTOS on the Mega CD.
 *
 * On an ST, PF.ASM takes over four vectors: the VBL ($70), MFP Timer B,
 * the 200 Hz timer and the keyboard. Timer B splits the screen into
 * horizontal stripes so that colours 1-3 are the menu's white, black
 * and red over the menus and the picture's own colours in between.
 *
 * None of that exists here. $70 is the CD drive's interrupt, the MFP
 * addresses alias the gate array (a write there resets it), and the
 * servant shows one sixteen-colour palette per frame. So this keeps
 * PF.ASM's interface -- the stripes, startclist, vbcount, vbcmap -- and
 * does the work from the OS's VBL queue instead, one palette per frame:
 * the picture's colours, with 1-3 taken from the menu stripes whenever
 * a menu shows. Hide the menus (right button) and the picture is exact.
 *
 * Colours go out through colorptr, as Setpalette does: the OS hands
 * them to the servant at the next VBL.
 */
#include <osbind.h>

typedef short WORD;

struct stripe {
    WORD colors[3];
    WORD height;
};

struct stripe stripe1 = { { 0x777, 0x000, 0x700 }, 0 };
struct stripe stripe2 = { { 0x004, 0x007, 0x141 }, 200 };
struct stripe stripe3 = { { 0x777, 0x000, 0x700 }, 255 };
struct stripe stripe4 = { { 0x004, 0x007, 0x141 }, 10 };
struct stripe stripe5 = { { 0x777, 0x000, 0x700 }, 255 };
struct stripe *startclist = &stripe1;

long vbcount;
WORD *vbcmap;

#define NVBLS    (*(volatile WORD *)0x454)
#define VBLQUEUE (*(void (** volatile *)(void))0x456)
#define COLORPTR (*(WORD * volatile *)0x45A)

static WORD cmap[16];           /* the picture's, as vbcmap last gave it */
static WORD out[16];            /* what went to colorptr */
static char hblanks, stashed, have_out;
static void (**slot)(void);

static int menu_stripe(const struct stripe *s)
{
    return s == &stripe1 || s == &stripe3 || s == &stripe5;
}

/* The stripe walk PF.ASM's VBL and Timer B handlers do between them:
 * the first stripe is skipped if its height is 0, and each one lasts
 * its height (low byte) in lines. Without Timer B, the first stripe
 * covers the screen. Returns the stripe whose colours 1-3 to show. */
static const struct stripe *pick(void)
{
    const struct stripe *s = startclist, *pic = 0;
    int lines = 0;

    if (!(s->height & 0xff))
        s++;
    for (;;) {
        int h = s->height & 0xff;
        if (h && menu_stripe(s))
            return s;
        if (!pic)
            pic = s;
        lines += h;
        if (!hblanks || lines >= 200 || s == &stripe5)
            return pic;
        s++;
    }
}

static void pf_vbl(void)
{
    const struct stripe *s;
    WORD *p = vbcmap;
    int i, changed = !have_out;

    vbcount++;
    if (p) {
        for (i = 0; i < 16; i++)
            cmap[i] = p[i];
        vbcmap = 0;
    }
    s = stashed ? &stripe1 : pick();
    for (i = 0; i < 16; i++) {
        WORD c = (i >= 1 && i <= 3) ? s->colors[i - 1] : cmap[i];
        if (c != out[i]) {
            out[i] = c;
            changed = 1;
        }
    }
    if (changed) {
        have_out = 1;
        COLORPTR = out;
    }
}

/* All of these run in supervisor mode: Supexec, as on an ST. */
void pfinit(void)
{
    void (**q)(void) = VBLQUEUE;
    WORD i, n = NVBLS;

    for (i = 0; i < 16; i++)    /* what the screen shows now */
        cmap[i] = (WORD)Setcolor(i, -1);
    for (i = 0; i < n; i++)
        if (!q[i]) {
            q[i] = pf_vbl;
            slot = &q[i];
            break;
        }
}

void pfcleanup(void)
{
    if (slot) {
        *slot = 0;
        slot = 0;
    }
    hblanks = 0;
}

void timebon(void)
{
    hblanks = 1;
}

void timeboff(void)
{
    hblanks = 0;
}

/* textbox.c, around a dialog: "continue making colors visible". The
 * box is drawn in the menu colours, so they hold until it goes. */
void stash_cmap(void)
{
    stashed = 1;
}

void unstash_cmap(void)
{
    stashed = 0;
}
