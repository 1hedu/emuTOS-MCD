/* VECTOR.MDP -- Vectorman, on the desktop that is on the screen.
 *
 * Desk -> Vectorman, and he drops onto the desktop. Every horizontal
 * edge on it -- the menu bar's underside, a window's frame, the top of
 * an icon -- is a surface he can land on, and he jumps up through them
 * the way he jumps up through the girders of the level he comes from.
 * The d-pad walks, a tap the other way turns him round, C jumps and C
 * again in the air fires his boots, A or B shoots -- standing, running
 * or in the air -- down looks down and down with C drops through what
 * he is standing on. Start puts the desktop back.
 *
 * He is the cartridge's own drawing, moves by the cartridge's own numbers
 * and makes the cartridge's own noises. VECTOR.MDD is every pose the game
 * drew him in and every write its sound driver made for him, recorded
 * off the running game by tools/vectorman-tools/extract.py, and the
 * physics below were measured off the same runs: a walk that gains
 * 32/256 of a pixel a frame up to four, a turn that sheds 128, a jump
 * that starts at -2080 and loses 96 a frame. Nothing here was eyeballed.
 *
 * What it does not have is the game's skeleton. The cartridge works his
 * pose out every frame from joint angles; this plays back the poses it
 * worked out. Where the game eases from one movement into another -- out
 * of standing into the first stride, out of aiming back into the run --
 * the skeleton is what does the easing, and here each of his sprites is
 * moved instead to the nearest one of the same part in the new pose
 * (blend, below). It is the same picture getting there the same way.
 *
 * The desktop is read once, at the start, into a bitmap of which pixels
 * are not the background. Nothing can draw on it while he runs -- the
 * servant's pump is stopped and the sub CPU is held -- so there is no
 * point reading it again, and every question he asks of the ground is
 * then a question about workspace memory rather than a trip through the
 * Mega CD's window.
 */
typedef unsigned char UBYTE;
typedef signed char BYTE;
typedef unsigned short UWORD;
typedef short WORD;
typedef unsigned long ULONG;
typedef long LONG;

/* ---- the machine -------------------------------------------------------- */
#define VDP_DATA    (*(volatile UWORD *)0xC00000)
#define VDP_CTRL    (*(volatile UWORD *)0xC00004)
#define VDP_CTRL_L  (*(volatile ULONG *)0xC00004)
#define GA_MEMMODE  (*(volatile UBYTE *)0xA12003)
#define PAD_DATA    (*(volatile UBYTE *)0xA10003)
#define PAD_CTRL    (*(volatile UBYTE *)0xA10009)
#define Z80_RAM     ((volatile UBYTE *)0xA00000)
#define Z80_BUSREQ  (*(volatile UWORD *)0xA11100)
#define Z80_RESET   (*(volatile UWORD *)0xA11200)
#define YM_A0       (*(volatile UBYTE *)0xA04000)
#define YM_D0       (*(volatile UBYTE *)0xA04001)
#define YM_A1       (*(volatile UBYTE *)0xA04002)
#define YM_D1       (*(volatile UBYTE *)0xA04003)

#define VRAM_W(a)   (0x40000000UL | (((ULONG)(a) & 0x3FFFu) << 16) | ((ULONG)(a) >> 14))
#define CRAM_W(a)   (0xC0000000UL | ((ULONG)(a) << 16))

#define SAT         0xF800u     /* the servant's sprite table */
#define SCREEN_YOFF 12          /* the servant centres 200 lines in 224 */
#define ROMDISK     0x080000UL  /* R: in the cartridge, iofw/hw.h */

/* The pad, in the same bits as the servant's own. */
#define P_UP    0x01
#define P_DOWN  0x02
#define P_LEFT  0x04
#define P_RIGHT 0x08
#define P_B     0x10
#define P_C     0x20
#define P_A     0x40
#define P_START 0x80

/* ---- VECTOR.MDD ---------------------------------------------------------
 * tools/vectorman-tools/extract.py writes it and says what every byte is.
 * Each animation is there twice, facing left and then facing right, in
 * this order -- the same list as ANIMS in the extractor. */
enum {
    A_LOOP = 0, A_FIDGET1 = 2, A_FIDGET2 = 4, A_FIDGET3 = 6,
    A_WALK = 8,                 /* eight segments of the cycle: 8 + 2s */
    A_STOP = 24, A_JUMP = 26, A_LAND = 28, A_BOOST = 30, A_SHOOT = 32,
    A_DOWN_IN = 34, A_DOWN_OUT = 36,
    A_AIM = 38,                 /* the aiming walk, eight segments too */
    A_AIRSHOT = 54, A_TURN = 56,
    A_SHOTFX = 58, A_AIRFX = 60,/* effects only: a flash and a bolt */
    A_COUNT = 62
};
#define H_TOTAL     8
#define H_NRUNS     12
#define H_NANIMS    14
#define H_RUNS      16
#define H_GROUPS    20
#define H_ANIMS     24
#define H_NGROUPS   28
#define H_FOOT      30
#define H_FEET_L    32
#define H_FEET_R    33
#define H_JUMPFALL  34
#define H_PAL       36
#define H_PALFX     68
#define H_CYCLE     100
#define H_AIMF      356         /* frames a running shot aims for */
#define H_NGLOW     358         /* frames of the glow a shot lights him with */
#define H_GLOW      360
#define H_BOLT      364         /* the standing shot's bolt against SHOTFX's */
#define H_SFX       372
#define H_SIZE      384

/* ---- his numbers, measured off the cartridge (docs/vectorman.md) ------- */
#define WALK_ACC    32          /* 1/256 px a frame, a frame */
#define WALK_MAX    1024
#define TURN        128         /* pressing against his facing or his way */
#define BRAKE       128         /* letting go */
#define AIR_ACC     8
#define JUMP_V      (-2080)
#define BOOST_V     (-2048)
#define GRAVITY     96
#define FALL_MAX    3072        /* never seen in the game; a ceiling for the
                                 * ground search, which reads a row a pixel */
#define FAST        896         /* a walk segment begun at this speed or more
                                 * runs at two poses a frame */
#define SKID        4           /* frames the walk holds still on a turnaround */
#define LAND_HOLD   9           /* frames of landing he keeps his speed through */
#define SURF_MIN    16          /* the narrowest edge he comes down on */
#define DROP        24          /* down with C passes this many rows */
#define AIM_EASE    8           /* the last frames of an aim ease back to the walk */
#define AIM_JUMP    4           /* the stride jumps ahead this far on a shot */
#define TAP         8           /* a press the other way let go of sooner is a turn */

enum { S_IDLE, S_WALK, S_STOP, S_AIR, S_LAND, S_SHOOT, S_DOWN, S_TURN };

struct spr { WORD dx, dy; UBYTE g, fl; };
#define NPART       16          /* kinds of picture: a ball, a hand, a foot... */

/* An animation playing: his own, or one of the effects flying about. */
struct player {
    const UBYTE *next;          /* the record after the current one */
    const UBYTE *front, *back;  /* this record's effects, in the blob */
    UWORD id, ai, nrec, fr;     /* which, which record, how many, frame */
    UBYTE left;                 /* frames the current record has to run */
    UBYTE nfront, nback, nbody;
};

/* A shot's flash and bolt. The flash rides with him; the bolt flies from
 * where it was fired, whatever he does after. */
struct fx {
    struct player pl;
    WORD x, y;                  /* where it was fired */
    UBYTE on, front, wait;
};
#define NFX 4

/* Everything that changes, in the servant's working space. */
struct vm {
    UWORD pix[200 * 20];        /* the desktop, a bit a pixel, set = drawn */
    struct spr body[32];        /* his sprites as the last record left them */
    struct spr shown[32];       /* ...and as they were last drawn */
    struct spr from[32];        /* where a blend started, sorted by part */
    UBYTE fstart[NPART + 1];    /* where each part's sprites begin in it */
    UWORD sat[80 * 4];
    struct player me;
    struct fx fx[NFX];

    LONG x, y;                  /* his feet, in 1/256 px */
    WORD vx, vy;
    UBYTE facing;               /* 0 left, 1 right */
    UBYTE state, boosted, k, step, skid, held, aim, tap;
    UBYTE nshown, nfrom, bw, bstep;
    WORD ignore;                /* rows above this are not landed on */
    UWORD loops, due, fidget, seed, cyc, nsat, glow, nglow;
    UBYTE pad, prev, sound;
    UWORD landlen;              /* the landing sample, as much as the Z80 holds */

    /* the two FM effects playing, frame by frame */
    const UBYTE *sfx[2];
    UWORD sfxleft[2];
};
_Static_assert(sizeof(struct vm) <= 11264, "head.S WORKSPACE is too small");

/* The statics, each initialised so that it is .data: there is no .bss
 * in a payload. */
static struct vm *V = 0;
static const UBYTE *blob = 0;
static const UBYTE *groups = 0;         /* the pictures: tile, size, part */

void vm_main(const UWORD *screen, struct vm *work, ULONG mode,
             const UBYTE *bulk, ULONG blen);

/* gcc may call these for a struct copy or a cleared array, freestanding
 * or not. */
void *memset(void *d, int c, unsigned long n)
{
    UBYTE *p = d;
    while (n--) *p++ = (UBYTE)c;
    return d;
}
void *memcpy(void *d, const void *s, unsigned long n)
{
    UBYTE *p = d;
    const UBYTE *q = s;
    while (n--) *p++ = *q++;
    return d;
}

/* A run of sprites, three words each. memcpy above goes a byte at a time,
 * at seventy-odd cycles a byte, and a pose is a hundred and twenty-six. */
static void spr_copy(struct spr *d, const struct spr *s, UBYTE n)
{
    UWORD *p = (UWORD *)d;
    const UWORD *q = (const UWORD *)s;
    UWORD w = (UWORD)(3 * n);
    while (w--)
        *p++ = *q++;
}

/* The few small things done hundreds of times a frame are inlined: at
 * -Os gcc would rather call them, and on a 68000 the call is most of it. */
#define HOT static inline __attribute__((always_inline))

HOT UWORD rd16(const UBYTE *p) { return (UWORD)((p[0] << 8) | p[1]); }
static ULONG rd32(const UBYTE *p) { return ((ULONG)rd16(p) << 16) | rd16(p + 2); }
static WORD hdr16(UWORD o) { return (WORD)rd16(blob + o); }

/* ---- the window ---------------------------------------------------------
 * Bits 7-6 of the gate array's memory mode pick which 128K of PRG RAM the
 * window shows; the rest is word RAM's and must be left alone. */
static void bank(UWORD b)
{
    GA_MEMMODE = (UBYTE)((GA_MEMMODE & 0x3F) | (b << 6));
}

/* ---- finding VECTOR.MDD -------------------------------------------------
 * Handed over, if NATIVE's loader put it in PRG RAM -- it was on C: or a
 * disc. On a cartridge it may instead be on R:, which is a FAT image in
 * the cartridge ROM itself, readable from here as it stands: the loader
 * leaves an R: file where it is (progs/payload.h), and the file is found
 * here by its directory entry. tools/mkfat.py lays every file down in one
 * run of clusters, so the entry's first cluster is the whole of it. */
static int good(const UBYTE *b, ULONG len)
{
    return b[0] == 'V' && b[1] == 'M' && b[2] == 'D' && b[3] == 'D'
        && b[4] == 0 && b[5] == 2 && rd32(b + H_TOTAL) <= len;
}

static const UBYTE *rom_find(void)
{
    const UBYTE *bs = (const UBYTE *)ROMDISK;
    const UBYTE *root, *e;
    UWORD spc, res, nf, nroot, spf, i, cl;
    static const char name[11] = "VECTOR  MDD";

    if (bs[11] != 0x00 || bs[12] != 0x02)           /* 512 a sector */
        return 0;
    spc = bs[13];
    res = (UWORD)(bs[14] | (bs[15] << 8));
    nf = bs[16];
    nroot = (UWORD)(bs[17] | (bs[18] << 8));
    spf = (UWORD)(bs[22] | (bs[23] << 8));
    root = bs + ((ULONG)(UWORD)(res + nf * spf) << 9);
    for (i = 0, e = root; i < nroot; i++, e += 32) {
        UWORD j;
        if (e[0] == 0)
            break;
        for (j = 0; j < 11 && e[j] == (UBYTE)name[j]; j++)
            ;
        if (j < 11)
            continue;
        {
            ULONG len = (ULONG)e[31] << 24 | (ULONG)e[30] << 16
                      | (ULONG)e[29] << 8 | e[28];
            cl = (UWORD)(e[26] | (e[27] << 8));
            if (cl < 2)
                return 0;
            e = root + ((ULONG)nroot << 5) + ((ULONG)(UWORD)((cl - 2) * spc) << 9);
            return good(e, len) ? e : 0;
        }
    }
    return 0;
}

/* ---- the desktop as ground ----------------------------------------------
 * The ST screen is four bitplanes interleaved a word at a time; a pixel
 * is drawn if any plane has it, which is "not colour 0". The rule for a
 * surface is Sonic's (payload/sonic/engine.S, sonic_solid): three drawn
 * pixels in a row. The desktop's paper is a checkerboard, which never
 * has two in a row, so it is not ground; every line is. */
static void read_desktop(const UWORD *s)
{
    UWORD *d = V->pix;
    UWORD n;
    for (n = 0; n < 4000; n++, s += 4)
        *d++ = s[0] | s[1] | s[2] | s[3];
}

HOT int px(WORD x, WORD y)
{
    if ((UWORD)x >= 320 || y < 0)
        return 0;
    if (y >= 200)
        return 1;                       /* below the screen is the floor */
    return (V->pix[(UWORD)y * 20 + ((UWORD)x >> 4)] << (x & 15)) & 0x8000;
}

HOT int solid(WORD x, WORD y)
{
    return px(x - 1, y) && px(x, y) && px(x + 1, y);
}

/* An edge he will come down on: SURF_MIN wide where his foot is. On line
 * art three pixels is a hairline -- a label, the pointer's arrow, the
 * printer's paper tray -- and he would stand in the air on it. Once he
 * is standing three is enough, so that walking off the end of a ledge
 * is the end of the ledge. Sonic learnt the same lesson the same way. */
static int wide(WORD x, WORD y)
{
    WORD a, b;
    if (y >= 200)
        return 1;
    if (!px(x, y))
        return 0;
    for (a = x; a > x - SURF_MIN && px(a - 1, y); a--)
        ;
    for (b = x; b < a + SURF_MIN - 1 && px(b + 1, y); b++)
        ;
    return b - a + 1 >= SURF_MIN;
}

/* Under each foot and between them; and how far out his feet reach,
 * which is where the screen's edges stop him. From the file's header. */
static WORD foot_l = 0, foot_r = 0, edge_l = 0, edge_r = 0;

static int stands(WORD x, WORD y)
{
    return solid(x + foot_l, y) || solid(x, y) || solid(x + foot_r, y);
}

static int lands(WORD x, WORD y)
{
    return wide(x + foot_l, y) || wide(x, y) || wide(x + foot_r, y);
}

/* ---- playing the poses ---------------------------------------------------- */
HOT void spr_read(struct spr *s, const UBYTE *p)
{
    s->dx = (WORD)rd16(p);
    s->dy = (WORD)rd16(p + 2);
    s->g = p[4];
    s->fl = p[5];
}

/* One record: his sprites, whole or as changes, then the effects in
 * front of him and behind him, which stay where they are in the blob.
 * An effects-only animation has none of his, and only his own player
 * keeps any. */
static const UBYTE *rec(struct player *pl, const UBYTE *p)
{
    UBYTE n, i, c;
    struct spr *b = V->body;
    pl->left = p[0];
    n = p[2];
    pl->nbody = n;
    if (p[1] == 0) {
        p += 3;
        for (i = n; i; i--, p += 6)
            spr_read(b++, p);
    } else {
        const UBYTE *mask = p + 3;
        UBYTE bits = 0;
        p = mask + ((n + 7) >> 3);
        for (i = 0; i < n; i++, b++, bits <<= 1) {
            UBYTE d;
            if (!(i & 7))
                bits = *mask++;
            if (!(bits & 0x80))
                continue;
            d = *p++;
            if (d == 0x88)
                continue;               /* replaced below */
            b->dx += (WORD)(((d >> 4) ^ 8) - 8);
            b->dy += (WORD)(((d & 15) ^ 8) - 8);
        }
        b = V->body;
        for (c = *p++; c; c--, p += 7)
            spr_read(&b[p[0]], p + 1);
    }
    pl->nfront = *p;
    pl->front = p + 1;
    p += 1 + 6 * (UWORD)*p;
    pl->nback = *p;
    pl->back = p + 1;
    p += 1 + 6 * (UWORD)*p;
    return p;
}

static void play_on(struct player *pl, UWORD id)
{
    const UBYTE *t = blob + rd32(blob + H_ANIMS) + 12 * id;
    pl->id = id;
    pl->nrec = rd16(t + 4);
    pl->ai = 0;
    pl->fr = 0;
    pl->next = rec(pl, blob + rd32(t));
}

/* A frame on. Nonzero, and nothing moved, if the animation was already
 * on its last frame: what follows is the caller's to decide. */
static int tick_on(struct player *pl)
{
    if (pl->left > 1) {
        pl->left--;
        pl->fr++;
        return 0;
    }
    if (pl->ai + 1 >= pl->nrec)
        return 1;
    pl->ai++;
    pl->fr++;
    pl->next = rec(pl, pl->next);
    return 0;
}

#define play(id)    play_on(&V->me, (id))
#define tick()      tick_on(&V->me)

/* To a frame of an animation, decoding up to `most` records past its
 * first; called again, it carries on from where it got to. */
static void seek_some(UWORD id, UWORD frame, UWORD most)
{
    if (V->me.id != id || V->me.fr > frame)
        play(id);
    while (V->me.fr < frame && most-- && !tick())
        ;
}
#define seek(id, frame)     seek_some((id), (frame), 0xFFFF)

static UBYTE anim_byte(UWORD id, UWORD o)
{
    return blob[rd32(blob + H_ANIMS) + 12 * id + o];
}
#define ENTRY(id)   anim_byte(id, 8)    /* the loop frame a fidget leaves at */
#define EXIT(id)    anim_byte(id, 9)    /* the loop frame it hands back to */

/* ---- easing from one pose into another -----------------------------------
 * Over the next few frames each of his sprites travels from where it was
 * drawn to where the new pose puts it -- to the nearest sprite of the same
 * part, a hand to a hand and a ball to a ball, which the file says of
 * every picture. His parts are depth-sorted afresh every frame, so the
 * order of the list says nothing about which sprite is which; the part
 * and the distance do. Halfway, each takes the new pose's picture. */
HOT UBYTE part(UBYTE g)
{
    return (UBYTE)(groups[4 * (UWORD)g + 3] & (NPART - 1));
}

/* The pose he was last drawn in becomes where the blend starts from,
 * sorted by part so that each sprite of the new pose need only look
 * among its own kind. */
static void blend(UBYTE frames)
{
    UBYTE n = V->nshown, i, k;
    UBYTE at[NPART], kind[32];

    for (k = 0; k < NPART; k++)
        at[k] = 0;
    for (i = 0; i < n; i++) {
        kind[i] = part(V->shown[i].g);
        at[kind[i]]++;
    }
    for (k = 0, i = 0; k < NPART; k++) {
        UBYTE c = at[k];
        V->fstart[k] = at[k] = i;
        i = (UBYTE)(i + c);
    }
    V->fstart[NPART] = n;
    for (i = 0; i < n; i++)
        V->from[at[kind[i]]++] = V->shown[i];
    V->nfrom = n;
    V->bw = 0;
    V->bstep = frames >= 8 ? 1 : frames >= 4 ? 2 : frames == 3 ? 3 : frames == 2 ? 4 : 8;
}

/* Each frame of a blend, every sprite of the pose he is going to finds
 * the nearest one of its kind in the pose he came from that no other has
 * taken. His twenty-one are the same eleven kinds in every pose, seven
 * of them balls, so that is seventy-one comparisons rather than four
 * hundred and forty-one -- which mattered: done the long way it took
 * longer than a frame, and everything, the bolt in the air included, ran
 * at half speed while he blended. */
static void compose(void)
{
    UBYTE i, j, n = V->me.nbody;
    UBYTE taken[32];
    const struct spr *from = V->from;
    WORD w;

    if (V->bw < 8)
        V->bw = (UBYTE)(V->bw + V->bstep);
    if (V->bw >= 8 || !V->nfrom) {
        spr_copy(V->shown, V->body, n);
        V->nshown = n;
        return;
    }
    w = V->bw;
    for (j = 0; j < V->nfrom; j++)
        taken[j] = 0;
    for (i = 0; i < n; i++) {
        const struct spr *t = &V->body[i];
        struct spr *o = &V->shown[i];
        UBYTE kind = part(t->g), best = 0xFF, end;
        WORD bd = 0x7FFF, tx = t->dx, ty = t->dy;
        end = V->fstart[kind + 1];
        for (j = V->fstart[kind]; j < end; j++) {
            WORD d, e;
            if (taken[j])
                continue;
            d = from[j].dx - tx;
            if (d < 0) d = -d;
            e = from[j].dy - ty;
            if (e < 0) e = -e;
            d += e;
            if (d < bd) {
                bd = d;
                best = j;
            }
        }
        if (best == 0xFF) {
            *o = *t;
            continue;
        }
        taken[best] = 1;
        {
            const struct spr *s = &from[best];
            o->dx = (WORD)(s->dx + (((WORD)(tx - s->dx) * w) >> 3));
            o->dy = (WORD)(s->dy + (((WORD)(ty - s->dy) * w) >> 3));
            o->g = w < 4 ? s->g : t->g;
            o->fl = w < 4 ? s->fl : t->fl;
        }
    }
    V->nshown = n;
}

/* ---- idling --------------------------------------------------------------
 * He breathes, a 32-frame loop, and every few seconds he does one of the
 * three things the cartridge has him do when nobody is playing. They
 * leave the loop at one frame of it and come back at another, and so do
 * stopping, landing, shooting and turning -- the extractor wrote down
 * where. */
static UWORD rnd(void)
{
    V->seed = (UWORD)(V->seed * 25173u + 13849u);
    return V->seed;
}

/* Six to thirteen breaths, then one of the three: the short one half the
 * time, the others a quarter each. */
static void plan(void)
{
    UWORD r = rnd();
    V->loops = 0;
    V->due = 6 + (r & 7);
    r &= 0x300;
    V->fidget = r == 0 ? A_FIDGET3 : r == 0x100 ? A_FIDGET2 : A_FIDGET1;
}

static void idle_at(UWORD phase)
{
    V->state = S_IDLE;
    plan();
    seek(A_LOOP + V->facing, phase);
}

static void idle(void)
{
    if ((V->me.id & ~1u) == A_LOOP) {
        UWORD f = V->fidget + V->facing;
        if (V->loops >= V->due && V->me.fr + 1 == ENTRY(f)) {
            play(f);
            plan();
            return;
        }
        if (tick()) {
            play(A_LOOP + V->facing);
            V->loops++;
        }
    } else if (tick()) {
        idle_at(EXIT(V->me.id));
    }
}

/* ---- sound -----------------------------------------------------------------
 * Three effects. The shot and the boost are FM: the game's driver wrote
 * a voice into a channel and then a frequency a frame, and the extractor
 * kept every write, so playing them is writing them again, a frame at a
 * time, into the YM2612 -- all but the voices, which it set aside to be
 * loaded once, at the start, each into a channel of its own. The landing is a sample, 8860 bytes a second
 * through the DAC -- too fast for this side to keep up with between
 * frames, so the Z80 plays it, from its own RAM, with the twenty-odd
 * instructions below. They are this project's, not the game's.
 *
 * The 68000 reaches the YM2612 only with the Z80's bus in its hand, and
 * taking the bus stops the Z80, possibly between its writing the DAC's
 * register number and its writing the sample. So the 68000 leaves that
 * register number behind it every time it lets go, and the Z80's next
 * write lands where it meant it to. */
static const UBYTE z80_player[] = {
    0xF3,                   /* 0000 di */
    0x31, 0x00, 0x20,       /* 0001 ld sp,2000h */
    0x21, 0x80, 0x00,       /* 0004 ld hl,0080h       the mailbox */
    0x7E,                   /* 0007 ld a,(hl)          idle: */
    0xB7,                   /* 0008 or a */
    0x28, 0xFC,             /* 0009 jr z,0007h */
    0x36, 0x00,             /* 000B ld (hl),0          take the order */
    0xED, 0x5B, 0x81, 0x00, /* 000D ld de,(0081h)      where */
    0xED, 0x4B, 0x83, 0x00, /* 0011 ld bc,(0083h)      how many */
    0x3E, 0x2B,             /* 0015 ld a,2Bh */
    0x32, 0x00, 0x40,       /* 0017 ld (4000h),a */
    0x3E, 0x80,             /* 001A ld a,80h */
    0x32, 0x01, 0x40,       /* 001C ld (4001h),a       DAC on */
    0x3E, 0x2A,             /* 001F ld a,2Ah           each sample: */
    0x32, 0x00, 0x40,       /* 0021 ld (4000h),a */
    0x1A,                   /* 0024 ld a,(de) */
    0x32, 0x01, 0x40,       /* 0025 ld (4001h),a */
    0x13,                   /* 0028 inc de */
    0x0B,                   /* 0029 dec bc */
    0x7E,                   /* 002A ld a,(hl) */
    0xB7,                   /* 002B or a */
    0x20, 0xDD,             /* 002C jr nz,000Bh        a new order wins */
    0x3E, 0x13,             /* 002E ld a,19 */
    0x3D,                   /* 0030 dec a */
    0x20, 0xFD,             /* 0031 jr nz,0030h */
    0x00,                   /* 0033 nop                400 T a sample */
    0x78,                   /* 0034 ld a,b */
    0xB1,                   /* 0035 or c */
    0x20, 0xE7,             /* 0036 jr nz,001Fh */
    0x3E, 0x2B,             /* 0038 ld a,2Bh */
    0x32, 0x00, 0x40,       /* 003A ld (4000h),a */
    0xAF,                   /* 003D xor a */
    0x32, 0x01, 0x40,       /* 003E ld (4001h),a       DAC off */
    0x18, 0xC4,             /* 0041 jr 0007h */
};
#define Z80_MAIL    0x80
#define Z80_SAMPLE  0x100

static void z80_take(void)
{
    UWORD n = 0xFFFF;
    Z80_BUSREQ = 0x0100;
    while ((Z80_BUSREQ & 0x0100) && --n)
        ;
}

static void z80_give(void)
{
    Z80_BUSREQ = 0;
}

/* Both waits are counted, for the reason payload/sonic/shim.S gives:
 * a poll for a flag that cannot stay set is a poll nobody has seen fail. */
static void ym_wait(void)
{
    UWORD n = 0x3FF;
    while ((YM_A0 & 0x80) && --n)
        ;
}

static void ym(UBYTE port, UBYTE reg, UBYTE val)
{
    ym_wait();
    if (port) {
        YM_A1 = reg;
        ym_wait();
        YM_D1 = val;
    } else {
        YM_A0 = reg;
        ym_wait();
        YM_D0 = val;
    }
}

static const UBYTE *sfx_entry(UWORD n)
{
    return blob + rd32(blob + H_SFX) + 8 * n;
}

static void sound_open(void)
{
    const UBYTE *e = sfx_entry(2);
    const UBYTE *land = blob + rd32(e), *w;
    UWORD nland = rd16(e + 4), i;

    if (nland > 0x2000 - Z80_SAMPLE)
        nland = 0x2000 - Z80_SAMPLE;
    Z80_BUSREQ = 0x0100;
    Z80_RESET = 0x0100;
    z80_take();
    for (i = 0; i < sizeof z80_player; i++)
        Z80_RAM[i] = z80_player[i];
    for (; i < Z80_SAMPLE; i++)
        Z80_RAM[i] = 0;
    for (i = 0; i < nland; i++)
        Z80_RAM[Z80_SAMPLE + i] = land[i];
    V->landlen = nland;
    V->sfxleft[0] = V->sfxleft[1] = 0;
    Z80_RESET = 0;                      /* which resets the YM2612 too */
    for (i = 0; i < 32; i++)
        __asm__ volatile("nop");
    z80_give();
    Z80_RESET = 0x0100;                 /* and the Z80 starts at 0, where it
                                         * waits for an order */
    /* What the reset left in the chip: everything off, and every channel
     * to both speakers. */
    z80_take();
    ym(0, 0x22, 0x00);                  /* LFO off */
    ym(0, 0x27, 0x00);                  /* timers off, channel 3 normal */
    ym(0, 0x2B, 0x00);                  /* DAC off */
    for (i = 0; i < 7; i++)
        if (i != 3)
            ym(0, 0x28, (UBYTE)i);      /* every channel keyed off */
    for (i = 0; i < 3; i++) {
        ym(0, (UBYTE)(0xB4 + i), 0xC0);
        ym(1, (UBYTE)(0xB4 + i), 0xC0);
    }
    /* and the effects' voices, each in a channel of its own, so that
     * playing one is a frequency and a key a frame. */
    e = sfx_entry(3);
    for (w = blob + rd32(e), i = rd16(e + 4); i; i--, w += 3)
        ym(w[0], w[1], w[2]);
    ym_wait();
    YM_A0 = 0x2A;
    z80_give();
    V->sound = 1;
}

/* Out: the Z80 stopped and its bus kept, as Sonic's payload leaves it,
 * and every channel silent. A Z80 held in reset will not answer a bus
 * request, so it is left out of reset; see sn_sound_close in
 * payload/sonic/main.S for the night that was learnt. */
static void sound_close(void)
{
    UBYTE i;
    if (!V->sound)
        return;
    z80_take();
    ym(0, 0x2B, 0x00);
    for (i = 0; i < 7; i++)
        if (i != 3)
            ym(0, 0x28, i);
}

static void sfx_fm(UWORD n)
{
    const UBYTE *e = sfx_entry(n);
    V->sfx[n] = blob + rd32(e);
    V->sfxleft[n] = rd16(e + 4);
}

static void sfx_land(void)
{
    UWORD n = V->landlen;
    if (!V->sound)
        return;
    z80_take();
    Z80_RAM[Z80_MAIL + 1] = (UBYTE)Z80_SAMPLE;
    Z80_RAM[Z80_MAIL + 2] = (UBYTE)(Z80_SAMPLE >> 8);
    Z80_RAM[Z80_MAIL + 3] = (UBYTE)n;
    Z80_RAM[Z80_MAIL + 4] = (UBYTE)(n >> 8);
    Z80_RAM[Z80_MAIL] = 1;
    z80_give();
}

/* This frame's writes for each FM effect that is playing, in the
 * vertical blank. */
static void sfx_frame(void)
{
    UBYTE n, c;
    if (!V->sound || !(V->sfxleft[0] | V->sfxleft[1]))
        return;
    z80_take();
    for (n = 0; n < 2; n++) {
        const UBYTE *p = V->sfx[n];
        if (!V->sfxleft[n])
            continue;
        for (c = *p++; c; c--, p += 3)
            ym(p[0], p[1], p[2]);
        V->sfx[n] = p;
        V->sfxleft[n]--;
    }
    ym_wait();
    YM_A0 = 0x2A;                       /* the DAC's, for the Z80 */
    z80_give();
}

/* ---- shots -----------------------------------------------------------------
 * A shot is three things: his pose, which is the animation he is in; a
 * flash and a bolt, which fly as an effect of their own; and the glow,
 * his palette lit up white-blue for half a second, as the game does it. */
static void fx_fire(UWORD id, WORD dx, WORD dy, BYTE dt, UBYTE front)
{
    struct fx *f = &V->fx[0];
    UBYTE i;
    for (i = 0; i < NFX; i++)
        if (!V->fx[i].on) {
            f = &V->fx[i];
            break;
        }
    play_on(&f->pl, id);
    f->x = (WORD)(V->x >> 8) + dx;
    f->y = (WORD)(V->y >> 8) - hdr16(H_FOOT) + dy;
    f->on = 1;
    f->front = front;
    f->wait = dt > 0 ? (UBYTE)dt : 0;
    while (dt++ < 0)
        tick_on(&f->pl);
}

static void fx_step(void)
{
    UBYTE i;
    for (i = 0; i < NFX; i++) {
        struct fx *f = &V->fx[i];
        if (!f->on)
            continue;
        if (f->wait)
            f->wait--;
        else if (tick_on(&f->pl))
            f->on = 0;
    }
}

static void lit(void)
{
    V->glow = 0;
    sfx_fm(0);
}

/* Standing: the SHOOT animation has his flash and the casing; the bolt is
 * the running shot's, flown from where the standing one's was. */
static void shoot_standing(void)
{
    const UBYTE *b = blob + H_BOLT + 3 * V->facing;
    V->state = S_SHOOT;
    play(A_SHOOT + V->facing);
    blend(2);
    fx_fire(A_SHOTFX + V->facing, (BYTE)b[0], (BYTE)b[1], (BYTE)b[2], 0);
    lit();
}

/* ---- the walk ---------------------------------------------------------------
 * The game's cycle is 64 poses in eight segments. A segment runs at one
 * pose a frame, or at two if he is near top speed when it begins -- so
 * the same 64 poses serve every speed, which is what the game does. A
 * shot on the run jumps the cycle four poses on and draws it from the
 * aiming cycle for the length of the aim, the last frames of which ease
 * back into the walk. */
static void walk_pose(void)
{
    UWORD base = V->aim > AIM_EASE ? A_AIM : A_WALK;
    /* Going over from the walk to the aim or back means decoding the new
     * cycle from the start of its segment, up to seven records, and in
     * the frame of a shot that was a third of the frame. Three at a time
     * is enough: it is blending there anyway, and the pose catches up in
     * a frame or two. */
    seek_some(base + 2 * (V->k >> 3) + V->facing, V->k & 7, 3);
}

static void walk_start(void)
{
    V->state = S_WALK;
    V->k = 0;
    V->step = 1;
    V->skid = 0;
    V->aim = 0;
    walk_pose();
    blend(8);
}

static void walk_advance(void)
{
    if (V->skid) {
        V->skid--;
    } else {
        V->k = (UBYTE)((V->k + V->step) & 63);
        if (!(V->k & 7))
            V->step = (V->vx >= FAST || V->vx <= -FAST) ? 2 : 1;
    }
    if (V->aim && --V->aim == AIM_EASE)
        blend(AIM_EASE);
    walk_pose();
}

static void shoot_running(void)
{
    if (!V->aim)
        V->k = (UBYTE)((V->k + AIM_JUMP) & 63);
    V->aim = (UBYTE)hdr16(H_AIMF);
    walk_pose();
    blend(2);
    fx_fire(A_SHOTFX + V->facing, 0, 0, 0, 1);
    lit();
}

/* ---- moving him ------------------------------------------------------------- */
static WORD dir_of(UBYTE pad)
{
    return (pad & P_RIGHT) ? 1 : (pad & P_LEFT) ? -1 : 0;
}

/* The ground speed rule, from the game frame by frame: pressing the way
 * he faces and moves gains 32; pressing against either -- a turn from a
 * standstill, a reversal at a run -- gains 128 and turns him at once;
 * letting go brakes by 128. Returns nonzero if he turned. */
static int ground_speed(WORD d)
{
    int turned = 0;
    if (d) {
        UBYTE f = d > 0;
        if (f != V->facing || (d > 0 ? V->vx < 0 : V->vx > 0)) {
            turned = f != V->facing;
            V->facing = f;
            V->vx += d * TURN;
        } else {
            V->vx += d * WALK_ACC;
        }
        if (V->vx > WALK_MAX) V->vx = WALK_MAX;
        if (V->vx < -WALK_MAX) V->vx = -WALK_MAX;
    } else if (V->vx > 0) {
        V->vx = V->vx > BRAKE ? V->vx - BRAKE : 0;
    } else if (V->vx < 0) {
        V->vx = V->vx < -BRAKE ? V->vx + BRAKE : 0;
    }
    return turned;
}

/* Left and right are the screen's edges, measured to his feet. Nonzero
 * if he is against one and pushing. */
static int move_x(void)
{
    WORD lo = edge_l, hi = edge_r;
    WORD ax;
    V->x += V->vx;
    ax = (WORD)(V->x >> 8);
    if (ax < lo) { V->x = (LONG)lo << 8; V->vx = 0; return 1; }
    if (ax > hi) { V->x = (LONG)hi << 8; V->vx = 0; return 1; }
    return 0;
}

static int blocked(WORD d)
{
    WORD ax = (WORD)(V->x >> 8);
    return (d < 0 && ax <= edge_l) || (d > 0 && ax >= edge_r);
}

/* Still on something? Small steps are followed, up by two and down by
 * four; anything more and he walks through it or off it. Edges are one
 * way, like the level's girders: he stands on top of them and nothing
 * else about them is solid. */
static int follow_ground(void)
{
    WORD ax = (WORD)(V->x >> 8), ay = (WORD)(V->y >> 8), r;
    if (stands(ax, ay)) {
        if (stands(ax, ay - 1) && !stands(ax, ay - 2))
            ay -= 1;
        else if (stands(ax, ay - 1) && stands(ax, ay - 2) && !stands(ax, ay - 3))
            ay -= 2;
        V->y = (LONG)ay << 8;
        return 1;
    }
    for (r = ay + 1; r <= ay + 4; r++)
        if (stands(ax, r)) {
            V->y = (LONG)r << 8;
            return 1;
        }
    return 0;
}

static void jump(void)
{
    V->state = S_AIR;
    V->vy = JUMP_V;
    V->boosted = 0;
    V->ignore = -32767;
    V->aim = 0;
    play(A_JUMP + V->facing);
    blend(2);
}

static void fall(WORD ignore_above)
{
    V->state = S_AIR;
    V->vy = 0;
    V->boosted = 0;
    V->ignore = ignore_above;
    V->aim = 0;
    seek(A_JUMP + V->facing, (UWORD)hdr16(H_JUMPFALL));
}

/* In the air: gravity, a little steering, and the ground looked for on
 * every row he passes through on the way down -- at twelve pixels a
 * frame a single test would step over a one-pixel edge. */
static void air(WORD d, UBYTE hit)
{
    LONG ny;
    WORD ax, r, r0, r1;

    if (d) {
        UBYTE f = d > 0;
        if (f != V->facing) {           /* he turns in the air, mid-pose */
            V->facing = f;
            seek((V->me.id & ~1u) + f, V->me.fr);
        }
        V->vx += d * AIR_ACC;
        if (V->vx > WALK_MAX) V->vx = WALK_MAX;
        if (V->vx < -WALK_MAX) V->vx = -WALK_MAX;
    }
    if ((hit & P_C) && !V->boosted) {
        V->boosted = 1;
        V->vy = BOOST_V;
        play(A_BOOST + V->facing);
        sfx_fm(1);
    } else if (hit & (P_A | P_B)) {
        play(A_AIRSHOT + V->facing);
        blend(3);
        fx_fire(A_AIRFX + V->facing, 0, 0, 0, 1);
        lit();
    }
    move_x();
    V->vy += GRAVITY;
    if (V->vy > FALL_MAX)
        V->vy = FALL_MAX;
    ny = V->y + V->vy;
    if (V->vy > 0) {
        ax = (WORD)(V->x >> 8);
        r0 = (WORD)(V->y >> 8) + 1;
        r1 = (WORD)(ny >> 8);
        for (r = r0; r <= r1; r++) {
            if (r <= V->ignore && r < 200)
                continue;
            if (lands(ax, r)) {
                V->y = (LONG)r << 8;
                V->vy = 0;
                V->state = S_LAND;
                V->held = LAND_HOLD;
                play(A_LAND + V->facing);
                blend(2);
                sfx_land();
                return;
            }
        }
    }
    V->y = ny;
    tick();                             /* the last pose holds */
}

/* From standing, a press: the way he faces is a walk; the other way is a
 * walk too, turned -- unless it is let go of within TAP frames, which
 * makes it the game's turn on the spot. */
static void start_moving(WORD d)
{
    UBYTE turning = (d > 0) != V->facing;
    ground_speed(d);
    move_x();
    walk_start();
    V->tap = turning ? TAP : 0;
}

/* ---- one frame -------------------------------------------------------------- */
static void step(void)
{
    UBYTE pad = V->pad, hit = (UBYTE)(pad & ~V->prev);
    WORD d = dir_of(pad);

    if (d && blocked(d) && V->state != S_AIR && V->state != S_WALK)
        d = 0;                          /* nose to the edge: stand there */

    switch (V->state) {
    case S_IDLE:
    case S_STOP:
    case S_SHOOT:
    case S_TURN:
        if (hit & P_C) { jump(); return; }
        if (d) {
            start_moving(d);
            break;
        }
        if (hit & (P_A | P_B)) {
            shoot_standing();
            break;
        }
        if ((pad & P_DOWN) && V->state != S_SHOOT) {
            V->state = S_DOWN;
            play(A_DOWN_IN + V->facing);
            blend(2);
            break;
        }
        ground_speed(0);
        move_x();
        if (V->state == S_IDLE)
            idle();
        else if (tick())
            idle_at(EXIT(V->me.id));
        break;

    case S_WALK:
        if (hit & P_C) { jump(); return; }
        if (!d) {
            ground_speed(0);
            move_x();
            if (V->tap) {
                /* a tap the other way: the turn on the spot */
                V->state = S_TURN;
                V->vx = 0;
                play(A_TURN + V->facing);
                blend(4);
            } else {
                V->state = S_STOP;
                V->aim = 0;
                play(A_STOP + V->facing);
                blend(4);
            }
            break;
        }
        if (V->tap)
            V->tap--;
        if (ground_speed(d))
            V->skid = SKID;
        if (move_x() && blocked(d)) {
            idle_at(0);
            break;
        }
        if (hit & (P_A | P_B))
            shoot_running();
        else
            walk_advance();
        break;

    case S_LAND:
        if (hit & P_C) { jump(); return; }
        if (V->held) {
            V->held--;
            move_x();
        } else {
            if (d) {
                start_moving(d);
                break;
            }
            if (hit & (P_A | P_B)) {
                shoot_standing();
                break;
            }
            ground_speed(0);
            move_x();
        }
        if (tick())
            idle_at(EXIT(V->me.id));
        break;

    case S_DOWN:
        if ((hit & P_C) && V->y < (200L << 8)) {
            /* Through what he stands on -- and through the rest of it: an
             * icon is a stack of lines a few pixels apart, a window's top
             * is a title bar and an info line, and dropping through one
             * line only to land on the next is not dropping through. */
            fall((WORD)(V->y >> 8) + DROP);
            return;
        }
        if ((V->me.id & ~1u) == A_DOWN_IN) {
            if (!(pad & P_DOWN))
                play(A_DOWN_OUT + V->facing);
            else
                tick();
        } else if (tick()) {
            idle_at(EXIT(V->me.id));
        }
        break;

    case S_AIR:
        air(d, hit);
        return;
    }

    /* Whatever he did on the ground, is the ground still there? */
    if (!follow_ground())
        fall(-32767);
}

/* ---- drawing ----------------------------------------------------------------
 * Thirty-odd sprites a frame, so this is written to be quick: x and y
 * come in with the sprite table's own offsets already added, and nothing
 * is called. */
HOT UWORD *put(UWORD *s, WORD x, WORD y, UBYTE g, UBYTE fl)
{
    const UBYTE *p;
    if ((UWORD)(x - 1) >= 128 + 320 - 1 || (UWORD)(y - 1) >= 511)
        return s;                       /* off the screen -- and an x of 0
                                         * would mask the sprites under it */
    p = groups + 4 * (UWORD)g;
    s[0] = (UWORD)y;
    s[1] = (UWORD)(p[2] << 8);          /* size; the link is filled in later */
    s[2] = (UWORD)(rd16(p) | ((fl & 3) << 11));
    s[3] = (UWORD)x;
    return s + 4;
}

static UWORD *put_list(UWORD *s, const UWORD *end, const UBYTE *p, UBYTE n,
                       WORD ax, WORD ay)
{
    ax += 128;
    ay += 128 + SCREEN_YOFF;
    for (; n && s < end; n--, p += 6)
        s = put(s, (WORD)(ax + (WORD)rd16(p)), (WORD)(ay + (WORD)rd16(p + 2)),
                p[4], p[5]);
    return s;
}

/* In the order the game draws in, earliest on top: the flashes, then
 * him, then what flies behind him. */
static void build_sat(void)
{
    WORD ax = (WORD)(V->x >> 8);
    WORD ay = (WORD)(V->y >> 8) - hdr16(H_FOOT);
    UWORD *s = V->sat, *q;
    const UWORD *end = V->sat + 80 * 4;
    const struct fx *f;
    const struct spr *b;
    UBYTE i, n;

    for (i = 0, f = V->fx; i < NFX; i++, f++)
        if (f->on && f->front && !f->wait)
            s = put_list(s, end, f->pl.front, f->pl.nfront, ax, ay);
    s = put_list(s, end, V->me.front, V->me.nfront, ax, ay);
    compose();
    {
        WORD x0 = (WORD)(ax + 128), y0 = (WORD)(ay + 128 + SCREEN_YOFF);
        for (i = V->nshown, b = V->shown; i && s < end; i--, b++)
            s = put(s, (WORD)(x0 + b->dx), (WORD)(y0 + b->dy), b->g, b->fl);
    }
    s = put_list(s, end, V->me.back, V->me.nback, ax, ay);
    for (i = 0, f = V->fx; i < NFX; i++, f++)
        if (f->on && !f->wait)
            s = put_list(s, end, f->pl.back, f->pl.nback, f->x, f->y);
    n = (UBYTE)((s - V->sat) >> 2);
    for (i = 1, q = V->sat + 1; i < n; i++, q += 4)
        *q |= i;                        /* each to the next; the last to 0 */
    if (!n) {                           /* one hidden entry ends the chain */
        s[0] = s[1] = s[2] = s[3] = 0;
        n = 1;
    }
    V->nsat = n;
}

static void vblank(void)
{
    while (VDP_CTRL & 8)                /* out of the blanking we are in */
        ;
    while (!(VDP_CTRL & 8))             /* and into the next */
        ;
}

static void colours(const UBYTE *p)
{
    UBYTE i;
    VDP_CTRL_L = CRAM_W(2 * (32 + 1));
    for (i = 0; i < 8; i++, p += 2)
        VDP_DATA = rd16(p);
}

static void show(void)
{
    const UWORD *s = V->sat;
    const UBYTE *c = blob + H_CYCLE + 4 * V->cyc;
    UWORD n = V->nsat * 4;
    VDP_CTRL_L = VRAM_W(SAT);
    while (n--)
        VDP_DATA = *s++;
    /* The glow in his joints, which the game cycles. Line 2, 9 and 10. */
    VDP_CTRL_L = CRAM_W(2 * (32 + 9));
    VDP_DATA = rd16(c);
    VDP_DATA = rd16(c + 2);
    V->cyc = (V->cyc + 1) & 63;
    /* A shot's glow, entries 1 to 8, and his own colours after it. */
    if (V->glow < V->nglow) {
        colours(blob + rd32(blob + H_GLOW) + 16 * V->glow);
        V->glow++;
    } else if (V->glow == V->nglow) {
        colours(blob + H_PAL + 2);
        V->glow++;
    }
    sfx_frame();
}

/* A three-button pad on port 1, the way shim.S reads it for Sonic. */
static UBYTE read_pad(void)
{
    UBYTE lo, hi, p = 0;
    PAD_CTRL = 0x40;
    PAD_DATA = 0x00;
    __asm__ volatile("nop\n\tnop");
    lo = (UBYTE)~PAD_DATA;              /* Start A 0 0 D U */
    PAD_DATA = 0x40;
    __asm__ volatile("nop\n\tnop");
    hi = (UBYTE)~PAD_DATA;              /* C B R L D U */
    p = hi & 0x0F;
    if (hi & 0x10) p |= P_B;
    if (hi & 0x20) p |= P_C;
    if (lo & 0x10) p |= P_A;
    if (lo & 0x20) p |= P_START;
    return p;
}

/* ---- in and out ------------------------------------------------------------- */
static void vdp_setup(void)
{
    const UBYTE *r = blob + rd32(blob + H_RUNS);
    UWORD n, i;

    VDP_CTRL = 0x8F02;                  /* auto-increment 2 */
    VDP_CTRL = 0x9100;                  /* the keyboard's window plane off: */
    VDP_CTRL = 0x9200;                  /* its tiles are about to be his */
    for (n = (UWORD)hdr16(H_NRUNS); n; n--, r += 8) {
        const UWORD *t = (const UWORD *)(blob + rd32(r + 4));
        VDP_CTRL_L = VRAM_W((ULONG)rd16(r) << 5);
        for (i = rd16(r + 2) * 16; i; i--)
            VDP_DATA = *t++;
    }
    /* Lines 2 and 3, the keyboard's: his colours and the effects'. */
    VDP_CTRL_L = CRAM_W(2 * 32);
    for (i = 0; i < 32; i++)
        VDP_DATA = (UWORD)hdr16(H_PAL + 2 * i);
}

void vm_main(const UWORD *screen, struct vm *work, ULONG mode,
             const UBYTE *bulk, ULONG blen)
{
    UWORD scr_bank = (UWORD)((mode >> 10) & 3);
    UWORD bulk_bank = (UWORD)((mode >> 8) & 3);
    ULONG *z = (ULONG *)work;
    UWORD n;

    V = work;
    for (n = 0; n < sizeof(struct vm) / 4; n++)
        *z++ = 0;
    read_desktop(screen);               /* the window is on the screen's bank */

    blob = 0;
    if (bulk && blen >= H_SIZE) {
        bank(bulk_bank);
        if (good(bulk, blen))
            blob = bulk;
        else
            bank(scr_bank);
    }
    if (!blob && (mode & 1))
        blob = rom_find();
    if (!blob)
        return;                         /* nothing to draw him with */
    groups = blob + rd32(blob + H_GROUPS);

    foot_l = (WORD)((BYTE)blob[H_FEET_L] + 4);
    foot_r = (WORD)((BYTE)blob[H_FEET_R] - 4);
    edge_l = (WORD)-(BYTE)blob[H_FEET_L];
    edge_r = (WORD)(319 - (BYTE)blob[H_FEET_R]);
    V->seed = (UWORD)(mode >> 16) | 1;
    V->prev = 0xFF;                     /* whatever was held to pick him
                                         * from the menu is not a press */
    V->nglow = (UWORD)hdr16(H_NGLOW);
    V->glow = V->nglow + 1;
    V->bw = 8;
    vdp_setup();
    sound_open();

    /* In from above the middle, and past the menu bar on the way: its top
     * edge is the first thing under him and the whole width of the
     * screen, and he would arrive standing on row 0 with nothing showing
     * but his feet. Anywhere is fair once he is in -- a boost can put him
     * up there, and down with C brings him back. */
    V->facing = 1;
    V->x = 160L << 8;
    V->y = -(8L << 8);
    V->me.id = 0xFFFF;
    fall(24);

    for (;;) {
        build_sat();
        vblank();
        show();
        V->pad = read_pad();
        if (V->pad & ~V->prev & P_START)
            break;
        step();
        fx_step();
        V->prev = V->pad;
    }

    /* Out: no sprites, no sound, and the window back on the screen for
     * the servant, which puts everything else back itself. */
    vblank();
    VDP_CTRL_L = VRAM_W(SAT);
    VDP_DATA = 0;
    VDP_DATA = 0;
    sound_close();
    bank(scr_bank);
}
