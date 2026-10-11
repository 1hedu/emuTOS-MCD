/* VECTOR.MDP -- Vectorman, on the desktop that is on the screen.
 *
 * Desk -> Vectorman, and he drops onto the desktop. Every horizontal
 * edge on it -- the menu bar's underside, a window's frame, the top of
 * an icon -- is a surface he can land on, and he jumps up through them
 * the way he jumps up through the girders of the level he comes from.
 * The d-pad walks, C jumps and C again in the air fires his boots, A or
 * B shoots, down looks down and down with C drops through what he is
 * standing on. Start puts the desktop back.
 *
 * He is the cartridge's own drawing and moves by the cartridge's own
 * numbers. VECTOR.MDD is every pose the game drew him in, recorded off
 * the running game by tools/vectorman-tools/extract.py, and the physics
 * below were measured off the same runs: a walk that gains 32/256 of a
 * pixel a frame up to four, a turn that sheds 128, a jump that starts
 * at -2080 and loses 96 a frame. Nothing here was eyeballed.
 *
 * What it does not have is the game's skeleton. The cartridge works his
 * pose out every frame from joint angles; this plays back the poses it
 * worked out. That is why some things are not here -- shooting while
 * walking, say, which the game composes on the fly and nobody recorded
 * -- and why everything that is here looks exactly right.
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
    A_COUNT = 38
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
#define H_SIZE      360

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

enum { S_IDLE, S_WALK, S_STOP, S_AIR, S_LAND, S_SHOOT, S_DOWN };

struct spr { WORD dx, dy; UBYTE g, fl; };

/* Everything that changes, in the servant's working space. */
struct vm {
    UWORD pix[200 * 20];        /* the desktop, a bit a pixel, set = drawn */
    struct spr body[32];        /* his sprites as the last record left them */
    UWORD sat[80 * 4];
    const UBYTE *front, *back;  /* this frame's effects, in the blob */
    UBYTE nbody, nfront, nback, dur;

    /* the animation playing */
    const UBYTE *anext;         /* the record after the current one */
    UWORD aid, ai, anrec, afr;  /* which, which record, how many, frame */
    UBYTE left;                 /* frames the current record has to run */

    LONG x, y;                  /* his feet, in 1/256 px */
    WORD vx, vy;
    UBYTE facing;               /* 0 left, 1 right */
    UBYTE state, boosted, k, step, skid, held;
    WORD ignore;                /* rows above this are not landed on */
    UWORD loops, due, fidget, seed, cyc, nsat;
    UBYTE pad, prev;
};
_Static_assert(sizeof(struct vm) <= 10240, "head.S WORKSPACE is too small");

/* The one static, and it is initialised so that it is .data: there is
 * no .bss in a payload. */
static struct vm *V = 0;
static const UBYTE *blob = 0;

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

static UWORD rd16(const UBYTE *p) { return (UWORD)((p[0] << 8) | p[1]); }
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
        && rd32(b + H_TOTAL) <= len;
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

static int px(WORD x, WORD y)
{
    if ((UWORD)x >= 320 || y < 0)
        return 0;
    if (y >= 200)
        return 1;                       /* below the screen is the floor */
    return (V->pix[(UWORD)y * 20 + ((UWORD)x >> 4)] << (x & 15)) & 0x8000;
}

static int solid(WORD x, WORD y)
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
static void spr_read(struct spr *s, const UBYTE *p)
{
    s->dx = (WORD)rd16(p);
    s->dy = (WORD)rd16(p + 2);
    s->g = p[4];
    s->fl = p[5];
}

/* One record: his sprites, whole or as changes, then the effects in
 * front of him and behind him, which stay where they are in the blob. */
static const UBYTE *rec(const UBYTE *p)
{
    UBYTE n, i, c;
    V->dur = p[0];
    n = p[2];
    V->nbody = n;
    if (p[1] == 0) {
        p += 3;
        for (i = 0; i < n; i++, p += 6)
            spr_read(&V->body[i], p);
    } else {
        const UBYTE *mask = p + 3;
        p = mask + ((n + 7) >> 3);
        for (i = 0; i < n; i++) {
            UBYTE b;
            if (!(mask[i >> 3] & (0x80 >> (i & 7))))
                continue;
            b = *p++;
            if (b == 0x88)
                continue;               /* replaced below */
            V->body[i].dx += (WORD)(((b >> 4) ^ 8) - 8);
            V->body[i].dy += (WORD)(((b & 15) ^ 8) - 8);
        }
        for (c = *p++; c; c--, p += 7)
            spr_read(&V->body[p[0]], p + 1);
    }
    V->nfront = *p;
    V->front = p + 1;
    p += 1 + 6 * (UWORD)*p;
    V->nback = *p;
    V->back = p + 1;
    p += 1 + 6 * (UWORD)*p;
    return p;
}

static void play(UWORD id)
{
    const UBYTE *t = blob + rd32(blob + H_ANIMS) + 12 * id;
    V->aid = id;
    V->anrec = rd16(t + 4);
    V->ai = 0;
    V->afr = 0;
    V->anext = rec(blob + rd32(t));
    V->left = V->dur;
}

/* A frame on. Nonzero, and nothing moved, if the animation was already
 * on its last frame: what follows is the caller's to decide. */
static int tick(void)
{
    if (V->left > 1) {
        V->left--;
        V->afr++;
        return 0;
    }
    if (V->ai + 1 >= V->anrec)
        return 1;
    V->ai++;
    V->afr++;
    V->anext = rec(V->anext);
    V->left = V->dur;
    return 0;
}

static void seek(UWORD id, UWORD frame)
{
    if (V->aid != id || V->afr > frame)
        play(id);
    while (V->afr < frame && !tick())
        ;
}

static UBYTE anim_byte(UWORD id, UWORD o)
{
    return blob[rd32(blob + H_ANIMS) + 12 * id + o];
}
#define ENTRY(id)   anim_byte(id, 8)    /* the loop frame a fidget leaves at */
#define EXIT(id)    anim_byte(id, 9)    /* the loop frame it hands back to */

/* ---- idling --------------------------------------------------------------
 * He breathes, a 32-frame loop, and every few seconds he does one of the
 * three things the cartridge has him do when nobody is playing. They
 * leave the loop at one frame of it and come back at another, and so do
 * stopping, landing and shooting -- the extractor wrote down where. */
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
    if ((V->aid & ~1u) == A_LOOP) {
        UWORD f = V->fidget + V->facing;
        if (V->loops >= V->due && V->afr + 1 == ENTRY(f)) {
            play(f);
            plan();
            return;
        }
        if (tick()) {
            play(A_LOOP + V->facing);
            V->loops++;
        }
    } else if (tick()) {
        idle_at(EXIT(V->aid));
    }
}

/* ---- the walk ---------------------------------------------------------------
 * The game's cycle is 64 poses in eight segments. A segment runs at one
 * pose a frame, or at two if he is near top speed when it begins -- so
 * the same 64 poses serve every speed, which is what the game does. */
static void walk_pose(void)
{
    seek(A_WALK + 2 * (V->k >> 3) + V->facing, V->k & 7);
}

static void walk_start(void)
{
    V->state = S_WALK;
    V->k = 0;
    V->step = 1;
    V->skid = 0;
    walk_pose();
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
    walk_pose();
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
    play(A_JUMP + V->facing);
}

static void fall(WORD ignore_above)
{
    V->state = S_AIR;
    V->vy = 0;
    V->boosted = 0;
    V->ignore = ignore_above;
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
            seek((V->aid & ~1u) + f, V->afr);
        }
        V->vx += d * AIR_ACC;
        if (V->vx > WALK_MAX) V->vx = WALK_MAX;
        if (V->vx < -WALK_MAX) V->vx = -WALK_MAX;
    }
    if ((hit & P_C) && !V->boosted) {
        V->boosted = 1;
        V->vy = BOOST_V;
        play(A_BOOST + V->facing);
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
                return;
            }
        }
    }
    V->y = ny;
    tick();                             /* the last pose holds */
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
        if (hit & P_C) { jump(); return; }
        if (d) {
            ground_speed(d);
            move_x();
            walk_start();
            break;
        }
        if ((hit & (P_A | P_B)) && V->state != S_SHOOT) {
            V->state = S_SHOOT;
            play(A_SHOOT + V->facing);
            break;
        }
        if ((pad & P_DOWN) && V->state != S_SHOOT) {
            V->state = S_DOWN;
            play(A_DOWN_IN + V->facing);
            break;
        }
        ground_speed(0);
        move_x();
        if (V->state == S_IDLE)
            idle();
        else if (tick())
            idle_at(EXIT(V->aid));
        break;

    case S_WALK:
        if (hit & P_C) { jump(); return; }
        if (!d) {
            V->state = S_STOP;
            ground_speed(0);
            move_x();
            play(A_STOP + V->facing);
            break;
        }
        if (ground_speed(d))
            V->skid = SKID;
        if (move_x() && blocked(d)) {
            idle_at(0);
            break;
        }
        walk_advance();
        break;

    case S_LAND:
        if (hit & P_C) { jump(); return; }
        if (V->held) {
            V->held--;
            move_x();
        } else {
            if (d) {
                ground_speed(d);
                move_x();
                walk_start();
                break;
            }
            ground_speed(0);
            move_x();
        }
        if (tick())
            idle_at(EXIT(V->aid));
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
        if ((V->aid & ~1u) == A_DOWN_IN) {
            if (!(pad & P_DOWN))
                play(A_DOWN_OUT + V->facing);
            else
                tick();
        } else if (tick()) {
            idle_at(EXIT(V->aid));
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

/* ---- drawing ---------------------------------------------------------------- */
static UWORD *put(UWORD *s, const struct spr *p, WORD ax, WORD ay)
{
    const UBYTE *g = blob + rd32(blob + H_GROUPS) + 4 * p->g;
    WORD x = ax + p->dx + 128, y = ay + p->dy + 128 + SCREEN_YOFF;
    if (x <= 0 || x >= 128 + 320 || y <= 0 || y >= 512)
        return s;                       /* off the screen -- and an x of 0
                                         * would mask the sprites under it */
    if (V->nsat >= 80)
        return s;
    s[0] = (UWORD)y;
    s[1] = (UWORD)(g[2] << 8);          /* size; the link is filled in later */
    s[2] = (UWORD)(rd16(g) | ((p->fl & 1) << 11) | ((p->fl & 2) << 11));
    s[3] = (UWORD)x;
    V->nsat++;
    return s + 4;
}

static void build_sat(void)
{
    WORD ax = (WORD)(V->x >> 8);
    WORD ay = (WORD)(V->y >> 8) - hdr16(H_FOOT);
    UWORD *s = V->sat;
    struct spr t;
    const UBYTE *p;
    UBYTE i;

    V->nsat = 0;
    for (i = 0, p = V->front; i < V->nfront; i++, p += 6) {
        spr_read(&t, p);
        s = put(s, &t, ax, ay);
    }
    for (i = 0; i < V->nbody; i++)
        s = put(s, &V->body[i], ax, ay);
    for (i = 0, p = V->back; i < V->nback; i++, p += 6) {
        spr_read(&t, p);
        s = put(s, &t, ax, ay);
    }
    for (i = 0; i < V->nsat; i++)
        V->sat[4 * i + 1] |= (i + 1 < V->nsat) ? i + 1 : 0;
    if (!V->nsat) {                     /* one hidden entry ends the chain */
        V->sat[0] = 0;
        V->sat[1] = 0;
        V->sat[2] = 0;
        V->sat[3] = 0;
        V->nsat = 1;
    }
}

static void vblank(void)
{
    while (VDP_CTRL & 8)                /* out of the blanking we are in */
        ;
    while (!(VDP_CTRL & 8))             /* and into the next */
        ;
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

    foot_l = (WORD)((BYTE)blob[H_FEET_L] + 4);
    foot_r = (WORD)((BYTE)blob[H_FEET_R] - 4);
    edge_l = (WORD)-(BYTE)blob[H_FEET_L];
    edge_r = (WORD)(319 - (BYTE)blob[H_FEET_R]);
    V->seed = (UWORD)(mode >> 16) | 1;
    V->prev = 0xFF;                     /* whatever was held to pick him
                                         * from the menu is not a press */
    vdp_setup();

    /* In from above the middle, and past the menu bar on the way: its top
     * edge is the first thing under him and the whole width of the
     * screen, and he would arrive standing on row 0 with nothing showing
     * but his feet. Anywhere is fair once he is in -- a boost can put him
     * up there, and down with C brings him back. */
    V->facing = 1;
    V->x = 160L << 8;
    V->y = -(8L << 8);
    V->aid = 0xFFFF;
    fall(24);

    for (;;) {
        build_sat();
        vblank();
        show();
        V->pad = read_pad();
        if (V->pad & ~V->prev & P_START)
            break;
        step();
        V->prev = V->pad;
    }

    /* Out: no sprites, and the window back on the screen for the
     * servant, which puts everything else back itself. */
    vblank();
    VDP_CTRL_L = VRAM_W(SAT);
    VDP_DATA = 0;
    VDP_DATA = 0;
    bank(scr_bank);
}
