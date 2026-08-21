/* Sonic, the desk accessory's engine.
 *
 * He runs on the desktop that is already on the screen.  Nothing is drawn into
 * the GEOS bitmap: he is Mega Drive sprites over it, placed from the
 * cartridge's own sprite pieces, so the desktop underneath is never touched and
 * never has to be repaired.
 *
 * The movement is the cartridge's, constant for constant -- see
 * reference/sonic/SONIC_REFERENCE.md, which cites the line each one comes from.
 * Positions are 16.16 and speeds are 8.8, as in the original.
 *
 * The act is four desktops long and nothing stores it: there is one desktop, in
 * the GEOS bitmap, and the level is that desktop named four times through a
 * scrolling name table -- each repeat after the first turned by a random number
 * of cards, so the same furniture comes back in a different arrangement.  See
 * "the camera" below.
 *
 * The ground is the desktop itself.  Sonic's foot sensors read the GEOS bitmap
 * at $A000 and call a pixel solid when three in a row are set, which is what
 * the real game does with "is this tile solid" -- only the tile map here is
 * whatever TopDesk has drawn.  The three-in-a-row test is what rejects the
 * desktop's background, which is a 50% checkerboard and would otherwise be
 * solid everywhere; a window frame, a menu bar underside, an icon's top edge
 * and a divider are all runs of set pixels and all hold him up.
 */
.include "geos_map.inc"
.include "geos_constants.inc"
.include "geos_macros.inc"
/* The port's overrides come after the GEOS map, deliberately: everything
 * this file names is still the map's, except the handful port.inc
 * re-bases onto the servant's VDP layout. See payload/sonic/port.inc. */
.include "port.inc"

.equ VDP_DATA,          0x00C00000
.equ VDP_CTRL,          0x00C00004
/* SAT_BASE: port.inc */
/* SONIC_TILE: port.inc */
/* DUST_TILE: port.inc */
                                         * A's last row and the horizontal scroll
                                         * table, of which the dust wants 16 */
.equ DUST_TILES,        16              /* the largest cue object 08 has */
/* SIGN_TILE: port.inc */
                                         * plane's $C000 page and the SAT at $D800 */
.equ SIGN_UPLOAD_STEP,  16              /* tiles per VBlank, so the copy fits */
/* SONIC_SAT_FIRST: port.inc */
/* SONIC_PAL_LINE: port.inc */
/* GEOS_DISPLAY_Y: port.inc */

/* vdp.S's own, and they are file-local there: the desktop's name table, the
 * horizontal scroll table, and the tile the first GEOS card was uploaded to
 * (tile zero is left blank so nothing ever draws it). */
/* DESKTOP_PLANE_A: port.inc */
/* HSCROLL_BASE: port.inc */
/* GEOS_TILE_BASE: port.inc */
.equ SN_PLANE_COLS,     64              /* register $9001: the plane is 64x32 */
.equ SN_VIS_COLS,       (SCREEN_WIDTH/8)        /* 40 */
.equ SN_VIS_ROWS,       25                      /* 200 lines of GEOS bitmap */

/* Every one of these is the cartridge's own value. */
.equ SN_TOP,            0x0600          /* son_maxspeed */
.equ SN_ACC,            0x000C          /* son_acceleration, and friction */
.equ SN_DEC,            0x0080          /* son_deceleration */
.equ SN_JUMP,           0x0680          /* son_jumpspeed */
.equ SN_GRAVITY,        0x0038          /* gravity */
.equ SN_JUMPCUT,        0x0400          /* Sonic_JumpHeight's cap */
.equ SN_SKID_MIN,       0x0400          /* "cmpi.w #$400" -- below this a turn is
                                         * just a turn and there is no screech */

/* The cartridge's own frame numbers (s1disasm/_anim/Sonic.asm fr_*), which are
 * also the mappings-table indices tools/sonic/export_data.py writes out. */
.equ SN_STAND_FRAME,    1
.equ SN_WALK11,         6
.equ SN_WALK12,         7
.equ SN_WALK13,         8
.equ SN_WALK14,         9
.equ SN_WALK15,         10
.equ SN_WALK16,         11
.equ SN_RUN11,          30
.equ SN_RUN12,          31
.equ SN_RUN13,          32
.equ SN_RUN14,          33
.equ SN_ROLL_FIRST,     46
.equ SN_ROLL_CELS,      5               /* SonAni_Roll: $2E $2F $30 $31 $32 */
.equ SN_STOP1,          55              /* SonAni_Stop: $37, $38, and it loops */
.equ SN_STOP2,          56
.equ SN_STOP_DUR,       7               /* its duration byte, read off the ROM */
.equ SN_DUCK_FRAME,     57              /* SonAni_Duck: $3F $39 $FF, one cel held */

/* Ani_Sonic indices, which is what `anim(a0)` holds.  The table lives at
 * $013B48 in Sonic 1's ROM and tools/sonic read it out of there; the entries
 * used here are 0 walk, 2 roll, 8 duck, $D stop.
 *
 * $1F is Sonic 2's, and it is the one animation in this accessory that cannot
 * come from Sonic 1's table: Sonic 1 has no spindash, so it has no spindash
 * animation and no spindash art.  Sonic 2's own script is `FF 32 33 FF` over
 * two frames drawn for the move.  What this port has is Sonic 1's ball, so the
 * charge runs Sonic 1's own roll cels with the spindash's duration -- a cel a
 * frame, which is faster than any roll he can reach -- and the frames are his
 * rather than a second character's. */
.equ SN_ANI_WALK,       0x00
.equ SN_ANI_ROLL,       0x02
.equ SN_ANI_DUCK,       0x08
.equ SN_ANI_STOP,       0x0D
.equ SN_ANI_SPINDASH,   0x1F

/* Object 08's own animations, Ani_obj08:
 *
 *      Obj08Ani_Dash:  dc.b 1, $A,$B,$C,$D,$E,$F,$10, $FF
 *      Obj08Ani_Skid:  dc.b 3, $11,$12,$13,$14, $FC
 *
 * -- seven cels two frames each, looping while he charges; four cels four
 * frames each and then the object is gone, which is why the skid throws off a
 * new one every four frames rather than animating one in place. */
.equ SN_DUST_NONE,      0
.equ SN_DUST_DASH,      2
.equ SN_DUST_SKID,      3
.equ SN_DUST_DASH_1ST,  0x0A
.equ SN_DUST_DASH_CELS, 7
.equ SN_DUST_DASH_DUR,  1
.equ SN_DUST_SKID_1ST,  0x11
.equ SN_DUST_SKID_CELS, 4
.equ SN_DUST_SKID_DUR,  3
.equ SN_DUST_SPAWN,     3               /* obj08_dust_timer's reload */
.equ SN_DUST_DROP,      0x10            /* "addi.w #$10,y_pos(a1)" */

/* Four is more than the skid can ever have alight at once -- it lasts four
 * frames from top speed and spawns one every four -- but the pool costs
 * nothing and a shorter one would be a limit rather than a headroom. */
.equ SN_DUST_PUFFS,     4
.equ SN_DP_X,           0
.equ SN_DP_Y,           2
.equ SN_DP_STEP,        4               /* $FF when the slot is free */
.equ SN_DP_TIME,        5
.equ SN_DP_SIZE,        6

/* obHeight and obWidth, and the smaller pair he has while curled up.  38/2,
 * 18/2, 28/2 and 14/2 in the cartridge's _Constants.asm; the floor sensor is at
 * obY + obHeight and the two feet at obX +/- obWidth. */
.equ SN_HEIGHT,         19
.equ SN_WIDTH,          9
.equ SN_ROLL_HEIGHT,    14
.equ SN_ROLL_WIDTH,     7
.equ SN_CURL,           (SN_HEIGHT - SN_ROLL_HEIGHT)    /* the 5 pixels Sonic_Jump
                                                         * drops him so his feet
                                                         * stay where they were */

/* The sign post, object 0D.  Its board is 48 wide about its origin and its post
 * reaches 32 below, so standing it at 199 - 32 puts the foot of the post on the
 * bottom line of the screen and the board just clear of it. */
/* The act is four desktops wide and the post stands at the end of it.  Until
 * now there was one screen and no camera, which is why the post sat at 280 and
 * why running out of the act had to be faked with a wall at 336. */
.equ SN_SCREENS,        4
.equ SN_LEVEL_W,       (SCREEN_WIDTH * SN_SCREENS)
.equ SN_LEVEL_COLS,    (SN_LEVEL_W/8)
.equ SN_CAM_MAX,       (SN_LEVEL_W - SCREEN_WIDTH)
/* The level, a column at a time.  256 rather than SN_SCREENS*40 so the index
 * is a mask: sonic_fill_col asks about the column before the leftmost one and
 * about SN_FILL_AHEAD past the rightmost, and neither is worth a comparison on
 * a path this hot. */
.equ SN_MAP_COLS,       256
.equ SN_SURF_MIN,       12              /* how wide a run has to be before he
                                         * may land on it -- and it is measured,
                                         * not chosen: the desktop's icons have
                                         * 15-pixel top edges and its hairline
                                         * details are 10 and under.  Sixteen
                                         * was tried and it dropped him through
                                         * the top of every program icon to
                                         * stand inside the hollow. */
.equ SN_FILL_AHEAD,     41              /* the last column the camera can expose:
                                         * 40 whole ones plus the sliver of the
                                         * next when camX is not a multiple of 8 */
.equ SIGN_X,           (SN_LEVEL_W - 40)
.equ SIGN_Y,            (199 - 32)
.equ SIGN_REACH,        32              /* Sign_Touch's own window */
.equ SIGN_SPIN_TIME,    60              /* "set spin cycle time to 1 second" */

/* Pad bits as genesis_input_update stores them: PORTED to port.inc,
 * because on this machine the thing that fills GEN_M3_PAD_STATE in is
 * the port's own shim and the two have to agree. */

.section .text
.align 2
.global geos_sonic_run
.global geos_sonic_vblank
.global geos_sonic_sat
.extern vdp_set_vram_write
.extern _DoCheckButtons
.extern _MouseOff
.extern _MouseUp

/* ------------------------------------------------------------------ the data */

/* A0 -> the blob.  See tools/sonic/export_data.py for the layout. */
sonic_data_base:
    movea.l sn_blob,%a0
    rts

/* D0.w = frame index -> A0 = its record and D0 nonzero, or D0 = 0. */
sonic_frame_record:
    move.l  %d1,-(%sp)
    /* PORTED. Mask the whole register, not the low word.
     *
     * The frame index arrives in D0 from a `move.b' at the call site,
     * which leaves bits 16-31 as whatever was in D0 before -- and four
     * instructions below, `add.l %d0,%d1' adds all thirty-two of them
     * to the index table's offset. On GEOS those bits happened to be
     * clear and this worked for years. Here they were not: the record
     * pointer came back sixteen megabytes out, the DPLC entry count was
     * read as 255 instead of 2, and the vblank wrote four thousand
     * tiles from a wild pointer straight through VRAM and out the other
     * side. The screen was noise from the first frame. */
    andi.l  #0x0000FFFF,%d0
    movea.l sn_blob,%a0
    move.w  (%a0),%d1                   /* frame count */
    cmp.w   %d1,%d0
    bhs.s   .sfr_none
    move.l  8(%a0),%d1                  /* offset of the frame index */
    add.w   %d0,%d0
    add.w   %d0,%d0
    add.l   %d0,%d1
    move.l  (%a0,%d1.l),%d1             /* offset of this frame's record */
    beq.s   .sfr_none
    adda.l  %d1,%a0
    move.l  (%sp)+,%d1
    moveq   #1,%d0                      /* set D0 last: callers branch on it, and
                                         * restoring D1 with MOVE would otherwise
                                         * be what set the flags they read */
    rts
.sfr_none:
    move.l  (%sp)+,%d1
    moveq   #0,%d0
    rts

/* The same, for object 08's dust, whose header block sits at +$38. */
sonic_dust_record:
    move.l  %d1,-(%sp)
    andi.l  #0x0000FFFF,%d0    /* as sonic_frame_record: the whole register */
    movea.l sn_blob,%a0
    move.w  0x38(%a0),%d1
    cmp.w   %d1,%d0
    bhs.s   .sdr_none
    move.l  0x40(%a0),%d1
    add.w   %d0,%d0
    add.w   %d0,%d0
    add.l   %d0,%d1
    move.l  (%a0,%d1.l),%d1
    beq.s   .sdr_none
    adda.l  %d1,%a0
    move.l  (%sp)+,%d1
    moveq   #1,%d0
    rts
.sdr_none:
    move.l  (%sp)+,%d1
    moveq   #0,%d0
    rts

/* The same, for Map_Sign, whose own header block sits at +$2C. */
sonic_sign_record:
    move.l  %d1,-(%sp)
    andi.l  #0x0000FFFF,%d0    /* as sonic_frame_record: the whole register */
    movea.l sn_blob,%a0
    move.w  0x2C(%a0),%d1
    cmp.w   %d1,%d0
    bhs.s   .sfr_none
    move.l  0x34(%a0),%d1
    add.w   %d0,%d0
    add.w   %d0,%d0
    add.l   %d0,%d1
    move.l  (%a0,%d1.l),%d1
    beq.s   .sfr_none
    adda.l  %d1,%a0
    move.l  (%sp)+,%d1
    moveq   #1,%d0
    rts

/* --------------------------------------------------------------- the ground */

/* D0.w = x, D1.w = y -> D2.w nonzero when the desktop is solid there.
 *
 * Three set pixels in a row.  The desktop's paper is a 50% checkerboard, so a
 * single set pixel means nothing and two adjacent ones cannot happen in it;
 * three is a drawn line, which is what a frame, a divider or an icon's edge is
 * made of. */
sonic_solid:
    movem.l %d0-%d1/%d3-%d4/%a0,-(%sp)
    moveq   #0,%d2
    /* Past the bottom line the screen itself is the floor.  This used to answer
     * "not solid", which put him in an argument with the clamp in sonic_step:
     * the clamp stood him on line 199, the sensor said there was nothing under
     * him, and he alternated between standing and falling every single frame.
     * Nothing was visibly wrong with the sprite -- it was a correctly drawn
     * ball, because the ball is what the falling half of that argument
     * animates.
     *
     * The order of the next two tests matters.  He is dropped in from forty
     * pixels above the screen, so the sensor y is genuinely negative for the
     * first second and a bit, and an unsigned "below 199" reads -20 as 65516
     * and calls the sky a floor. */
    tst.w   %d1
    bmi.s   .ss_none                    /* above the screen: nothing up there */
    cmpi.w  #199,%d1
    bgt.s   .ss_floor
    moveq   #-1,%d4                     /* x-1, x, x+1 */
.ss_next:
    move.w  %d0,%d3
    add.w   %d4,%d3
    bmi.s   .ss_none                    /* off the left edge: not solid */
    cmpi.w  #(SN_LEVEL_W-1),%d3
    bgt.s   .ss_none                    /* nor off the right */
    bsr     sonic_pixel
    tst.w   %d2
    beq.s   .ss_out                     /* any gap and it is not a surface */
    addq.w  #1,%d4
    cmpi.w  #2,%d4
    blt.s   .ss_next
    moveq   #1,%d2
    bra.s   .ss_out
.ss_floor:
    moveq   #1,%d2
    bra.s   .ss_out
.ss_none:
    moveq   #0,%d2                      /* sonic_pixel may have left one behind */
.ss_out:
    movem.l (%sp)+,%d0-%d1/%d3-%d4/%a0
    rts

/* D0.w = a level card column -> D0.w = the desktop card column shown there.
 *
 * The act is SN_SCREENS desktops laid end to end, and every one of them is the
 * desktop: column L shows desktop column L mod SN_VIS_COLS, and that is all
 * this is.
 *
 * PORTED only in how it is answered.  The cartridge turned each repeat after
 * the first by a random number of cards; a version of this made runs of
 * *stalls* -- one column repeated, to stretch the printer's icon into a bar
 * you could stand on -- and it went, because a clean repeat reads better than
 * a shuffled one.  See docs/sonic.md.
 *
 * It stayed a table, one byte a column, built once at the start: sonic_pixel
 * asks this question a couple of hundred times a frame and the answer used to
 * be a DIVU. */
sonic_desk_col:
    movem.l %d1/%a0,-(%sp)
    move.w  %d0,%d1
    andi.w  #(SN_MAP_COLS-1),%d1
    lea     sn_deskmap,%a0
    moveq   #0,%d0
    move.b  (%a0,%d1.w),%d0
    movem.l (%sp)+,%d1/%a0
    rts

/* Build it: SN_MAP_COLS entries of "column L shows desktop column L mod 40".
 *
 * No division, because there is none on this path and there does not need to
 * be one here either -- a counter that wraps at SN_VIS_COLS is the modulo.
 * SN_MAP_COLS is 256 so the lookup can mask, and the act is 160 columns, which
 * is four whole desktops, so the only entries past the act are the ones
 * sonic_fill_col reads when it draws the column either side of what the camera
 * can see.  They are off screen and they are still desktop. */
sonic_deskmap_build:
    movem.l %d0/%d3/%a0,-(%sp)
    lea     sn_deskmap,%a0
    move.w  #(SN_MAP_COLS-1),%d0
    moveq   #0,%d3
.dm_col:
    move.b  %d3,(%a0)+
    addq.w  #1,%d3
    cmpi.w  #SN_VIS_COLS,%d3
    blo.s   .dm_next
    moveq   #0,%d3
.dm_next:
    dbra    %d0,.dm_col
    movem.l (%sp)+,%d0/%d3/%a0
    rts

/* D3.w = level x, D1.w = y -> D2.w = that pixel of the GEOS bitmap.
 *
 * There is one bitmap and it is 320 wide, so the level x has to come back
 * through the same mapping the name table used.  It has to be the same one:
 * what he stands on and what he can see are the same desktop card, and a
 * sensor that read the unturned column would have him walking on furniture
 * that is somewhere else on the screen. */
sonic_pixel:
    movem.l %d0/%d3-%d5/%a0,-(%sp)
    move.w  %d3,%d0
    lsr.w   #3,%d0
    bsr     sonic_desk_col
    lsl.w   #3,%d0
    move.w  %d3,%d5
    andi.w  #7,%d5
    add.w   %d5,%d0
    move.w  %d0,%d3
    /* PORTED. GEOS keeps the desktop as one bit per pixel in 8-byte
     * cards, 320 bytes to a card row; the servant's screen is the Atari
     * ST's, 320x200 in four bitplanes interleaved a word at a time --
     * sixteen pixels per group of four words, 160 bytes to a line.
     *
     * The rule does not change. GEOS asks "is this bit set"; here the
     * question is "is this pixel not the background colour", which is
     * the same question about a screen that has sixteen of them, and
     * the answer is the OR of the four planes. Three in a row still
     * make a surface, and sonic_solid is untouched.
     *
     * sn_screen is where the servant said the screen is, in the Mega
     * CD's window -- and the window shows one 128K bank at a time. Bank
     * 3 holds the art and is selected for the whole run because every
     * frame reads it; the screen is bank 2, and this is the only thing
     * in the engine that reads the screen. So the borrow is here, four
     * words wide, rather than anywhere that would have to remember. */
    bsr     sn_bank2
    move.w  %d1,%d4
    mulu.w  #160,%d4                    /* line */
    move.w  %d3,%d5
    lsr.w   #4,%d5
    lsl.w   #3,%d5                      /* + group of sixteen, four words */
    add.w   %d5,%d4
    movea.l sn_screen,%a0
    adda.w  %d4,%a0
    move.w  (%a0),%d2                   /* plane 0 */
    or.w    2(%a0),%d2                  /* 1 */
    or.w    4(%a0),%d2                  /* 2 */
    or.w    6(%a0),%d2                  /* 3 */
    bsr     sn_bank3                    /* and give the window back */
    move.w  %d3,%d5
    andi.w  #15,%d5
    eori.w  #15,%d5                     /* bit 15 is the leftmost pixel */
    btst    %d5,%d2
    beq.s   .sp_clear
    moveq   #1,%d2
    bra.s   .sp_done
.sp_clear:
    moveq   #0,%d2
.sp_done:
    movem.l (%sp)+,%d0/%d3-%d5/%a0
    rts

/* Any sensor on line D1.w, about centre D0.w -> D2.w.
 *
 * PORTED: there are three of them.  The cartridge has two, one under each
 * shoulder, and that is right for a game whose terrain comes in sixteen-pixel
 * tiles -- nothing there is ever narrower than he is.  The desktop is not
 * built like that.  A program icon's top edge is *fifteen* pixels and his
 * shoulders are eighteen apart, so standing dead centre on one, both feet
 * missed it: he fell through the top of the icon and came to rest on its
 * bottom edge, inside the hollow, with the outline drawn round him and
 * nothing under his shoes.  That is the "floating mid program icon".
 *
 * A sensor between his feet costs one more read and answers it.  It also
 * makes him a little more willing to stand on a narrow thing, which on this
 * terrain is the point. */
sonic_ground_line:
    movem.l %d0-%d1,-(%sp)
    moveq   #0,%d2
    move.b  (GEN_SONIC_WIDTH-GEOS_BIAS)(%a6),%d2
    sub.w   %d2,%d0                     /* sensor A, the left shoulder */
    bsr     sonic_solid_sel
    tst.w   %d2
    bne.s   .gl_out
    moveq   #0,%d2
    move.b  (GEN_SONIC_WIDTH-GEOS_BIAS)(%a6),%d2
    add.w   %d2,%d0                     /* ...back to the middle of him */
    bsr     sonic_solid_sel
    tst.w   %d2
    bne.s   .gl_out
    moveq   #0,%d2
    move.b  (GEN_SONIC_WIDTH-GEOS_BIAS)(%a6),%d2
    add.w   %d2,%d0                     /* sensor B, the right shoulder */
    bsr     sonic_solid_sel
.gl_out:
    movem.l (%sp)+,%d0-%d1
    rts

/* PORTED: what counts as a surface to come down on.
 *
 * sonic_solid is three adjacent set pixels, which is GEOS's rule and is fine
 * on a level drawn as terrain.  The desktop is not drawn as terrain, it is
 * drawn as line art, and three pixels is a hairline.  The printer icon's paper
 * tray is a ten-pixel run at y=167 with seven rows of white under it and the
 * printer's actual top edge at y=175: falling onto the printer, he caught the
 * hairline and stood eight pixels over the icon with nothing beneath him.  The
 * trash can's lid handle does it at five pixels, the icon labels do it at four,
 * and so does the mouse pointer's arrow.
 *
 * So a surface he can *land* on has to be SN_SURF_MIN pixels wide -- the run
 * he is touching, measured from wherever on it his foot is, which is the part
 * that matters: the trash can's lid is 21 pixels and he has to be able to come
 * down on either end of it, not only its middle.  Sixteen is two cards, and
 * the desktop is drawn in cards; every real top edge on it clears that (the
 * printer's is 22, the lid 21, the menu bar and the floor the whole width) and
 * every hairline is 4 to 10.
 *
 * Landing only.  Once he is standing, sonic_ground is called the old way and
 * three pixels under either foot keep him up, so walking to the end of a ledge
 * is exactly what it was.  The two rules answer different questions -- "is
 * there something here to come down on" and "is there still something under
 * me" -- and it is only the first that a hairline should not get to answer. */
sonic_ground_land:
    move.b  #1,sn_gnd_wide
    bsr     sonic_ground
    clr.b   sn_gnd_wide
    rts

/* D0.w = x, D1.w = y -> D2.w, through whichever rule is in force. */
sonic_solid_sel:
    tst.b   sn_gnd_wide
    bne.s   sonic_solid_wide
    bra     sonic_solid

/* sonic_solid, and then how wide the run it found is.  It stops counting at
 * SN_SURF_MIN because nothing past that changes the answer, so this is at
 * worst sixteen reads of the screen and it is only reached while he is in the
 * air. */
sonic_solid_wide:
    bsr     sonic_solid
    tst.w   %d2
    beq.s   .sw_out                     /* not even a hairline */
    cmpi.w  #199,%d1
    bgt.s   .sw_out                     /* the screen's own floor, 320 wide */
    movem.l %d0-%d1/%d3-%d4,-(%sp)
    moveq   #1,%d4                      /* the pixel under his foot */
    move.w  %d0,%d3
.sw_left:
    subq.w  #1,%d3
    bmi.s   .sw_right_start
    bsr     sonic_pixel
    tst.w   %d2
    beq.s   .sw_right_start
    addq.w  #1,%d4
    cmpi.w  #SN_SURF_MIN,%d4
    blo.s   .sw_left
    bra.s   .sw_wide
.sw_right_start:
    move.w  %d0,%d3
.sw_right:
    addq.w  #1,%d3
    cmpi.w  #(SN_LEVEL_W-1),%d3
    bgt.s   .sw_narrow
    bsr     sonic_pixel
    tst.w   %d2
    beq.s   .sw_narrow
    addq.w  #1,%d4
    cmpi.w  #SN_SURF_MIN,%d4
    blo.s   .sw_right
.sw_wide:
    movem.l (%sp)+,%d0-%d1/%d3-%d4
    moveq   #1,%d2
    rts
.sw_narrow:
    movem.l (%sp)+,%d0-%d1/%d3-%d4
    moveq   #0,%d2
.sw_out:
    rts

/* Is there ground under his feet?  D0.w = x centre, D1.w = y centre.
 * Returns D2.w nonzero and D3.w = the surface's y when either sensor hits. */
sonic_ground:
    movem.l %d0-%d1/%d4,-(%sp)
    move.w  %d1,%d3
    moveq   #0,%d4
    move.b  (GEN_SONIC_HEIGHT-GEOS_BIAS)(%a6),%d4
    add.w   %d4,%d3
    move.w  %d3,%d1
    bsr     sonic_ground_line
    tst.w   %d2
    beq.s   .sg_out
    /* A point sensor reports the first line *inside* the surface, so a fast
     * fall would leave him sunk into it by however far he travelled that frame
     * -- landing on the bottom of the screen at 186 instead of 180.  The
     * original reads a sixteen-pixel column and returns the distance to the
     * surface; walking back up to the last solid line is the same answer. */
    moveq   #SN_HEIGHT,%d4              /* never walk further than he is tall */
.sg_up:
    move.w  %d3,%d1
    subq.w  #1,%d1
    bsr     sonic_ground_line
    tst.w   %d2
    beq.s   .sg_top
    subq.w  #1,%d3
    subq.w  #1,%d4
    bgt.s   .sg_up
.sg_top:
    moveq   #1,%d2
.sg_out:
    movem.l (%sp)+,%d0-%d1/%d4
    rts

/* --------------------------------------------------------------- the camera */

/* Plane A is 64 cards wide and the screen sees 40 of them, so the act is not
 * laid out in the plane -- it is drawn through it.  The plane is a window 64
 * cards long that the camera drags along the level: level column L always lives
 * in plane column L mod 64, and because the plane wraps at 512 pixels and the
 * scroll register wraps with it, the same arithmetic does both.  Every time the
 * camera uncovers a column, the one 64 back from it is 23 cards off the far
 * edge and free to be rewritten -- so one column of 25 name-table cells a step
 * is the whole cost of scrolling, whatever the level's length.
 *
 * The tiles themselves are never touched.  A GEOS card is uploaded once and
 * named as often as the level repeats it. */

/* D2.w = the level column to put into the plane. */
sonic_fill_col:
    movem.l %d0-%d4/%a0,-(%sp)
    move.w  %sr,-(%sp)
    ori.w   #0x0700,%sr                 /* the VBlank handler walks the same
                                         * VDP with an auto-increment of two */
    move.w  %d2,%d0
    bsr     sonic_desk_col
    move.w  %d0,%d4                     /* the desktop column it shows */
    move.w  %d2,%d3
    andi.w  #(SN_PLANE_COLS-1),%d3      /* the plane column it lands in */
    move.w  #0x8F80,VDP_CTRL            /* step a whole plane row at a time */
    move.w  #DESKTOP_PLANE_A,%d0
    add.w   %d3,%d0
    add.w   %d3,%d0
    jsr     vdp_set_vram_write
    move.w  %d4,%d1
    addi.w  #GEOS_TILE_BASE,%d1         /* the card in the top row of the column */
    moveq   #(SN_VIS_ROWS-1),%d2
.fc_row:
    move.w  %d1,VDP_DATA
    addi.w  #SN_VIS_COLS,%d1            /* the card below it */
    dbra    %d2,.fc_row
    move.w  #0x8F02,VDP_CTRL            /* back to the word step everything
                                         * else in this port assumes */
    move.w  (%sp)+,%sr
    movem.l (%sp)+,%d0-%d4/%a0
    rts

/* The camera.  Sonic 1 keeps him in the middle of the screen and stops at the
 * level's edges, which with no vertical scrolling is all of it. */
sonic_camera:
    movem.l %d0-%d2,-(%sp)
    move.l  (GEN_SONIC_X-GEOS_BIAS)(%a6),%d0
    swap    %d0
    subi.w  #(SCREEN_WIDTH/2),%d0
    bpl.s   .cam_low_ok
    moveq   #0,%d0
.cam_low_ok:
    cmpi.w  #SN_CAM_MAX,%d0
    ble.s   .cam_high_ok
    move.w  #SN_CAM_MAX,%d0
.cam_high_ok:
    move.w  %d0,(GEN_SONIC_CAMX-GEOS_BIAS)(%a6)

    /* Whatever the camera crossed, fill it.  Top speed is six pixels a frame so
     * this is never more than one column, but a loop costs nothing and a missed
     * column would be a stripe of the wrong desktop for the rest of the run. */
    lsr.w   #3,%d0
    move.w  (GEN_SONIC_CAMCOL-GEOS_BIAS)(%a6),%d1
    cmp.w   %d1,%d0
    beq.s   .cam_out
    bhi.s   .cam_right
.cam_left:
    subq.w  #1,%d1
    move.w  %d1,%d2                     /* the column appearing on the left */
    bsr     sonic_fill_col
    cmp.w   %d1,%d0
    bne.s   .cam_left
    bra.s   .cam_store
.cam_right:
    addq.w  #1,%d1
    move.w  %d1,%d2
    addi.w  #SN_FILL_AHEAD,%d2          /* the one appearing on the right, and
                                         * it is filled a card early: the last
                                         * visible column is only a sliver wide
                                         * and it is already on the screen */
    bsr     sonic_fill_col
    cmp.w   %d1,%d0
    bne.s   .cam_right
.cam_store:
    move.w  %d0,(GEN_SONIC_CAMCOL-GEOS_BIAS)(%a6)
.cam_out:
    movem.l (%sp)+,%d0-%d2
    rts

/* The scroll register itself, written in the VBlank: it moves the whole page
 * and changing it mid-frame tears the screen across a scanline. */
sonic_hscroll:
    move.w  %sr,-(%sp)
    ori.w   #0x0700,%sr
    movem.l %d0-%d1,-(%sp)
    move.w  #HSCROLL_BASE,%d0
    jsr     vdp_set_vram_write
    move.w  (GEN_SONIC_CAMX-GEOS_BIAS)(%a6),%d0
    neg.w   %d0                         /* the page moves left as he moves right */
    move.w  %d0,VDP_DATA
    movem.l (%sp)+,%d0-%d1
    move.w  (%sp)+,%sr
    rts

/* Lay the whole 64-column window down, which is what the accessory starts from.
 * Columns 0 to 39 come out identical to the name table vdp_init built -- the
 * first repeat is not turned -- so nothing on the screen moves; the other 24
 * were the blank pad and are off the right edge while they are written. */
sonic_plane_init:
    movem.l %d0-%d2,-(%sp)
    moveq   #0,%d2
.pi_col:
    bsr     sonic_fill_col
    addq.w  #1,%d2
    cmpi.w  #SN_PLANE_COLS,%d2
    blo.s   .pi_col
    movem.l (%sp)+,%d0-%d2
    rts

/* And give it back.  The plane is the desktop's, not the accessory's: the name
 * table goes back to naming card R*40+C at column C and the pad columns go back
 * to blank, exactly as vdp_init writes them, and the scroll goes back to zero.
 * The GEOS bitmap in WRAM was never touched, so this is the whole repair. */
sonic_plane_restore:
    movem.l %d0-%d4,-(%sp)
    move.w  %sr,-(%sp)
    ori.w   #0x0700,%sr
    move.w  #HSCROLL_BASE,%d0
    jsr     vdp_set_vram_write
    move.w  #0,VDP_DATA
    move.w  #DESKTOP_PLANE_A,%d0
    jsr     vdp_set_vram_write
    moveq   #0,%d1                      /* the card at the left of the row */
    moveq   #(SN_VIS_ROWS-1),%d2
.pr_row:
    move.w  %d1,%d0
    addi.w  #GEOS_TILE_BASE,%d0
    moveq   #(SN_VIS_COLS-1),%d3
.pr_visible:
    move.w  %d0,VDP_DATA
    addq.w  #1,%d0
    dbra    %d3,.pr_visible
    moveq   #(SN_PLANE_COLS-SN_VIS_COLS-1),%d4
.pr_pad:
    move.w  #0,VDP_DATA
    dbra    %d4,.pr_pad
    addi.w  #SN_VIS_COLS,%d1
    dbra    %d2,.pr_row
    move.w  (%sp)+,%sr
    movem.l (%sp)+,%d0-%d4
    rts

/* -------------------------------------------------------------- the physics */

sonic_step:
    movem.l %d0-%d7/%a0,-(%sp)
    move.w  (GEN_SONIC_PAD-GEOS_BIAS)(%a6),%d7
    /* Walking is what he does unless this frame says otherwise.  In the
     * cartridge every routine that wants a different animation writes `anim`
     * and the rest leave it alone; here it is reset first and written by the
     * three that want something else -- the skid, the duck and the charge --
     * and it cannot strand him mid-screech.
     *
     * It is NOT the same thing for the skid, which is why the latch below
     * exists: the duck and the charge assert their animation every frame they
     * own him, and the cartridge's braking path asserts the pose once and then
     * spends the rest of the brake deliberately not writing `anim`.  Rolling
     * the latch forward here is what gives that back.  See sonic_skid. */
    move.b  #SN_ANI_WALK,(GEN_SONIC_ANIM-GEOS_BIAS)(%a6)
    move.b  sn_skid_on,sn_skid_was
    clr.b   sn_skid_on
    tst.b   (GEN_SONIC_AIR-GEOS_BIAS)(%a6)
    bne     .st_air

    /* --- on the ground.  Sonic_Move, with the angle always flat. --- */
    bsr     sonic_spindash              /* Sonic_CheckSpindash, and Sonic_Duck
                                         * before it: while either owns him the
                                         * rest of the ground path is skipped,
                                         * which is what the "addq.l #4,sp" in
                                         * Sonic_CheckSpindash does */
    tst.w   %d0
    bne     .st_integrate
    move.w  (GEN_SONIC_PAD-GEOS_BIAS)(%a6),%d7
    move.w  (GEN_SONIC_INERTIA-GEOS_BIAS)(%a6),%d0
    btst    #PAD_LEFT,%d7
    beq.s   .st_notleft
    tst.w   %d0
    ble.s   .st_left_acc
    sub.w   #SN_DEC,%d0                 /* turning: deceleration, not acceleration */
    bgt.s   .st_left_skid
    move.w  #-0x80,%d0                  /* the original's half-pixel kickback */
    bsr     sonic_skid_carry            /* the sign-change frame is still a
                                         * braking frame: the cartridge reaches
                                         * .nostopping with d0 = -$80 and leaves
                                         * `anim` alone here too */
    bra.s   .st_have
.st_left_skid:
    cmpi.w  #SN_SKID_MIN,%d0            /* "cmpi.w #$400 / blt" -- and it is the
                                         * decelerated value that is tested, so
                                         * the screech stops before he does */
    blt.s   .st_left_hold
    bsr     sonic_skid
    bra.s   .st_have
.st_left_hold:
    bsr     sonic_skid_carry            /* under $400: no screech, and the pose
                                         * only if the brake already had it */
    bra.s   .st_have
.st_left_acc:
    sub.w   #SN_ACC,%d0
    cmpi.w  #-SN_TOP,%d0
    bge.s   .st_have
    move.w  #-SN_TOP,%d0
    bra.s   .st_have
.st_notleft:
    btst    #PAD_RIGHT,%d7
    beq.s   .st_nodir
    tst.w   %d0
    bge.s   .st_right_acc
    add.w   #SN_DEC,%d0
    blt.s   .st_right_skid
    move.w  #0x80,%d0
    bsr     sonic_skid_carry            /* the sign-change frame; see the left */
    bra.s   .st_have
.st_right_skid:
    cmpi.w  #-SN_SKID_MIN,%d0           /* "cmpi.w #-$400 / bgt" */
    bgt.s   .st_right_hold
    bsr     sonic_skid
    bra.s   .st_have
.st_right_hold:
    bsr     sonic_skid_carry
    bra.s   .st_have
.st_right_acc:
    add.w   #SN_ACC,%d0
    cmpi.w  #SN_TOP,%d0
    ble.s   .st_have
    move.w  #SN_TOP,%d0
    bra.s   .st_have
.st_nodir:
    /* Friction is acceleration -- Sonic_ResetScr subtracts d5, and d5 is
     * v_sonspeedacc.  Sonic 1 has no separate friction constant. */
    tst.w   %d0
    beq.s   .st_have
    bmi.s   .st_fric_left
    sub.w   #SN_ACC,%d0
    bge.s   .st_have
    moveq   #0,%d0
    bra.s   .st_have
.st_fric_left:
    add.w   #SN_ACC,%d0
    ble.s   .st_have
    moveq   #0,%d0
.st_have:
    move.w  %d0,(GEN_SONIC_INERTIA-GEOS_BIAS)(%a6)
    move.w  %d0,(GEN_SONIC_XSP-GEOS_BIAS)(%a6)      /* flat ground: ground speed is x speed */
    tst.w   %d0
    beq.s   .st_face_keep
    bpl.s   .st_face_right
    move.b  #1,(GEN_SONIC_FACING-GEOS_BIAS)(%a6)
    bra.s   .st_face_keep
.st_face_right:
    clr.b   (GEN_SONIC_FACING-GEOS_BIAS)(%a6)
.st_face_keep:
    /* Jump. */
    move.w  (GEN_SONIC_PADEDGE-GEOS_BIAS)(%a6),%d1
    andi.w  #((1<<PAD_A)+(1<<PAD_B)+(1<<PAD_C)),%d1
    beq.s   .st_ground_move
    move.w  #-SN_JUMP,(GEN_SONIC_YSP-GEOS_BIAS)(%a6)
    move.b  #1,(GEN_SONIC_AIR-GEOS_BIAS)(%a6)
    move.b  #1,(GEN_SONIC_JUMPING-GEOS_BIAS)(%a6)   /* also his ball flag: obStatus bit 2 and
                                         * jumping(a0) are set and cleared
                                         * together, and only by a jump */
    move.b  #SN_ROLL_HEIGHT,(GEN_SONIC_HEIGHT-GEOS_BIAS)(%a6)
    move.b  #SN_ROLL_WIDTH,(GEN_SONIC_WIDTH-GEOS_BIAS)(%a6)
    addi.l  #(SN_CURL<<16),(GEN_SONIC_Y-GEOS_BIAS)(%a6)
    bsr     sonic_sfx_jump              /* "move.w #sfx_Jump,d0" is here in
                                         * Obj01, at _incObj/01 Sonic.asm:1238 */
.st_ground_move:
    bra     .st_integrate

    /* --- in the air.  Sonic_JumpDirection, Sonic_JumpHeight, Sonic_AirDrag. --- */
.st_air:
    move.w  (GEN_SONIC_XSP-GEOS_BIAS)(%a6),%d0
    btst    #PAD_LEFT,%d7
    beq.s   .st_air_right
    sub.w   #(SN_ACC*2),%d0             /* acceleration doubled: asl.w #1,d5 */
    cmpi.w  #-SN_TOP,%d0
    bge.s   .st_air_done
    move.w  #-SN_TOP,%d0
    bra.s   .st_air_done
.st_air_right:
    btst    #PAD_RIGHT,%d7
    beq.s   .st_air_done
    add.w   #(SN_ACC*2),%d0
    cmpi.w  #SN_TOP,%d0
    ble.s   .st_air_done
    move.w  #SN_TOP,%d0
.st_air_done:
    move.w  %d0,(GEN_SONIC_XSP-GEOS_BIAS)(%a6)
    tst.w   %d0
    beq.s   .st_air_face_keep
    bpl.s   .st_air_face_right
    move.b  #1,(GEN_SONIC_FACING-GEOS_BIAS)(%a6)
    bra.s   .st_air_face_keep
.st_air_face_right:
    clr.b   (GEN_SONIC_FACING-GEOS_BIAS)(%a6)
.st_air_face_keep:
    /* Jump height: let go while still rising fast and the rise is capped. */
    tst.b   (GEN_SONIC_JUMPING-GEOS_BIAS)(%a6)
    beq.s   .st_nocut
    move.w  (GEN_SONIC_YSP-GEOS_BIAS)(%a6),%d1
    cmpi.w  #-SN_JUMPCUT,%d1
    bge.s   .st_nocut
    move.w  %d7,%d2
    andi.w  #((1<<PAD_A)+(1<<PAD_B)+(1<<PAD_C)),%d2
    bne.s   .st_nocut
    move.w  #-SN_JUMPCUT,(GEN_SONIC_YSP-GEOS_BIAS)(%a6)
.st_nocut:
    /* Air drag, gated on Y speed alone, as Sonic_AirDrag writes it. */
    move.w  (GEN_SONIC_YSP-GEOS_BIAS)(%a6),%d1
    cmpi.w  #-SN_JUMPCUT,%d1
    blt.s   .st_nodrag
    move.w  (GEN_SONIC_XSP-GEOS_BIAS)(%a6),%d0
    move.w  %d0,%d1
    asr.w   #5,%d1
    beq.s   .st_nodrag
    sub.w   %d1,%d0
    move.w  %d0,(GEN_SONIC_XSP-GEOS_BIAS)(%a6)
.st_nodrag:
    add.w   #SN_GRAVITY,(GEN_SONIC_YSP-GEOS_BIAS)(%a6)

.st_integrate:
    /* 8.8 speed into a 16.16 position is a shift of eight. */
    move.w  (GEN_SONIC_XSP-GEOS_BIAS)(%a6),%d0
    ext.l   %d0
    lsl.l   #8,%d0
    add.l   %d0,(GEN_SONIC_X-GEOS_BIAS)(%a6)
    move.w  (GEN_SONIC_YSP-GEOS_BIAS)(%a6),%d0
    ext.l   %d0
    lsl.l   #8,%d0
    add.l   %d0,(GEN_SONIC_Y-GEOS_BIAS)(%a6)

    /* Keep him on the screen horizontally. */
    move.l  (GEN_SONIC_X-GEOS_BIAS)(%a6),%d0
    swap    %d0
    cmpi.w  #8,%d0
    bge.s   .st_xleft_ok
    move.w  #8,%d0
    clr.w   (GEN_SONIC_XSP-GEOS_BIAS)(%a6)
    clr.w   (GEN_SONIC_INERTIA-GEOS_BIAS)(%a6)
    swap    %d0
    clr.w   %d0
    swap    %d0
    move.w  %d0,(GEN_SONIC_X-GEOS_BIAS)(%a6)
.st_xleft_ok:
    move.l  (GEN_SONIC_X-GEOS_BIAS)(%a6),%d0
    swap    %d0
    move.w  #(SN_LEVEL_W-9),%d1         /* the ordinary wall: the act's end */
    tst.b   (GEN_SONIC_LOCK-GEOS_BIAS)(%a6)
    beq.s   .st_wall
    /* Running out of the act.  The camera has already stopped at SN_CAM_MAX, so
     * he crosses the right edge of the screen on his own and keeps going for a
     * sprite's width past it -- which is the game's own ending and what the
     * wall at 336 was standing in for when there was nothing to scroll. */
    move.w  #(SN_LEVEL_W+24),%d1
.st_wall:
    cmp.w   %d1,%d0
    ble.s   .st_xright_ok
    move.w  %d1,(GEN_SONIC_X-GEOS_BIAS)(%a6)
    clr.w   (GEN_SONIC_XSP-GEOS_BIAS)(%a6)
    clr.w   (GEN_SONIC_INERTIA-GEOS_BIAS)(%a6)
.st_xright_ok:

    /* --- the ground sensors --- */
    move.l  (GEN_SONIC_X-GEOS_BIAS)(%a6),%d0
    swap    %d0
    move.l  (GEN_SONIC_Y-GEOS_BIAS)(%a6),%d1
    swap    %d1
    tst.b   (GEN_SONIC_AIR-GEOS_BIAS)(%a6)
    bne.s   .st_air_land
    /* Walked off an edge? */
    bsr     sonic_ground
    tst.w   %d2
    bne     .st_done
    move.b  #1,(GEN_SONIC_AIR-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_JUMPING-GEOS_BIAS)(%a6)
    bra     .st_done
.st_air_land:
    move.w  (GEN_SONIC_YSP-GEOS_BIAS)(%a6),%d4
    bmi     .st_done                    /* rising: nothing to land on */
    bsr     sonic_ground_land
    tst.w   %d2
    beq.s   .st_floor
    /* D3 held the sensor line; put his feet on it. */
    move.w  %d3,%d1
    moveq   #0,%d2
    move.b  (GEN_SONIC_HEIGHT-GEOS_BIAS)(%a6),%d2
    sub.w   %d2,%d1
    swap    %d1
    clr.w   %d1
    swap    %d1
    move.w  %d1,(GEN_SONIC_Y-GEOS_BIAS)(%a6)
    bsr     sonic_uncurl
    clr.w   (GEN_SONIC_YSP-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_AIR-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_JUMPING-GEOS_BIAS)(%a6)
    move.w  (GEN_SONIC_XSP-GEOS_BIAS)(%a6),(GEN_SONIC_INERTIA-GEOS_BIAS)(%a6)
    bra.s   .st_done
.st_floor:
    /* The bottom of the screen is a floor, so he cannot be lost. */
    cmpi.w  #(199-SN_HEIGHT),%d1
    blt.s   .st_done
    move.w  #(199-SN_HEIGHT),(GEN_SONIC_Y-GEOS_BIAS)(%a6)
    bsr     sonic_uncurl
    clr.w   (GEN_SONIC_YSP-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_AIR-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_JUMPING-GEOS_BIAS)(%a6)
    move.w  (GEN_SONIC_XSP-GEOS_BIAS)(%a6),(GEN_SONIC_INERTIA-GEOS_BIAS)(%a6)
.st_done:
    movem.l (%sp)+,%d0-%d7/%a0
    rts

/* Sonic_ResetOnFloor's ball half: the hitbox goes back to standing size and he
 * comes up the five pixels Sonic_Jump dropped him, "so he's not inside the
 * ground". */
sonic_uncurl:
    tst.b   (GEN_SONIC_JUMPING-GEOS_BIAS)(%a6)
    beq.s   .uc_out
    subi.l  #(SN_CURL<<16),(GEN_SONIC_Y-GEOS_BIAS)(%a6)
    move.b  #SN_HEIGHT,(GEN_SONIC_HEIGHT-GEOS_BIAS)(%a6)
    move.b  #SN_WIDTH,(GEN_SONIC_WIDTH-GEOS_BIAS)(%a6)
.uc_out:
    rts

/* ------------------------------------------------------------ the animation */

/* The cartridge's own scripts and its own timing:
 *    duration = max(0, $800 - |inertia|) >> 8
 *    |inertia| >= $600 runs, below that walks
 * On flat ground the octant modifier is zero, so no frame offset is added. */
sonic_animate:
    movem.l %d0-%d3/%a0,-(%sp)
    /* The ball is not "airborne" -- it is obStatus bit 2, which only Sonic_Jump
     * and Sonic_Roll ever set.  Walking off a ledge leaves him upright and the
     * walk cycle running, and that is the only reason the ball ever appears. */
    tst.b   (GEN_SONIC_JUMPING-GEOS_BIAS)(%a6)
    beq.s   .an_have_id
    move.b  #SN_ANI_ROLL,(GEN_SONIC_ANIM-GEOS_BIAS)(%a6)
.an_have_id:
    /* Sonic_Animate's own first act: a change of script restarts it.
     *      move.b  anim(a0),d0
     *      cmp.b   next_anim(a0),d0
     *      beq.s   +
     *      move.b  d0,next_anim(a0)
     *      move.b  #0,anim_frame(a0)
     *      move.b  #0,anim_frame_duration(a0)
     */
    move.b  (GEN_SONIC_ANIM-GEOS_BIAS)(%a6),%d0
    cmp.b   (GEN_SONIC_ANIMPREV-GEOS_BIAS)(%a6),%d0
    beq.s   .an_same
    move.b  %d0,(GEN_SONIC_ANIMPREV-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_ANIMSTEP-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_ANIMTIME-GEOS_BIAS)(%a6)
.an_same:
    cmpi.b  #SN_ANI_DUCK,%d0
    beq     .an_duck
    cmpi.b  #SN_ANI_STOP,%d0
    beq     .an_stop
    cmpi.b  #SN_ANI_SPINDASH,%d0
    beq     .an_spindash
    cmpi.b  #SN_ANI_ROLL,%d0
    bne     .an_ground

    move.b  #SN_ROLL_FIRST,%d0          /* the ball, as a jump uses */
    moveq   #0,%d1
    move.b  (GEN_SONIC_ANIMSTEP-GEOS_BIAS)(%a6),%d1
    subq.b  #1,(GEN_SONIC_ANIMTIME-GEOS_BIAS)(%a6)
    bpl.s   .an_ball_hold
    move.b  #1,(GEN_SONIC_ANIMTIME-GEOS_BIAS)(%a6)  /* SonAni_Roll's own interval */
    addq.b  #1,%d1
    cmpi.b  #5,%d1
    blo.s   .an_ball_hold
    moveq   #0,%d1
.an_ball_hold:
    move.b  %d1,(GEN_SONIC_ANIMSTEP-GEOS_BIAS)(%a6)
    add.b   %d1,%d0
    move.b  %d0,(GEN_SONIC_FRAME-GEOS_BIAS)(%a6)
    bra     .an_done

/* SonAni_Duck, `3F 39 FF`: one cel, and the duration only matters because
 * there is nothing to advance to. */
.an_duck:
    move.b  #SN_DUCK_FRAME,(GEN_SONIC_FRAME-GEOS_BIAS)(%a6)
    bra     .an_done

/* SonAni_Stop, `07 37 38 FF`: two cels, eight frames each, looping for as long
 * as the skid lasts -- which is four frames from top speed, so in practice he
 * shows the first of them and is walking again. */
.an_stop:
    subq.b  #1,(GEN_SONIC_ANIMTIME-GEOS_BIAS)(%a6)
    bpl.s   .an_stop_hold
    move.b  #SN_STOP_DUR,(GEN_SONIC_ANIMTIME-GEOS_BIAS)(%a6)
    eori.b  #1,(GEN_SONIC_ANIMSTEP-GEOS_BIAS)(%a6)
.an_stop_hold:
    move.b  #SN_STOP1,%d0
    move.b  (GEN_SONIC_ANIMSTEP-GEOS_BIAS)(%a6),%d1
    andi.w  #1,%d1
    add.b   %d1,%d0
    move.b  %d0,(GEN_SONIC_FRAME-GEOS_BIAS)(%a6)
    bra     .an_done

/* The charge.  Sonic 1's roll cels at the spindash's own rate -- a cel a
 * frame, no timer, which is what a duration byte of zero means and is faster
 * than any roll his inertia can buy him. */
.an_spindash:
    moveq   #0,%d1
    move.b  (GEN_SONIC_ANIMSTEP-GEOS_BIAS)(%a6),%d1
    addq.b  #1,%d1
    cmpi.b  #SN_ROLL_CELS,%d1
    blo.s   .an_spin_hold
    moveq   #0,%d1
.an_spin_hold:
    move.b  %d1,(GEN_SONIC_ANIMSTEP-GEOS_BIAS)(%a6)
    addi.b  #SN_ROLL_FIRST,%d1
    move.b  %d1,(GEN_SONIC_FRAME-GEOS_BIAS)(%a6)
    bra     .an_done

.an_ground:
    move.w  (GEN_SONIC_INERTIA-GEOS_BIAS)(%a6),%d2
    bpl.s   .an_pos
    neg.w   %d2
.an_pos:
    tst.w   %d2
    bne.s   .an_moving
    /* Standing.  One frame, no timer. */
    move.b  #SN_STAND_FRAME,(GEN_SONIC_FRAME-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_ANIMSTEP-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_ANIMTIME-GEOS_BIAS)(%a6)
    bra.s   .an_done
.an_moving:
    subq.b  #1,(GEN_SONIC_ANIMTIME-GEOS_BIAS)(%a6)
    bpl     .an_frame                   /* still on this cel */
    move.w  %d2,%d3
    neg.w   %d3
    addi.w  #0x800,%d3
    bpl.s   .an_dur_ok
    moveq   #0,%d3
.an_dur_ok:
    lsr.w   #8,%d3
    move.b  %d3,(GEN_SONIC_ANIMTIME-GEOS_BIAS)(%a6)
    moveq   #0,%d1
    move.b  (GEN_SONIC_ANIMSTEP-GEOS_BIAS)(%a6),%d1
    addq.b  #1,%d1
    move.b  %d1,(GEN_SONIC_ANIMSTEP-GEOS_BIAS)(%a6)
.an_frame:
    moveq   #0,%d1
    move.b  (GEN_SONIC_ANIMSTEP-GEOS_BIAS)(%a6),%d1
    cmpi.w  #SN_TOP,%d2
    blo.s   .an_walk
    andi.w  #3,%d1                      /* SonAni_Run: four cels */
    move.b  %d1,(GEN_SONIC_ANIMSTEP-GEOS_BIAS)(%a6)
    lea     sonic_ani_run,%a0
    bra.s   .an_pick
.an_walk:
    cmpi.b  #6,%d1                      /* SonAni_Walk: six cels */
    blo.s   .an_walk_ok
    moveq   #0,%d1
    move.b  %d1,(GEN_SONIC_ANIMSTEP-GEOS_BIAS)(%a6)
.an_walk_ok:
    lea     sonic_ani_walk,%a0
.an_pick:
    move.b  (%a0,%d1.w),(GEN_SONIC_FRAME-GEOS_BIAS)(%a6)
.an_done:
    movem.l (%sp)+,%d0-%d3/%a0
    rts

/* ------------------------------------------------------------------ the dust
 *
 * Object 08, and it does two jobs in the cartridge.
 *
 * Obj08_MdSpindashDust keeps one of them on Sonic while spindash_flag is set,
 * at his own x and y and with his own x flip, running Obj08Ani_Dash.  That one
 * is the cloud under the charge.
 *
 * Obj08_CheckSkid watches the character's animation -- "cmpi.b #AniIDSonAni_Stop,
 * anim(a2)" -- and every four frames allocates a *fresh* object at his x and
 * y+$10 running Obj08Ani_Skid, four cels and then gone.  Those are the puffs
 * kicked up behind a brake.
 *
 * The two never coexist: charging wants inertia zero and skidding wants $400
 * of it.  That is why one strip of VRAM serves both, and why the cartridge can
 * point every instance of the object at the same art window. */
sonic_dust_step:
    movem.l %d0-%d3/%a0,-(%sp)
    move.b  #0xFF,(GEN_DUST_WANT-GEOS_BIAS)(%a6)    /* nothing to have in VRAM yet */

    /* The charge cloud.  Obj08_MdSpindashDust drops the object the moment the
     * flag clears, so this does too. */
    tst.b   (GEN_SONIC_SPIN_FLAG-GEOS_BIAS)(%a6)
    beq.s   .dt_no_dash
    cmpi.b  #SN_DUST_DASH,(GEN_DUST_ANIM-GEOS_BIAS)(%a6)
    beq.s   .dt_dash_run
    move.b  #SN_DUST_DASH,(GEN_DUST_ANIM-GEOS_BIAS)(%a6)
    clr.b   (GEN_DUST_STEP-GEOS_BIAS)(%a6)
    clr.b   (GEN_DUST_TIME-GEOS_BIAS)(%a6)
.dt_dash_run:
    subq.b  #1,(GEN_DUST_TIME-GEOS_BIAS)(%a6)
    bpl.s   .dt_dash_hold
    move.b  #SN_DUST_DASH_DUR,(GEN_DUST_TIME-GEOS_BIAS)(%a6)
    addq.b  #1,(GEN_DUST_STEP-GEOS_BIAS)(%a6)
    cmpi.b  #SN_DUST_DASH_CELS,(GEN_DUST_STEP-GEOS_BIAS)(%a6)
    blo.s   .dt_dash_hold
    clr.b   (GEN_DUST_STEP-GEOS_BIAS)(%a6)
.dt_dash_hold:
    move.b  (GEN_DUST_STEP-GEOS_BIAS)(%a6),%d0
    addi.b  #SN_DUST_DASH_1ST,%d0
    move.b  %d0,(GEN_DUST_FRAME-GEOS_BIAS)(%a6)
    move.b  %d0,(GEN_DUST_WANT-GEOS_BIAS)(%a6)
    bra.s   .dt_puffs
.dt_no_dash:
    clr.b   (GEN_DUST_ANIM-GEOS_BIAS)(%a6)

.dt_puffs:
    /* Obj08_CheckSkid, and its timer, which only runs while he is braking. */
    cmpi.b  #SN_ANI_STOP,(GEN_SONIC_ANIM-GEOS_BIAS)(%a6)
    bne.s   .dt_no_skid
    subq.b  #1,(GEN_DUST_TIMER-GEOS_BIAS)(%a6)
    bpl.s   .dt_no_spawn
    move.b  #SN_DUST_SPAWN,(GEN_DUST_TIMER-GEOS_BIAS)(%a6)
    bsr     sonic_dust_spawn
    bra.s   .dt_no_spawn
.dt_no_skid:
    clr.b   (GEN_DUST_TIMER-GEOS_BIAS)(%a6)
.dt_no_spawn:

    /* One tick of every puff alight. */
    lea     (GEN_DUST_PUFFS-GEOS_BIAS)(%a6),%a0
    moveq   #(SN_DUST_PUFFS-1),%d3
.dt_puff:
    cmpi.b  #0xFF,SN_DP_STEP(%a0)
    beq.s   .dt_puff_next
    subq.b  #1,SN_DP_TIME(%a0)
    bpl.s   .dt_puff_show
    move.b  #SN_DUST_SKID_DUR,SN_DP_TIME(%a0)
    addq.b  #1,SN_DP_STEP(%a0)
    cmpi.b  #SN_DUST_SKID_CELS,SN_DP_STEP(%a0)
    blo.s   .dt_puff_show
    move.b  #0xFF,SN_DP_STEP(%a0)       /* smpsStop's equivalent: it is gone */
    bra.s   .dt_puff_next
.dt_puff_show:
    cmpi.b  #0xFF,(GEN_DUST_WANT-GEOS_BIAS)(%a6)
    bne.s   .dt_puff_next               /* the charge already named the art */
    move.b  SN_DP_STEP(%a0),%d0
    addi.b  #SN_DUST_SKID_1ST,%d0
    move.b  %d0,(GEN_DUST_WANT-GEOS_BIAS)(%a6)      /* all four skid cels share one cue */
.dt_puff_next:
    lea     SN_DP_SIZE(%a0),%a0
    dbra    %d3,.dt_puff
    movem.l (%sp)+,%d0-%d3/%a0
    rts

/* AllocateObject, near enough: the first free slot, and none if there is not
 * one.  The cartridge drops the puff in that case too ("bne.s loc_1DEE0"). */
sonic_dust_spawn:
    movem.l %d0-%d1/%a0,-(%sp)
    lea     (GEN_DUST_PUFFS-GEOS_BIAS)(%a6),%a0
    moveq   #(SN_DUST_PUFFS-1),%d1
.ds_slot:
    cmpi.b  #0xFF,SN_DP_STEP(%a0)
    beq.s   .ds_have
    lea     SN_DP_SIZE(%a0),%a0
    dbra    %d1,.ds_slot
    bra.s   .ds_out
.ds_have:
    move.l  (GEN_SONIC_X-GEOS_BIAS)(%a6),%d0
    swap    %d0
    move.w  %d0,SN_DP_X(%a0)            /* the level position, not the screen's */
    move.l  (GEN_SONIC_Y-GEOS_BIAS)(%a6),%d0
    swap    %d0
    addi.w  #SN_DUST_DROP,%d0           /* "addi.w #$10,y_pos(a1)" */
    move.w  %d0,SN_DP_Y(%a0)
    clr.b   SN_DP_STEP(%a0)
    move.b  #SN_DUST_SKID_DUR,SN_DP_TIME(%a0)
.ds_out:
    movem.l (%sp)+,%d0-%d1/%a0
    rts

/* D2.w = how many SAT entries the dust wants this frame, which the chain has
 * to know before any of it is written.  Only D2 is touched. */
sonic_dust_count:
    movem.l %d0/%d1/%a0-%a1,-(%sp)
    moveq   #0,%d2
    tst.b   (GEN_DUST_ANIM-GEOS_BIAS)(%a6)
    beq.s   .dc_puffs
    moveq   #0,%d0
    move.b  (GEN_DUST_FRAME-GEOS_BIAS)(%a6),%d0
    bsr     sonic_dust_record
    beq.s   .dc_puffs
    move.b  (%a0),%d2
    andi.w  #0xFF,%d2
.dc_puffs:
    lea     (GEN_DUST_PUFFS-GEOS_BIAS)(%a6),%a1
    moveq   #(SN_DUST_PUFFS-1),%d1
.dc_slot:
    cmpi.b  #0xFF,SN_DP_STEP(%a1)
    beq.s   .dc_next
    moveq   #0,%d0
    move.b  SN_DP_STEP(%a1),%d0
    addi.w  #SN_DUST_SKID_1ST,%d0
    bsr     sonic_dust_record
    beq.s   .dc_next
    moveq   #0,%d0
    move.b  (%a0),%d0
    add.w   %d0,%d2
.dc_next:
    lea     SN_DP_SIZE(%a1),%a1
    dbra    %d1,.dc_slot
    move.b  %d2,(GEN_DUST_PIECES-GEOS_BIAS)(%a6)
    movem.l (%sp)+,%d0/%d1/%a0-%a1
    rts

/* Every puff put out and the cloud forgotten. */
sonic_dust_off:
    movem.l %d0/%a0,-(%sp)
    clr.b   (GEN_DUST_ANIM-GEOS_BIAS)(%a6)
    clr.b   (GEN_DUST_TIMER-GEOS_BIAS)(%a6)
    clr.b   (GEN_DUST_PIECES-GEOS_BIAS)(%a6)
    move.b  #0xFF,(GEN_DUST_SHOWN-GEOS_BIAS)(%a6)
    move.b  #0xFF,(GEN_DUST_WANT-GEOS_BIAS)(%a6)
    lea     (GEN_DUST_PUFFS-GEOS_BIAS)(%a6),%a0
    moveq   #(SN_DUST_PUFFS-1),%d0
.do_slot:
    move.b  #0xFF,SN_DP_STEP(%a0)
    lea     SN_DP_SIZE(%a0),%a0
    dbra    %d0,.do_slot
    movem.l (%sp)+,%d0/%a0
    rts

/* ------------------------------------------------------------- the sign post */

/* Object 0D, routine for routine.
 *
 *   Sign_Touch     he is between the post and 32 pixels to its right
 *   Sign_Spin      three one-second cycles, Eggman's face turning to Sonic's
 *   Sign_SonicRun  controls locked, held right, until he is off the screen
 *
 * The last one is where the accessory closes: in the game that is the act clear
 * card, and here it is handing the desktop back. */
sonic_sign_step:
    movem.l %d0-%d2,-(%sp)
    move.b  (GEN_SIGN_ROUTINE-GEOS_BIAS)(%a6),%d0
    beq.s   .sn_touch
    cmpi.b  #2,%d0
    beq.s   .sn_spin
    cmpi.b  #4,%d0
    beq.s   .sn_runout
    bra     .sn_anim

.sn_touch:
    /* "get Sonic's X-position / subtract signpost's X-position / if Sonic is to
     * the left of the signpost, branch / is Sonic within 32 pixels of it" */
    move.l  (GEN_SONIC_X-GEOS_BIAS)(%a6),%d0
    swap    %d0
    sub.w   (GEN_SONIC_POSTX-GEOS_BIAS)(%a6),%d0
    blo     .sn_anim
    cmpi.w  #SIGN_REACH,%d0
    bhs     .sn_anim
    move.b  #2,(GEN_SIGN_ROUTINE-GEOS_BIAS)(%a6)
    clr.w   (GEN_SIGN_SPINTIME-GEOS_BIAS)(%a6)
    bsr     sonic_fm_signpost           /* "move.w #sfx_Signpost,d0 / jsr
                                         * QueueSound1" is here in Sign_Touch */
    bra     .sn_anim

.sn_spin:
    subq.w  #1,(GEN_SIGN_SPINTIME-GEOS_BIAS)(%a6)
    bpl     .sn_anim
    move.w  #SIGN_SPIN_TIME,(GEN_SIGN_SPINTIME-GEOS_BIAS)(%a6)
    addq.b  #1,(GEN_SIGN_ANIM-GEOS_BIAS)(%a6)
    cmpi.b  #3,(GEN_SIGN_ANIM-GEOS_BIAS)(%a6)
    bne     .sn_anim
    move.b  #4,(GEN_SIGN_ROUTINE-GEOS_BIAS)(%a6)
    bra     .sn_anim

.sn_runout:
    /* Sign_SonicRun, and now it is the routine the cartridge wrote:
     *
     *      move.b  #1,(f_lockctrl).w
     *      move.w  #btnR<<8,(v_jpadhold2).w
     *
     * The pad is taken away here, after the post's three cycles -- not at the
     * touch.  It had to be moved to the touch while there was one screen and no
     * camera, because with nothing scrolling he would have stood beside the post
     * for three seconds and only then set off, which reads as a pause.  With the
     * camera following him the original's order is the one that looks right: he
     * runs on past, the post spins behind him, and the act ends when it has
     * finished turning and he is over the right boundary. */
    move.b  #1,(GEN_SONIC_LOCK-GEOS_BIAS)(%a6)      /* "lock controls" */
    move.l  (GEN_SONIC_X-GEOS_BIAS)(%a6),%d0
    swap    %d0
    cmpi.w  #(SN_LEVEL_W+24),%d0
    blt.s   .sn_anim
    move.b  #6,(GEN_SIGN_ROUTINE-GEOS_BIAS)(%a6)
    move.b  #1,(GEN_SONIC_DONE-GEOS_BIAS)(%a6)

.sn_anim:
    bsr     sonic_sign_animate
    movem.l (%sp)+,%d0-%d2
    rts

/* AnimateSprite over Ani_Sign: a duration byte, then frame numbers, then $FF to
 * start the script again.  Changing script resets the cel and the timer, which
 * is what the `cmp.b obPrevAnim` at the top of AnimateSprite does. */
sonic_sign_animate:
    movem.l %d0-%d2/%a0,-(%sp)
    moveq   #0,%d0
    move.b  (GEN_SIGN_ANIM-GEOS_BIAS)(%a6),%d0
    cmp.b   (GEN_SIGN_PREVANIM-GEOS_BIAS)(%a6),%d0
    beq.s   .sa_same
    move.b  %d0,(GEN_SIGN_PREVANIM-GEOS_BIAS)(%a6)
    clr.b   (GEN_SIGN_ANIMSTEP-GEOS_BIAS)(%a6)
    clr.b   (GEN_SIGN_ANIMTIME-GEOS_BIAS)(%a6)
.sa_same:
    subq.b  #1,(GEN_SIGN_ANIMTIME-GEOS_BIAS)(%a6)
    bpl.s   .sa_out
    add.w   %d0,%d0
    lea     sign_ani_table,%a0
    adda.w  (%a0,%d0.w),%a0
    move.b  (%a0)+,(GEN_SIGN_ANIMTIME-GEOS_BIAS)(%a6)   /* the script's own duration */
    moveq   #0,%d1
    move.b  (GEN_SIGN_ANIMSTEP-GEOS_BIAS)(%a6),%d1
    move.b  (%a0,%d1.w),%d2
    cmpi.b  #0xFF,%d2                       /* afEnd: back to the first cel */
    bne.s   .sa_have
    moveq   #0,%d1
    move.b  (%a0),%d2
.sa_have:
    move.b  %d2,(GEN_SIGN_FRAME-GEOS_BIAS)(%a6)
    addq.b  #1,%d1
    move.b  %d1,(GEN_SIGN_ANIMSTEP-GEOS_BIAS)(%a6)
.sa_out:
    movem.l (%sp)+,%d0-%d2/%a0
    rts

/* ------------------------------------------------------------- the graphics */

/* Called from the VBlank interrupt: if the frame changed, walk its DPLC runs
 * and copy those tiles into Sonic's VRAM slot.  A frame is at most 23 tiles,
 * so this is at most 736 bytes -- comfortable inside the blanking interval. */
geos_sonic_vblank:
    tst.b   (GEN_SONIC_ACTIVE-GEOS_BIAS)(%a6)
    beq     .sv_out
    bsr     sonic_hscroll               /* the page moves here and nowhere else */
    tst.b   (GEN_SIGN_UPLOAD-GEOS_BIAS)(%a6)
    beq.s   .sv_sign_done
    bsr     sonic_sign_upload
.sv_sign_done:
    move.b  (GEN_DUST_WANT-GEOS_BIAS)(%a6),%d0
    cmp.b   (GEN_DUST_SHOWN-GEOS_BIAS)(%a6),%d0
    beq.s   .sv_dust_done
    move.b  %d0,(GEN_DUST_SHOWN-GEOS_BIAS)(%a6)
    cmpi.b  #0xFF,%d0
    beq.s   .sv_dust_done
    bsr     sonic_dust_upload
.sv_dust_done:
    move.b  (GEN_SONIC_FRAME-GEOS_BIAS)(%a6),%d0
    cmp.b   (GEN_SONIC_SHOWN-GEOS_BIAS)(%a6),%d0
    beq     .sv_sat
    movem.l %d0-%d7/%a0-%a2,-(%sp)
    move.b  %d0,(GEN_SONIC_SHOWN-GEOS_BIAS)(%a6)
    andi.w  #0xFF,%d0
    bsr     sonic_frame_record
    beq     .sv_restore
    movea.l %a0,%a2
    move.b  (%a2),(GEN_SONIC_PIECES-GEOS_BIAS)(%a6)
    moveq   #0,%d7
    move.b  1(%a2),%d7                  /* DPLC entry count */
    lea     2(%a2),%a2
    movea.l sn_blob,%a1
    move.l  4(%a1),%d6
    lea     (%a1,%d6.l),%a1             /* the art pool */
    move.w  #(SONIC_TILE*32),%d0
    jsr     vdp_set_vram_write
    subq.w  #1,%d7
    bmi     .sv_restore
.sv_run:
    moveq   #0,%d1
    move.w  (%a2)+,%d0
    move.w  %d0,%d1
    andi.l  #0x00000FFF,%d1             /* art tile -- the whole register, since
                                         * the shift below is a long one */
    lsr.w   #8,%d0
    lsr.w   #4,%d0
    andi.w  #0x000F,%d0
    addq.w  #1,%d0                      /* tiles in this run */
    lsl.l   #5,%d1
    movea.l %a1,%a0
    adda.l  %d1,%a0
    subq.w  #1,%d0
.sv_tile:
    moveq   #15,%d2                     /* 32 bytes = 16 words */
.sv_word:
    move.w  (%a0)+,VDP_DATA
    dbra    %d2,.sv_word
    dbra    %d0,.sv_tile
    dbra    %d7,.sv_run
.sv_restore:
    movem.l (%sp)+,%d0-%d7/%a0-%a2
.sv_sat:
    /* And his sprite entries, every frame.  The pointer's own SAT refresh is
     * event-driven -- it runs when the mouse moves or a sprite is enabled --
     * and he moves whether or not anything touched the pad. */
    bsr     geos_sonic_sat
.sv_out:
    rts

/* One dust cel's cues into its own window.  D0.b = the mapping frame.  Sixteen
 * tiles at the very most, and only when the cel changed -- which for the skid
 * is once every four frames, since its four cels are one cue between them. */
sonic_dust_upload:
    movem.l %d0-%d3/%a0-%a2,-(%sp)
    andi.w  #0xFF,%d0
    bsr     sonic_dust_record
    beq.s   .du_out
    movea.l %a0,%a2
    moveq   #0,%d3
    move.b  1(%a2),%d3                  /* DPLC entry count */
    lea     2(%a2),%a2
    movea.l sn_blob,%a1
    move.l  0x3C(%a1),%d0
    lea     (%a1,%d0.l),%a1             /* object 08's art */
    move.w  #(DUST_TILE*32),%d0
    jsr     vdp_set_vram_write
    subq.w  #1,%d3
    bmi.s   .du_out
.du_run:
    moveq   #0,%d1
    move.w  (%a2)+,%d0
    move.w  %d0,%d1
    andi.l  #0x00000FFF,%d1
    lsr.w   #8,%d0
    lsr.w   #4,%d0
    andi.w  #0x000F,%d0
    addq.w  #1,%d0
    lsl.l   #5,%d1
    movea.l %a1,%a0
    adda.l  %d1,%a0
    subq.w  #1,%d0
.du_tile:
    moveq   #15,%d2
.du_word:
    move.w  (%a0)+,VDP_DATA
    dbra    %d2,.du_word
    dbra    %d0,.du_tile
    dbra    %d3,.du_run
.du_out:
    movem.l (%sp)+,%d0-%d3/%a0-%a2
    rts

/* The sign post's 58 tiles, a slice at a time.  GEN_SIGN_UPLOAD counts down the
 * tiles still to go, so the whole thing lands within the first four VBlanks --
 * long before he has fallen far enough to see it. */
sonic_sign_upload:
    movem.l %d0-%d3/%a0-%a1,-(%sp)
    movea.l sn_blob,%a1
    move.w  0x2E(%a1),%d3               /* tiles in the sign post's art */
    moveq   #0,%d0
    move.b  (GEN_SIGN_UPLOAD-GEOS_BIAS)(%a6),%d0
    move.w  %d3,%d1
    sub.w   %d0,%d1                     /* the first tile still to write */
    move.l  0x30(%a1),%d2
    lea     (%a1,%d2.l),%a0
    move.w  %d1,%d2
    lsl.w   #5,%d2
    adda.w  %d2,%a0
    add.w   #SIGN_TILE,%d1
    lsl.w   #5,%d1
    move.w  %d1,%d0
    jsr     vdp_set_vram_write
    moveq   #0,%d0
    move.b  (GEN_SIGN_UPLOAD-GEOS_BIAS)(%a6),%d0
    cmpi.w  #SIGN_UPLOAD_STEP,%d0
    blo.s   .su_have
    moveq   #SIGN_UPLOAD_STEP,%d0
.su_have:
    sub.b   %d0,(GEN_SIGN_UPLOAD-GEOS_BIAS)(%a6)
    subq.w  #1,%d0
.su_tile:
    moveq   #15,%d1
.su_word:
    move.w  (%a0)+,VDP_DATA
    dbra    %d1,.su_word
    dbra    %d0,.su_tile
    movem.l (%sp)+,%d0-%d3/%a0-%a1
    rts

/* Sonic's SAT entries, and the sign post's, written by the sprite update after
 * the app sprites.  The chain has already been linked into entry 8 by the
 * caller when GEN_SONIC_ACTIVE is set, so everything from here on is one run of
 * consecutive entries ending in a link of zero. */
geos_sonic_sat:
    movem.l %d0-%d7/%a0-%a2,-(%sp)
    tst.b   (GEN_SONIC_ACTIVE-GEOS_BIAS)(%a6)
    beq     .ss_done

    /* How many entries between them, so the last one can end the chain. */
    moveq   #0,%d0
    move.b  (GEN_SONIC_FRAME-GEOS_BIAS)(%a6),%d0
    bsr     sonic_frame_record
    beq     .ss_done
    moveq   #0,%d6
    move.b  (%a0),%d6
    move.b  %d6,(GEN_SONIC_PIECES-GEOS_BIAS)(%a6)
    moveq   #0,%d1
    tst.b   (GEN_SIGN_UPLOAD-GEOS_BIAS)(%a6)
    bne.s   .ss_counted                 /* its tiles are not all in VRAM yet */
    moveq   #0,%d0
    move.b  (GEN_SIGN_FRAME-GEOS_BIAS)(%a6),%d0
    bsr     sonic_sign_record
    beq.s   .ss_counted
    /* Off the side of the screen it is not drawn at all.  The SAT's X field is
     * nine bits, so a post standing 1240 pixels into the level would not simply
     * be out of sight -- it would wrap round and stand in the middle of the
     * desktop from the first frame.  This is the game's own display-range test
     * on an object, at the one place where it matters. */
    move.w  (GEN_SONIC_POSTX-GEOS_BIAS)(%a6),%d2
    sub.w   (GEN_SONIC_CAMX-GEOS_BIAS)(%a6),%d2
    cmpi.w  #-64,%d2                    /* a sign's width of slack either side */
    blt.s   .ss_counted
    cmpi.w  #(SCREEN_WIDTH+64),%d2
    bgt.s   .ss_counted
    move.b  (%a0),%d1
.ss_counted:
    move.b  %d1,(GEN_SIGN_PIECES-GEOS_BIAS)(%a6)
    bsr     sonic_dust_count            /* D2 = what the dust will want */
    moveq   #SONIC_SAT_FIRST,%d3
    move.w  %d3,%d4
    add.w   %d6,%d4
    add.w   %d1,%d4
    add.w   %d2,%d4
    subq.w  #1,%d4                      /* the last SAT index this frame uses */

    /* Him first. */
    move.l  (GEN_SONIC_X-GEOS_BIAS)(%a6),%d0
    swap    %d0
    sub.w   (GEN_SONIC_CAMX-GEOS_BIAS)(%a6),%d0     /* the camera, at last */
    move.w  %d0,(GEN_SONIC_SCRX-GEOS_BIAS)(%a6)
    move.l  (GEN_SONIC_Y-GEOS_BIAS)(%a6),%d0
    swap    %d0
    move.w  %d0,(GEN_SONIC_SCRY-GEOS_BIAS)(%a6)
    move.b  (GEN_SONIC_FACING-GEOS_BIAS)(%a6),(GEN_SONIC_EMIT_FLIP-GEOS_BIAS)(%a6)
    move.w  #SONIC_TILE,(GEN_SONIC_EMIT_TILE-GEOS_BIAS)(%a6)
    moveq   #0,%d0
    move.b  (GEN_SONIC_FRAME-GEOS_BIAS)(%a6),%d0
    bsr     sonic_frame_record
    beq.s   .ss_sign
    movea.l %a0,%a2
    moveq   #0,%d7
    move.b  (%a2),%d7
    moveq   #0,%d5
    move.b  1(%a2),%d5
    lea     2(%a2),%a2
    add.w   %d5,%d5
    adda.w  %d5,%a2                     /* skip the DPLC entries */
    bsr     sonic_emit_pieces

    /* Then the post, which never flips as a whole and indexes its own art. */
.ss_sign:
    tst.b   (GEN_SIGN_PIECES-GEOS_BIAS)(%a6)
    beq.s   .ss_dust
    tst.b   (GEN_SIGN_UPLOAD-GEOS_BIAS)(%a6)
    bne.s   .ss_dust                    /* its tiles are not all in VRAM yet */
    move.w  (GEN_SONIC_POSTX-GEOS_BIAS)(%a6),%d0
    sub.w   (GEN_SONIC_CAMX-GEOS_BIAS)(%a6),%d0     /* it stands in the level, not on the screen */
    move.w  %d0,(GEN_SONIC_SCRX-GEOS_BIAS)(%a6)
    move.w  (GEN_SONIC_POSTY-GEOS_BIAS)(%a6),(GEN_SONIC_SCRY-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_EMIT_FLIP-GEOS_BIAS)(%a6)
    move.w  #SIGN_TILE,(GEN_SONIC_EMIT_TILE-GEOS_BIAS)(%a6)
    moveq   #0,%d0
    move.b  (GEN_SIGN_FRAME-GEOS_BIAS)(%a6),%d0
    bsr     sonic_sign_record
    beq.s   .ss_dust
    movea.l %a0,%a2
    moveq   #0,%d7
    move.b  (%a2),%d7
    lea     2(%a2),%a2                  /* Map_Sign carries no DPLC entries */
    bsr     sonic_emit_pieces

    /* And the dust last, so it is behind everything: a SAT entry is drawn in
     * front of every entry after it, and object 08 has priority 1 like Sonic
     * but is loaded after him. */
.ss_dust:
    tst.b   (GEN_DUST_PIECES-GEOS_BIAS)(%a6)
    beq     .ss_done
    tst.b   (GEN_DUST_ANIM-GEOS_BIAS)(%a6)
    beq.s   .ss_puffs
    /* The charge cloud sits exactly where he does, flipped the way he faces. */
    move.l  (GEN_SONIC_X-GEOS_BIAS)(%a6),%d0
    swap    %d0
    sub.w   (GEN_SONIC_CAMX-GEOS_BIAS)(%a6),%d0
    move.w  %d0,(GEN_SONIC_SCRX-GEOS_BIAS)(%a6)
    move.l  (GEN_SONIC_Y-GEOS_BIAS)(%a6),%d0
    swap    %d0
    move.w  %d0,(GEN_SONIC_SCRY-GEOS_BIAS)(%a6)
    move.b  (GEN_SONIC_FACING-GEOS_BIAS)(%a6),(GEN_SONIC_EMIT_FLIP-GEOS_BIAS)(%a6)
    move.w  #DUST_TILE,(GEN_SONIC_EMIT_TILE-GEOS_BIAS)(%a6)
    moveq   #0,%d0
    move.b  (GEN_DUST_FRAME-GEOS_BIAS)(%a6),%d0
    bsr     sonic_dust_record
    beq.s   .ss_puffs
    movea.l %a0,%a2
    moveq   #0,%d7
    move.b  (%a2),%d7
    moveq   #0,%d5
    move.b  1(%a2),%d5
    lea     2(%a2),%a2
    add.w   %d5,%d5
    adda.w  %d5,%a2
    bsr     sonic_emit_pieces

.ss_puffs:
    /* Then each puff, where it was kicked up rather than where he is now. */
    clr.b   (GEN_SONIC_EMIT_FLIP-GEOS_BIAS)(%a6)    /* "move.b #0,status(a1)" */
    move.w  #DUST_TILE,(GEN_SONIC_EMIT_TILE-GEOS_BIAS)(%a6)
    lea     (GEN_DUST_PUFFS-GEOS_BIAS)(%a6),%a1
    moveq   #(SN_DUST_PUFFS-1),%d6
.ss_puff:
    cmpi.b  #0xFF,SN_DP_STEP(%a1)
    beq.s   .ss_puff_next
    move.w  SN_DP_X(%a1),%d0
    sub.w   (GEN_SONIC_CAMX-GEOS_BIAS)(%a6),%d0
    move.w  %d0,(GEN_SONIC_SCRX-GEOS_BIAS)(%a6)
    move.w  SN_DP_Y(%a1),(GEN_SONIC_SCRY-GEOS_BIAS)(%a6)
    moveq   #0,%d0
    move.b  SN_DP_STEP(%a1),%d0
    addi.w  #SN_DUST_SKID_1ST,%d0
    bsr     sonic_dust_record
    beq.s   .ss_puff_next
    movea.l %a0,%a2
    moveq   #0,%d7
    move.b  (%a2),%d7
    moveq   #0,%d5
    move.b  1(%a2),%d5
    lea     2(%a2),%a2
    add.w   %d5,%d5
    adda.w  %d5,%a2
    bsr     sonic_emit_pieces
.ss_puff_next:
    lea     SN_DP_SIZE(%a1),%a1
    dbra    %d6,.ss_puff
.ss_done:
    movem.l (%sp)+,%d0-%d7/%a0-%a2
    rts

/* One object's pieces into consecutive SAT entries.
 *   A2 = the piece bytes          D7 = how many
 *   D3 = the first SAT index      D4 = the last index of the whole chain
 *   GEN_SONIC_SCRX/SCRY           where the object's origin is
 *   GEN_SONIC_EMIT_FLIP/_TILE     mirror the whole object, and its tile base
 * D3 and A2 come back advanced past what was written. */
sonic_emit_pieces:
    movem.l %d0-%d2/%d5-%d6,-(%sp)
    subq.w  #1,%d7
    bmi     .ep_out
.ep_piece:
    move.w  %d3,%d0
    lsl.w   #3,%d0
    addi.w  #SAT_BASE,%d0
    jsr     vdp_set_vram_write

    move.b  (%a2)+,%d0                  /* y offset, signed */
    ext.w   %d0
    moveq   #0,%d1
    move.b  (%a2)+,%d1                  /* size: (w-1)<<2 | (h-1) */
    /* The tile word, a byte at a time.  A spritePiece is five bytes -- the
     * cartridge's own record -- so every second piece starts on an odd address
     * and this field lands on one.  A 68000 takes an address error on a word
     * read there and stops the machine; PicoDrive and Genesis Plus GX both let
     * it through, so this was invisible in every emulator run and immediate on
     * real hardware: MS_Stand's piece 1 faulted at $02841F the instant he was
     * asked to draw. */
    moveq   #0,%d2
    move.b  (%a2)+,%d2
    lsl.w   #8,%d2
    moveq   #0,%d6
    move.b  (%a2)+,%d6
    or.w    %d6,%d2                     /* tile and flags */
    move.b  (%a2)+,%d6                  /* x offset, signed */
    ext.w   %d6

    /* Facing left mirrors the whole frame, which is what obRender's xflip bit
     * does in the game: every piece flips and its x reflects about the origin. */
    tst.b   (GEN_SONIC_EMIT_FLIP-GEOS_BIAS)(%a6)
    beq.s   .ep_noflip
    eori.w  #0x0800,%d2
    move.w  %d1,%d5
    lsr.w   #2,%d5
    andi.w  #3,%d5
    addq.w  #1,%d5
    lsl.w   #3,%d5                      /* the piece's width in pixels */
    neg.w   %d6
    sub.w   %d5,%d6
.ep_noflip:
    /* Y */
    add.w   (GEN_SONIC_SCRY-GEOS_BIAS)(%a6),%d0
    addi.w  #(128+GEOS_DISPLAY_Y),%d0
    move.w  %d0,VDP_DATA

    /* Size and link.  The piece's size byte is already in the SAT's own layout
     * once it is shifted up a byte: (w-1) lands in bits 11-10 and (h-1) in
     * bits 9-8. */
    move.w  %d1,%d0
    andi.w  #0x000F,%d0
    lsl.w   #8,%d0
    move.w  %d3,%d5
    addq.w  #1,%d5                      /* link on to the next piece... */
    cmp.w   %d4,%d5
    ble.s   .ep_link_ok
    moveq   #0,%d5                      /* ...except the last, which ends it */
.ep_link_ok:
    or.w    %d5,%d0
    move.w  %d0,VDP_DATA

    /* Attribute: the piece's own flip bits, this port's palette line, and the
     * tile offset by wherever the object's art was loaded. */
    move.w  %d2,%d0
    andi.w  #0x1800,%d0                 /* vflip and hflip */
    ori.w   #(SONIC_PAL_LINE<<13),%d0
    move.w  %d2,%d1
    andi.w  #0x07FF,%d1
    add.w   (GEN_SONIC_EMIT_TILE-GEOS_BIAS)(%a6),%d1
    or.w    %d1,%d0
    move.w  %d0,VDP_DATA

    /* X */
    move.w  %d6,%d0
    add.w   (GEN_SONIC_SCRX-GEOS_BIAS)(%a6),%d0
    addi.w  #128,%d0
    move.w  %d0,VDP_DATA

    addq.w  #1,%d3
    dbra    %d7,.ep_piece
.ep_out:
    movem.l (%sp)+,%d0-%d2/%d5-%d6
    rts

/* ----------------------------------------------------------------- the loop */

geos_sonic_run:
    movem.l %d0-%d7/%a0-%a4,-(%sp)
    /* Start above the screen and fall in. */
    move.l  #(160<<16),(GEN_SONIC_X-GEOS_BIAS)(%a6)
    move.l  #((-40)<<16),(GEN_SONIC_Y-GEOS_BIAS)(%a6)
    clr.w   (GEN_SONIC_XSP-GEOS_BIAS)(%a6)
    clr.w   (GEN_SONIC_YSP-GEOS_BIAS)(%a6)
    clr.w   (GEN_SONIC_INERTIA-GEOS_BIAS)(%a6)
    move.b  #1,(GEN_SONIC_AIR-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_JUMPING-GEOS_BIAS)(%a6)
    move.b  #SN_HEIGHT,(GEN_SONIC_HEIGHT-GEOS_BIAS)(%a6)
    move.b  #SN_WIDTH,(GEN_SONIC_WIDTH-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_FACING-GEOS_BIAS)(%a6)
    move.b  #SN_STAND_FRAME,(GEN_SONIC_FRAME-GEOS_BIAS)(%a6)
    move.b  #0xFF,(GEN_SONIC_SHOWN-GEOS_BIAS)(%a6)  /* force the first tile upload */
    clr.b   (GEN_SONIC_ANIMSTEP-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_ANIMTIME-GEOS_BIAS)(%a6)
    move.b  #SN_ANI_WALK,(GEN_SONIC_ANIM-GEOS_BIAS)(%a6)
    move.b  #0xFF,(GEN_SONIC_ANIMPREV-GEOS_BIAS)(%a6)   /* nothing matches, so the first
                                             * frame starts a script cleanly */
    bsr     sonic_dust_off
    clr.b   (GEN_SONIC_DONE-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_LOCK-GEOS_BIAS)(%a6)
    clr.l   (GEN_SONIC_TICKS-GEOS_BIAS)(%a6)
    clr.w   (GEN_SONIC_PAD-GEOS_BIAS)(%a6)
    clr.l   (GEN_SONIC_FM_TRACKS-GEOS_BIAS)(%a6)
    clr.l   ((GEN_SONIC_FM_TRACKS-GEOS_BIAS)+4)(%a6)

    /* The sign post, waiting in the bottom right corner with Eggman's face on. */
    move.w  #SIGN_X,(GEN_SONIC_POSTX-GEOS_BIAS)(%a6)
    move.w  #SIGN_Y,(GEN_SONIC_POSTY-GEOS_BIAS)(%a6)
    clr.b   (GEN_SIGN_ROUTINE-GEOS_BIAS)(%a6)
    clr.b   (GEN_SIGN_ANIM-GEOS_BIAS)(%a6)
    clr.b   (GEN_SIGN_PREVANIM-GEOS_BIAS)(%a6)
    clr.b   (GEN_SIGN_ANIMSTEP-GEOS_BIAS)(%a6)
    clr.b   (GEN_SIGN_ANIMTIME-GEOS_BIAS)(%a6)
    clr.b   (GEN_SIGN_FRAME-GEOS_BIAS)(%a6)
    clr.b   (GEN_SIGN_PIECES-GEOS_BIAS)(%a6)
    clr.w   (GEN_SIGN_SPINTIME-GEOS_BIAS)(%a6)
    movea.l sn_blob,%a0
    move.b  0x2F(%a0),(GEN_SIGN_UPLOAD-GEOS_BIAS)(%a6)  /* tiles of its art still to send */

    /* The act, drawn out of the desktop a run of columns at a time. */
    bsr     sonic_deskmap_build
    clr.w   (GEN_SONIC_CAMX-GEOS_BIAS)(%a6)
    clr.w   (GEN_SONIC_CAMCOL-GEOS_BIAS)(%a6)
    bsr     sonic_plane_init

    bsr     sonic_palette
    move.b  #1,(GEN_SONIC_ACTIVE-GEOS_BIAS)(%a6)
    jsr     _MouseOff                   /* the pad is his while he is up */

.run_loop:
    bsr     sn_frame                    /* PORTED. One frame. On GEOS this was
                                         * `stop #$2000' and geos_sonic_vblank
                                         * was an interrupt handler; here the
                                         * vector table is in the Mega CD's boot
                                         * ROM and is nobody's to write, so
                                         * main.S polls the VBlank flag and calls
                                         * the handler itself. Same work, same
                                         * place in the frame. */
    jsr     _DoCheckButtons             /* keeps the desktop's own clock going */
    bsr     sonic_sfx_step              /* one tick of the score, if one is playing */
    bsr     sonic_fm_step               /* and of the signpost, which has its own */
    addq.l  #1,(GEN_SONIC_TICKS-GEOS_BIAS)(%a6)
    move.w  (GEN_M3_PAD_STATE-GEOS_BIAS)(%a6),%d0
    move.w  (GEN_SONIC_PAD-GEOS_BIAS)(%a6),%d1
    move.w  %d0,(GEN_SONIC_PAD-GEOS_BIAS)(%a6)
    not.w   %d1
    and.w   %d0,%d1
    move.w  %d1,(GEN_SONIC_PADEDGE-GEOS_BIAS)(%a6)  /* press edges only */
    /* Once the post has finished turning, Sign_SonicRun takes the pad away and
     * holds right: "move.b #1,(f_lockctrl).w / move.w #btnR<<8,(v_jpadhold2).w". */
    tst.b   (GEN_SONIC_LOCK-GEOS_BIAS)(%a6)
    beq.s   .run_free
    move.w  #(1<<PAD_RIGHT),(GEN_SONIC_PAD-GEOS_BIAS)(%a6)
    clr.w   (GEN_SONIC_PADEDGE-GEOS_BIAS)(%a6)
.run_free:
    bsr     sonic_step
    bsr     sonic_camera
    bsr     sonic_animate
    bsr     sonic_dust_step             /* after the animation, because the skid
                                         * puffs are spawned off `anim` */
    bsr     sonic_sign_step
    /* Start is the way out for anyone who does not want to walk to the post. */
    btst    #PAD_START,%d1
    bne.s   .run_end
    tst.b   (GEN_SONIC_DONE-GEOS_BIAS)(%a6)
    beq     .run_loop
.run_end:
    clr.b   (GEN_SONIC_ACTIVE-GEOS_BIAS)(%a6)
    bsr     sonic_sfx_off               /* never leave the channel sounding */
    bsr     sonic_fm_off                /* nor the FM one */
    bsr     sonic_dust_off
    bsr     sonic_plane_restore         /* the desktop's plane back as it was */
    jsr     _MouseUp
    movem.l (%sp)+,%d0-%d7/%a0-%a4
    rts

/* Sonic's sixteen colours, straight from the cartridge, into palette line 2. */
sonic_palette:
    movem.l %d0-%d2/%a0,-(%sp)
    /* CRAM write, address = line*32.  The VDP command is
     * $C0000000 | (address & $3FFF)<<16, and line 2 is address $40. */
    move.l  #(0xC0000000 + ((SONIC_PAL_LINE*32) << 16)),VDP_CTRL
    movea.l sn_blob,%a0
    lea     12(%a0),%a0                 /* the palette lives in the header */
    moveq   #15,%d0
.sp_loop:
    move.w  (%a0)+,VDP_DATA
    dbra    %d0,.sp_loop
    movem.l (%sp)+,%d0-%d2/%a0
    rts



/* ------------------------------------------------------------- the spindash */

/* Sonic 2's spindash, on Sonic 1's Sonic.  s2disasm/s2.asm,
 * Sonic_CheckSpindash / Sonic_UpdateSpindash / Sonic_ChargingSpindash, and
 * SpindashSpeeds.
 *
 * It is not in this cartridge -- Sonic 1's Obj01 has Sonic_Duck and nothing
 * behind it -- so this is the one piece of the accessory that comes from a
 * different game.  Everything under it does not: the two games' Sonic runs on
 * the same numbers.
 *
 *      son_maxspeed      $600     Sonic_top_speed     $600
 *      son_acceleration  $C       Sonic_acceleration  $C
 *      son_deceleration  $80      Sonic_deceleration  $80
 *      son_jumpspeed     $680     the jump            $680
 *      jump cut          $400     jump cut            $400
 *
 * -- s1disasm/_incObj/01 Sonic.asm:5-8 against s2.asm:36187 and 37355.  Same
 * top speed, same acceleration, same deceleration, same jump, same cut.  So the
 * spindash lands on physics it was written for.
 *
 * The launch is the same shape as the jump's curl, which is the other reason it
 * fits: y_radius $E, x_radius 7, y_pos += 5 -- SN_ROLL_HEIGHT, SN_ROLL_WIDTH
 * and SN_CURL, already here because Sonic 1 rolls the same way.
 *
 * Returns D0 nonzero when it owns the frame.
 */
/* The tail of Sonic_MoveLeft and Sonic_MoveRight, which is the same three
 * instructions in both games and in both directions:
 *
 *      move.b  #$D,anim(a0)            ; use "braking" animation
 *      bclr    #0,render_flags(a0)     ; face the way he is still sliding
 *      move.w  #sfx_Skid,d0
 *      jsr     (PlaySound_Special).l
 *
 * -- Sonic 1 at $0130D2 and $013138, Sonic 2 at $01A71A and $01A79A, and the
 * sound is $A4 in both.  The facing is not repeated here because this port
 * takes it from the sign of the inertia a few instructions later, which on
 * flat ground is the same answer: he is still moving the old way.
 *
 * It plays every frame the condition holds, as it does in the cartridge -- the
 * turn takes four frames from top speed at $80 a frame, so it is four
 * retriggers and not a drone. */

/* THE POSE IS A LATCH, AND THAT IS THE WHOLE OF IT.
 *
 * The $400 test gates where `anim` is *written*.  It does not gate how long
 * the pose lasts, because the branch it guards does not write `anim` at all:
 *
 *      cmpi.w  #$400,d0                ; has Sonic changed direction while
 *      blt.s   .nostopping             ;  being really fast?
 *      move.b  #id_Stop,obAnim(a0)     ; use "stopping" animation
 *      ...
 *  .nostopping:
 *      rts                             ; and obAnim is left exactly as it was
 *
 * -- Sonic 1's Sonic_MoveLeft, and Sonic_MoveRight below it.  Compare the
 * other exit from the same routine, the one taken when he is accelerating the
 * way he already faces, which ends `move.b #id_Walk,obAnim(a0)`.  So `anim`
 * holds id_Stop from the frame it is set until he is actually moving the new
 * way; every remaining frame of the brake goes out through `.nostopping` and
 * leaves it alone.  SonAni_Stop is `dc.b 7, fr_Stop1, fr_Stop2, afEnd` -- two
 * cels at eight frames each -- and it needs those frames to be a skid rather
 * than a flicker.
 *
 * This port cannot inherit that by doing nothing, because it clears `anim` to
 * Walk at the top of every frame (see sonic_step).  The comment there says
 * that "comes to the same thing", and for the duck and the charge it does:
 * they assert their animation every frame they own him.  For the skid it does
 * not, and this is what that cost -- four frames of cel one, never reaching
 * cel two, which on hardware reads as the screech playing over a Sonic who
 * barely skids at all.
 *
 * So the latch is written out.  sn_skid_on is set on the frames the cartridge
 * writes id_Stop, and sn_skid_was carries it into the next frame; a braking
 * frame with nothing to write asserts the pose anyway if the brake had
 * already earned it.  Any frame that is not a braking frame clears it, which
 * is the `move.b #id_Walk` on the other exit.
 *
 * The screech does not change.  It stays on the strict $400 test -- four
 * retriggers from top speed, not a drone -- because in the cartridge that
 * really is the same test, and the sound is meant to stop before he does. */
sonic_skid:
    st.b    sn_skid_on                  /* the brake has earned the pose */
sonic_skid_hold:
    move.b  #SN_ANI_STOP,(GEN_SONIC_ANIM-GEOS_BIAS)(%a6)
    bra     sonic_sfx_skid              /* the caller is mid-inertia and
                                         * sonic_sfx_skid keeps D0 */

/* Assert the pose without the screech, for a braking frame under $400 that a
 * frame above $400 has already led.  Nothing else may call this: an unlatched
 * brake has no pose in the cartridge either. */
sonic_skid_carry:
    tst.b   sn_skid_was
    beq.s   1f
    st.b    sn_skid_on
    move.b  #SN_ANI_STOP,(GEN_SONIC_ANIM-GEOS_BIAS)(%a6)
1:  rts

sonic_spindash:
    movem.l %d1-%d3/%a0,-(%sp)
    move.w  (GEN_SONIC_PAD-GEOS_BIAS)(%a6),%d1
    move.w  (GEN_SONIC_PADEDGE-GEOS_BIAS)(%a6),%d2
    andi.w  #((1<<PAD_A)+(1<<PAD_B)+(1<<PAD_C)),%d2

    tst.b   (GEN_SONIC_SPIN_FLAG-GEOS_BIAS)(%a6)
    bne     .sd_update

    /* Sonic_Duck: down, and only standing still.  In Sonic 1 the duck is only
     * reached with inertia zero, which is why it sits behind Sonic_LookUp. */
    clr.b   (GEN_SONIC_DUCK-GEOS_BIAS)(%a6)
    btst    #PAD_DOWN,%d1
    beq     .sd_none
    tst.w   (GEN_SONIC_INERTIA-GEOS_BIAS)(%a6)
    bne     .sd_none
    move.b  #1,(GEN_SONIC_DUCK-GEOS_BIAS)(%a6)
    move.b  #SN_ANI_DUCK,(GEN_SONIC_ANIM-GEOS_BIAS)(%a6)    /* "move.b #$12,anim(a0)" in
                                                 * Sonic 2, $8 in Sonic 1's
                                                 * table -- SonAni_Duck either
                                                 * way, one cel held */

    /* Sonic_CheckSpindash: ducking, and a fresh A, B or C. */
    tst.w   %d2
    beq     .sd_owned
    move.b  #1,(GEN_SONIC_SPIN_FLAG-GEOS_BIAS)(%a6)
    clr.w   (GEN_SONIC_SPIN_CTR-GEOS_BIAS)(%a6)
    bsr     sonic_fm_rev                /* "move.w #SndID_SpindashRev,d0 / jsr
                                         * PlaySound", s2.asm:37564 */
    bra     .sd_owned

.sd_update:
    /* Sonic_UpdateSpindash: down still held keeps charging.  "move.b
     * #$1F,anim(a0)" is the first thing it does, at s2.asm:37652. */
    move.b  #SN_ANI_SPINDASH,(GEN_SONIC_ANIM-GEOS_BIAS)(%a6)
    btst    #PAD_DOWN,%d1
    beq     .sd_release

    /* Sonic_ChargingSpindash:
     *      move.w spindash_counter(a0),d0 / lsr.w #5,d0
     *      sub.w d0,spindash_counter(a0) / bcc + / move.w #0,...
     * then a rev adds $200 and stops at $800. */
    move.w  (GEN_SONIC_SPIN_CTR-GEOS_BIAS)(%a6),%d3
    beq.s   .sd_no_decay
    move.w  %d3,%d0
    lsr.w   #5,%d0
    sub.w   %d0,%d3
    bcc.s   .sd_decayed
    moveq   #0,%d3
.sd_decayed:
    move.w  %d3,(GEN_SONIC_SPIN_CTR-GEOS_BIAS)(%a6)
.sd_no_decay:
    tst.w   %d2
    beq     .sd_owned
    bsr     sonic_fm_rev                /* and again at s2.asm:37679, ahead of
                                         * the $200 -- each rev is a semitone */
    addi.w  #0x200,(GEN_SONIC_SPIN_CTR-GEOS_BIAS)(%a6)
    cmpi.w  #0x800,(GEN_SONIC_SPIN_CTR-GEOS_BIAS)(%a6)
    blo.s   .sd_owned
    move.w  #0x800,(GEN_SONIC_SPIN_CTR-GEOS_BIAS)(%a6)
    bra     .sd_owned

.sd_release:
    /* The launch, in the original's order. */
    move.b  #SN_ROLL_HEIGHT,(GEN_SONIC_HEIGHT-GEOS_BIAS)(%a6)
    move.b  #SN_ROLL_WIDTH,(GEN_SONIC_WIDTH-GEOS_BIAS)(%a6)
    addi.l  #(SN_CURL<<16),(GEN_SONIC_Y-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_SPIN_FLAG-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_DUCK-GEOS_BIAS)(%a6)
    /* moveq #0,d0 / move.b spindash_counter(a0),d0 -- the HIGH byte of the
     * counter, so 0..8, which is the table index. */
    moveq   #0,%d0
    move.b  (GEN_SONIC_SPIN_CTR-GEOS_BIAS)(%a6),%d0
    cmpi.w  #8,%d0
    bls.s   .sd_index_ok
    moveq   #8,%d0
.sd_index_ok:
    add.w   %d0,%d0
    lea     sn_spindash_speeds,%a0
    move.w  (%a0,%d0.w),%d3
    tst.b   (GEN_SONIC_FACING-GEOS_BIAS)(%a6)       /* the x_flip test, and the neg with it */
    beq.s   .sd_right
    neg.w   %d3
.sd_right:
    move.w  %d3,(GEN_SONIC_INERTIA-GEOS_BIAS)(%a6)
    move.w  %d3,(GEN_SONIC_XSP-GEOS_BIAS)(%a6)
    move.b  #1,(GEN_SONIC_JUMPING-GEOS_BIAS)(%a6)   /* the rolling status bit: this port has
                                         * one flag for the ball, and a spindash
                                         * launch is a roll */
    clr.w   (GEN_SONIC_SPIN_CTR-GEOS_BIAS)(%a6)
    bsr     sonic_sfx_dash
    bra.s   .sd_none                    /* he is moving now: the normal path */

.sd_owned:
    moveq   #1,%d0
    bra.s   .sd_out
.sd_none:
    moveq   #0,%d0
.sd_out:
    movem.l (%sp)+,%d1-%d3/%a0
    rts

.section .rodata
.align 2
/* SpindashSpeeds, s2.asm.  The Super table is not here: there is no Super
 * Sonic in this accessory to read it. */
sn_spindash_speeds:
    .word 0x800, 0x880, 0x900, 0x980, 0xA00, 0xA80, 0xB00, 0xB80, 0xC00
.section .text
.align 2

/* ------------------------------------------------------------------- sound */

/* Sonic 1's jump, SndA0, on the PSG's first tone channel.
 *
 * The score is four lines and drives one channel, so there is no SMPS player
 * here and there does not need to be: tools/sonic/build_sfx.py turns it into
 * one frequency a tick and this walks the table.  Everything the driver would
 * have computed -- the note indices, the header's -12 transpose, the frequency
 * formula, the -8-a-tick modulation -- is done there, where it can be read
 * against the disassembly.
 *
 * The PSG is one write port and nothing else in the port touches it: the Z80
 * carries the sample driver for the synth, which is not running while a desk
 * accessory is up. */
.equ SN_PSG,            0x00C00011

/* A PSG score is up to two channels of one sound.
 *
 * Each channel is a table of one word a tick -- the attenuation in the top
 * nibble and the frequency register in the rest, $F000 for a rest -- and the
 * whole score may carry a noise setting, which the chip wants once and not
 * per tick.  Three scores use it: Sonic 1's jump on one channel, Sonic 2's
 * skid on two, and the spindash release's white noise on the noise channel.
 *
 * The noise channel has no frequency of its own in the mode the release asks
 * for: $E7 is "white noise, at channel 2's pitch", so the frequency goes to
 * channel 2's register and the attenuation to the noise channel's.  That is
 * the driver's own arrangement -- zPSGUpdateFreq turns $E0 into $C0 for the
 * frequency write and leaves the volume write alone. */
.equ SN_PSGS_TAB_A,     0
.equ SN_PSGS_LATCH_A,   4
.equ SN_PSGS_TAB_B,     6               /* 0 when the score is one channel */
.equ SN_PSGS_LATCH_B,   10
.equ SN_PSGS_TICKS,     12
.equ SN_PSGS_FORM,      14              /* byte: the noise setting, or 0 */
.equ SN_PSGS_SIZE,      16

.equ SN_SFX_NONE,       0
.equ SN_SFX_JUMP,       1
.equ SN_SFX_SKID,       2
.equ SN_SFX_DASH,       3

/* -> A1 = the record for whatever is playing, Z set if nothing is. */
sonic_psg_score:
    move.l  %d0,-(%sp)
    lea     sn_psg_scores,%a1
    moveq   #0,%d0
    move.b  (GEN_SONIC_SFX_WHICH-GEOS_BIAS)(%a6),%d0
    beq.s   .ps_idle
    subq.w  #1,%d0
    mulu.w  #SN_PSGS_SIZE,%d0
    adda.l  %d0,%a1
.ps_idle:
    move.l  (%sp)+,%d0                  /* restore first: the answer is a
                                         * condition code, so it is taken from
                                         * memory afterwards */
    tst.b   (GEN_SONIC_SFX_WHICH-GEOS_BIAS)(%a6)
    rts

/* D0.b = score.  Start it. */
sonic_psg_play:
    movem.l %d0/%a1,-(%sp)
    move.b  %d0,(GEN_SONIC_SFX_WHICH-GEOS_BIAS)(%a6)
    move.w  #1,(GEN_SONIC_SFX_TICK-GEOS_BIAS)(%a6)
    bsr     sonic_psg_score
    beq.s   .pp_out
    move.b  SN_PSGS_FORM(%a1),%d0
    beq.s   .pp_out
    move.b  %d0,SN_PSG                  /* the noise setting, once */
.pp_out:
    movem.l (%sp)+,%d0/%a1
    rts

sonic_sfx_jump:
    move.l  %d0,-(%sp)
    moveq   #SN_SFX_JUMP,%d0
    bsr     sonic_psg_play
    move.l  (%sp)+,%d0
    rts

sonic_sfx_skid:
    move.l  %d0,-(%sp)
    moveq   #SN_SFX_SKID,%d0
    bsr     sonic_psg_play
    move.l  (%sp)+,%d0
    rts

/* The spindash release is two channels of one score on two different chips:
 * the FM ping and the noise that whooshes under it. */
sonic_sfx_dash:
    bra     sonic_fm_release

/* Silence and forget.  All four channels, not just the one the score used:
 * nothing else on this machine plays the PSG, so there is nothing to be
 * careful of and a channel left sounding would sound over the desktop. */
sonic_sfx_off:
    move.b  #0x9F,SN_PSG
    move.b  #0xBF,SN_PSG
    move.b  #0xDF,SN_PSG
    move.b  #0xFF,SN_PSG
    clr.w   (GEN_SONIC_SFX_TICK-GEOS_BIAS)(%a6)
    clr.b   (GEN_SONIC_SFX_WHICH-GEOS_BIAS)(%a6)
    rts

/* A0 = the table, D4.w = the tick's byte offset into it, D3.w = the latch. */
sonic_psg_write:
    movem.l %d0-%d2,-(%sp)
    move.w  (%a0,%d4.w),%d1
    move.w  %d1,%d2
    rol.w   #4,%d2
    andi.w  #0x000F,%d2                 /* the attenuation */
    cmpi.w  #15,%d2
    beq.s   .pw_quiet
    move.w  %d3,%d0
    cmpi.w  #0xE0,%d0                   /* the noise channel borrows two's */
    bne.s   .pw_tone
    move.w  #0xC0,%d0
.pw_tone:
    andi.w  #0x0FFF,%d1
    move.w  %d1,%d2
    andi.w  #0x000F,%d1
    or.w    %d0,%d1
    move.b  %d1,SN_PSG                  /* latch: channel, tone, low nibble */
    move.w  %d2,%d1
    lsr.w   #4,%d1
    andi.w  #0x003F,%d1
    move.b  %d1,SN_PSG                  /* and the six high bits */
    move.w  (%a0,%d4.w),%d1
    rol.w   #4,%d1
    andi.w  #0x000F,%d1
    or.w    %d3,%d1
    ori.w   #0x10,%d1
    move.b  %d1,SN_PSG                  /* attenuation, on its own channel */
    bra.s   .pw_out
.pw_quiet:
    move.w  %d3,%d1
    ori.w   #0x1F,%d1
    move.b  %d1,SN_PSG                  /* silent, but the score is running */
.pw_out:
    movem.l (%sp)+,%d0-%d2
    rts

/* One tick, from the accessory's own per-frame step. */
sonic_sfx_step:
    movem.l %d0-%d4/%a0-%a1,-(%sp)

    /* zSpindashPlayingCounter, which runs whether or not anything is
     * sounding: sixty frames after the last rev the pitch forgets how far it
     * had climbed. */
    tst.w   sn_rev_timer
    beq.s   .sfx_timer_done
    subq.w  #1,sn_rev_timer
    bne.s   .sfx_timer_done
    clr.w   sn_rev_bump
.sfx_timer_done:

    bsr     sonic_psg_score
    beq     .sfx_out
    move.w  (GEN_SONIC_SFX_TICK-GEOS_BIAS)(%a6),%d0
    beq     .sfx_out
    cmp.w   SN_PSGS_TICKS(%a1),%d0
    bhi     .sfx_end
    subq.w  #1,%d0
    add.w   %d0,%d0
    move.w  %d0,%d4
    movea.l SN_PSGS_TAB_A(%a1),%a0
    move.w  SN_PSGS_LATCH_A(%a1),%d3
    bsr     sonic_psg_write
    move.l  SN_PSGS_TAB_B(%a1),%d0
    beq.s   .sfx_advance
    movea.l %d0,%a0
    move.w  SN_PSGS_LATCH_B(%a1),%d3
    bsr     sonic_psg_write
.sfx_advance:
    addq.w  #1,(GEN_SONIC_SFX_TICK-GEOS_BIAS)(%a6)
    bra.s   .sfx_out
.sfx_end:
    bsr     sonic_sfx_off
.sfx_out:
    movem.l (%sp)+,%d0-%d4/%a0-%a1
    rts

/* ---------------------------------------------------------------- the FM side
 *
 * PORTED.  Two of these sounds are Sonic 2's -- the spindash rev and its
 * release -- and on the cartridge they were queued to Sonic 2's own Z80
 * driver, because four goes at writing an SMPS player out of the
 * disassembly's annotations produced four different wrong sounds.
 *
 * That is not available here.  The Z80 reads score data through its window
 * onto the 68000's bus, and the only memory this machine has there is the
 * Mega CD's PRG-RAM window -- which shows one 128KB bank at a time, and which
 * this payload switches several hundred times a frame between the screen's
 * bank and the art's.  Every read the driver made would be a coin toss.
 *
 * So they are played on this side after all, and the objection is answered
 * rather than argued with: tools/sonic-tools/build_sfx.py reads the scores out
 * of bank 31 of the cartridge, byte for byte -- header, script and voice --
 * and renders them a tick at a time.  Nothing is transcribed, so there is
 * nothing for a comment to be wrong about.
 *
 * Every register below is part II, because every score plays on FM4 or FM5:
 * their per-channel registers are base + 0 or base + 1 written to
 * $A04002/$A04003.  Key on and off are the one exception -- register $28
 * belongs to part I whatever the channel, and the channel is bits 0-2 of its
 * *data*. */
.equ SN_FM_PART2,       0x0100          /* geos_fm_write's part select */
.equ SN_FM_PAN,         0xC0            /* $B4: both speakers, no AMS or FMS */

.extern geos_fm_lock
.extern geos_fm_unlock
.extern geos_fm_write

/* Two scores, and everything that differs between them lives in a record: the
 * voice, the script, its length, which total levels a volume change may touch
 * (zVolTLMaskTbl by algorithm), and which FM channel it plays on.
 *
 * And two tracks, because Sonic 1's signpost is two channels running the same
 * loop four ticks apart.  A track is which score it is playing, how far into
 * it, and the volume currently in its channel's total-level registers. */
.equ SN_FM_SIGN5,       1
.equ SN_FM_SIGN4,       2
.equ SN_FM_REV,         3
.equ SN_FM_DASH,        4

.equ SN_FMS_VOICE,      0
.equ SN_FMS_SCRIPT,     4
.equ SN_FMS_REGS,       8               /* its driver's register order */
.equ SN_FMS_BUMP,       12              /* the address of a word to add to
                                         * every note index, or zero.  Only
                                         * the rev has one: its pitch climbs a
                                         * semitone a rev and a rendered score
                                         * cannot know how many */
.equ SN_FMS_TICKS,      16
.equ SN_FMS_MASK,       18
.equ SN_FMS_CHAN,       20              /* register offset inside part II */
.equ SN_FMS_KEY,        22              /* what $28 calls it */
.equ SN_FMS_SIZE,       24

.equ SN_FM_TRACKS,      2
.equ SN_FMT_WHICH,      0
.equ SN_FMT_VOL,        1
.equ SN_FMT_TICK,       2
.equ SN_FMT_SIZE,       4

.equ SN_FM_ATTACK,      0x80            /* in a script's volume byte */

/* D1.w = track -> A2 = its state. */
sonic_fm_track:
    lea     (GEN_SONIC_FM_TRACKS-GEOS_BIAS)(%a6),%a2
    add.w   %d1,%d1
    add.w   %d1,%d1
    adda.w  %d1,%a2
    lsr.w   #2,%d1
    rts

/* A2 = a track -> A1 = the record of whatever it is playing, Z set if idle. */
sonic_fm_score:
    move.l  %d0,-(%sp)
    lea     sn_fm_scores,%a1            /* never leave A1 unassigned: a caller
                                         * that reads it anyway gets a real
                                         * record rather than whatever was in
                                         * the register before the call */
    moveq   #0,%d0
    move.b  SN_FMT_WHICH(%a2),%d0
    beq.s   .fsc_idle
    subq.w  #1,%d0
    mulu.w  #SN_FMS_SIZE,%d0
    adda.l  %d0,%a1
.fsc_idle:
    move.l  (%sp)+,%d0                  /* restore FIRST -- MOVE sets the
                                         * condition codes, and this routine's
                                         * whole answer is a condition code */
    tst.b   SN_FMT_WHICH(%a2)           /* so the flag is set last, and from
                                         * memory, where nothing can disturb it */
    rts

/* D0.w = key value, D1.w = zero to release -> register $28, which is a part I
 * register whatever the channel: the part is bit 2 of its data. */
sonic_fm_key:
    movem.l %d0-%d1,-(%sp)
    tst.w   %d1
    beq.s   .fk_off
    ori.w   #0xF0,%d0                   /* all four operators */
.fk_off:
    move.b  %d0,%d1
    move.w  #0x28,%d0
    bsr     geos_fm_write
    movem.l (%sp)+,%d0-%d1
    rts

/* D0.b = score, D1.w = track.  Load the voice and arm the script; the key-on
 * is the script's business, because in two of the four it happens more than
 * once and in one of them not on the first tick. */
sonic_fm_play:
    movem.l %d0-%d4/%a0-%a2,-(%sp)
    bsr     sonic_fm_track
    move.b  %d0,SN_FMT_WHICH(%a2)
    clr.b   SN_FMT_VOL(%a2)             /* every header's volume byte, near
                                         * enough: the signpost's FM4 carries
                                         * $03 and the script starts there */
    move.w  #1,SN_FMT_TICK(%a2)
    bsr     geos_fm_lock
    bsr     sonic_fm_score
    beq.s   .fp_out
    move.w  SN_FMS_KEY(%a1),%d0
    moveq   #0,%d1
    bsr     sonic_fm_key                /* silence whatever was on it */
    bsr     sonic_fm_voice
.fp_out:
    bsr     geos_fm_unlock
    movem.l (%sp)+,%d0-%d4/%a0-%a2
    rts

/* One rev, and the climb.
 *
 * zPlaySound_CheckSpindash is three facts: the pitch goes up a semitone with
 * every rev, it stops going up at twelve, and if sixty frames pass without a
 * rev the next one starts from the bottom again.  On the cartridge all three
 * are inside the driver, because queueing $E0 is all the game does; here they
 * are these nine instructions, and sonic_sfx_step runs the countdown. */
sonic_fm_rev:
    movem.l %d0-%d1,-(%sp)
    tst.w   sn_rev_timer
    bne.s   .fr_climbing
    clr.w   sn_rev_bump                 /* the first rev of a fresh charge */
    bra.s   .fr_armed
.fr_climbing:
    cmpi.w  #11,sn_rev_bump             /* `cp 0Ch` -- it saturates rather
                                         * than wrapping */
    bhs.s   .fr_armed
    addq.w  #1,sn_rev_bump
.fr_armed:
    move.w  #0x3C,sn_rev_timer
    moveq   #SN_FM_REV,%d0
    moveq   #0,%d1
    bsr     sonic_fm_play
    movem.l (%sp)+,%d0-%d1
    rts

/* And the release, which is one sound on two chips: the FM ping, and under it
 * white noise sliding down over eighty ticks. */
sonic_fm_release:
    movem.l %d0-%d1,-(%sp)
    clr.w   sn_rev_timer                /* the charge is spent */
    clr.w   sn_rev_bump
    moveq   #SN_FM_DASH,%d0
    moveq   #0,%d1
    bsr     sonic_fm_play
    moveq   #SN_SFX_DASH,%d0
    bsr     sonic_psg_play
    movem.l (%sp)+,%d0-%d1
    rts

/* The signpost, on both its channels.  FM4's own data is four ticks of rest
 * and then nothing at all -- no smpsStop -- so in the cartridge its track
 * pointer runs on into FM5's, loads the same voice and plays the same loop.
 * Two channels, four ticks apart, three units of attenuation between them.
 * That doubling is the sound; one channel of it is a thinner sound than the
 * cartridge makes. */
sonic_fm_signpost:
    movem.l %d0-%d1,-(%sp)
    moveq   #SN_FM_SIGN5,%d0
    moveq   #0,%d1
    bsr     sonic_fm_play
    moveq   #SN_FM_SIGN4,%d0
    moveq   #1,%d1
    bsr     sonic_fm_play
    movem.l (%sp)+,%d0-%d1
    rts

/* zSetVoice, in its own order: feedback/algorithm, the four detune/multiples,
 * the sixteen envelope registers, the panning, then the four total levels.
 * A1 = the score, A2 = the track, and the bus is the caller's. */
sonic_fm_voice:
    movem.l %d0-%d4/%a0/%a3,-(%sp)
    movea.l SN_FMS_VOICE(%a1),%a0
    movea.l SN_FMS_REGS(%a1),%a3
    move.w  SN_FMS_CHAN(%a1),%d3
    /* Feedback and algorithm, which is the one register not in the table. */
    move.w  #(SN_FM_PART2+0xB0),%d0
    add.w   %d3,%d0
    move.b  (%a0)+,%d1
    bsr     geos_fm_write
    moveq   #19,%d4                     /* then the twenty operator bytes, in
                                         * whatever order this score's driver
                                         * writes them */
.fv_op:
    moveq   #0,%d0
    move.b  (%a3)+,%d0
    add.w   %d3,%d0
    ori.w   #SN_FM_PART2,%d0
    move.b  (%a0)+,%d1
    bsr     geos_fm_write
    dbra    %d4,.fv_op
    move.w  #(SN_FM_PART2+0xB4),%d0
    add.w   %d3,%d0
    move.b  #SN_FM_PAN,%d1
    bsr     geos_fm_write
    bsr     sonic_fm_levels
    movem.l (%sp)+,%d0-%d4/%a0/%a3
    rts

/* The four total levels.  zVolTLMaskTbl is rotated right a bit per write and
 * only a set bit takes the channel volume, because only an output operator is
 * a volume -- on a modulator the same byte is part of the timbre. */
sonic_fm_levels:
    movem.l %d0-%d4/%a0/%a3,-(%sp)
    movea.l SN_FMS_VOICE(%a1),%a0
    lea     21(%a0),%a0                 /* the TL bytes, last in the voice */
    movea.l SN_FMS_REGS(%a1),%a3
    lea     20(%a3),%a3                 /* and their own four registers */
    move.w  SN_FMS_MASK(%a1),%d3
    moveq   #3,%d4
.fl_next:
    moveq   #0,%d1
    move.b  (%a0)+,%d1
    lsr.b   #1,%d3
    bcc.s   .fl_write
    move.b  SN_FMT_VOL(%a2),%d2
    add.b   %d2,%d1                     /* eight bits, and allowed to wrap.
                                         * Total level is seven bits and bit 7
                                         * is not part of it, so a voice byte
                                         * of $80 is TL 0, the loudest value
                                         * there is.  Clamping it at $7F -- the
                                         * disassembly's FixDriverBugs guard,
                                         * not the cartridge -- turned that
                                         * into silence. */
.fl_write:
    moveq   #0,%d0
    move.b  (%a3)+,%d0
    add.w   SN_FMS_CHAN(%a1),%d0
    ori.w   #SN_FM_PART2,%d0
    bsr     geos_fm_write
    dbra    %d4,.fl_next
    movem.l (%sp)+,%d0-%d4/%a0/%a3
    rts

/* Release every channel a track might be holding, and forget the scores. */
sonic_fm_off:
    movem.l %d0-%d2/%a1-%a2,-(%sp)
    bsr     geos_fm_lock
    moveq   #0,%d2
.fo_track:
    move.w  %d2,%d1
    bsr     sonic_fm_track
    bsr     sonic_fm_score
    beq.s   .fo_next
    move.w  SN_FMS_KEY(%a1),%d0
    moveq   #0,%d1
    bsr     sonic_fm_key
.fo_next:
    clr.b   SN_FMT_WHICH(%a2)
    clr.w   SN_FMT_TICK(%a2)
    addq.w  #1,%d2
    cmpi.w  #SN_FM_TRACKS,%d2
    blo.s   .fo_track
    bsr     geos_fm_unlock
    movem.l (%sp)+,%d0-%d2/%a1-%a2
    rts

/* One tick of every track.  Nothing playing means nothing taken: the Z80 has
 * the cartridge's driver on it now, and taking its bus once a frame to write
 * no registers is a pause it does not need. */
sonic_fm_step:
    tst.b   ((GEN_SONIC_FM_TRACKS-GEOS_BIAS)+SN_FMT_WHICH)(%a6)
    bne.s   .fs_go
    tst.b   ((GEN_SONIC_FM_TRACKS-GEOS_BIAS)+SN_FMT_SIZE+SN_FMT_WHICH)(%a6)
    bne.s   .fs_go
    rts
.fs_go:
    movem.l %d0-%d6/%a0-%a2,-(%sp)
    bsr     geos_fm_lock
    moveq   #0,%d6
.fs_track:
    move.w  %d6,%d1
    bsr     sonic_fm_track
    bsr     sonic_fm_score
    beq     .fs_next
    move.w  SN_FMT_TICK(%a2),%d0
    beq     .fs_next
    cmp.w   SN_FMS_TICKS(%a1),%d0
    bhi     .fs_end

    subq.w  #1,%d0
    lsl.w   #2,%d0                      /* four bytes a tick */
    movea.l SN_FMS_SCRIPT(%a1),%a0
    adda.w  %d0,%a0

    /* A key-on always starts by releasing what was there -- that is what makes
     * twenty-one of them a pip-pip-pip and not one long note. */
    move.b  1(%a0),%d5                  /* volume, with the attack bit */
    move.w  SN_FMS_KEY(%a1),%d4
    btst    #7,%d5
    beq.s   .fs_no_release
    move.w  %d4,%d0
    moveq   #0,%d1
    bsr     sonic_fm_key
.fs_no_release:

    moveq   #0,%d2
    move.b  (%a0),%d2                   /* the note index; 0 is $80, a rest */
    beq     .fs_rest
    move.l  SN_FMS_BUMP(%a1),%d1        /* ...and the spindash's climb, if this
                                         * is the score that has one */
    beq.s   .fs_nobump
    movea.l %d1,%a0
    add.w   (%a0),%d2
.fs_nobump:
    add.w   %d2,%d2
    lea     sn_fm_freq,%a0
    move.w  (%a0,%d2.w),%d3
    movea.l SN_FMS_SCRIPT(%a1),%a0
    move.w  SN_FMT_TICK(%a2),%d0
    subq.w  #1,%d0
    lsl.w   #2,%d0
    adda.w  %d0,%a0
    add.w   2(%a0),%d3                  /* plus the modulation so far */

    /* The high byte first: the chip latches the whole word on the low write,
     * so writing $A0 before $A4 would sound the previous block for a frame. */
    move.w  #(SN_FM_PART2+0xA4),%d0
    add.w   SN_FMS_CHAN(%a1),%d0
    move.w  %d3,%d1
    lsr.w   #8,%d1
    bsr     geos_fm_write
    move.w  #(SN_FM_PART2+0xA0),%d0
    add.w   SN_FMS_CHAN(%a1),%d0
    move.b  %d3,%d1
    bsr     geos_fm_write

    /* smpsAlterVol only moves at a note boundary, so the four total levels
     * are only rewritten when it has. */
    move.b  %d5,%d1
    andi.b  #0x7F,%d1
    cmp.b   SN_FMT_VOL(%a2),%d1
    beq.s   .fs_vol_same
    move.b  %d1,SN_FMT_VOL(%a2)
    bsr     sonic_fm_levels
.fs_vol_same:
    btst    #7,%d5
    beq.s   .fs_advance
    move.w  %d4,%d0
    moveq   #1,%d1
    bsr     sonic_fm_key                /* and now it sounds */
    bra.s   .fs_advance
.fs_rest:
    move.w  %d4,%d0
    moveq   #0,%d1
    bsr     sonic_fm_key                /* zFMDoRest, which also lifts the key */
.fs_advance:
    addq.w  #1,SN_FMT_TICK(%a2)
    bra.s   .fs_next
.fs_end:
    move.w  SN_FMS_KEY(%a1),%d0
    moveq   #0,%d1
    bsr     sonic_fm_key
    clr.b   SN_FMT_WHICH(%a2)
    clr.w   SN_FMT_TICK(%a2)
.fs_next:
    addq.w  #1,%d6
    cmpi.w  #SN_FM_TRACKS,%d6
    blo     .fs_track
    bsr     geos_fm_unlock
    movem.l (%sp)+,%d0-%d6/%a0-%a2
    rts

/* Above the API gate with the other large read-only blobs.  The jump sound is
 * an FM score and a voice table -- a kilobyte of data that never moves -- and
 * .rodata is shared with everything under $20000.  Absolute addressing already,
 * so nothing here cared where it lived. */
.section .blobdata,"a"
.align 2
sn_fm_scores:
    .long   sn_fm_voice_sign5, sn_fm_sign5, sn_fm_regs_s1, 0
    .word   SN_FM_SIGN5_TICKS, SN_FM_SIGN5_MASK, SN_FM_SIGN5_CHAN, SN_FM_SIGN5_KEY
    .long   sn_fm_voice_sign4, sn_fm_sign4, sn_fm_regs_s1, 0
    .word   SN_FM_SIGN4_TICKS, SN_FM_SIGN4_MASK, SN_FM_SIGN4_CHAN, SN_FM_SIGN4_KEY
    .long   sn_fm_voice_rev,   sn_fm_rev,   sn_fm_regs_s2, sn_rev_bump
    .word   SN_FM_REV_TICKS,   SN_FM_REV_MASK,   SN_FM_REV_CHAN,   SN_FM_REV_KEY
    .long   sn_fm_voice_dash,  sn_fm_dash,  sn_fm_regs_s2, 0
    .word   SN_FM_DASH_TICKS,  SN_FM_DASH_MASK,  SN_FM_DASH_CHAN,  SN_FM_DASH_KEY

/* One record a PSG score: the jump on channel 0, the skid on 1 and 2, and the
 * release's noise on channel 3 with the setting that makes it white noise at
 * channel 2's pitch. */
sn_psg_scores:
    .long   sn_psg_jump
    .word   0x0080
    .long   0
    .word   0x0000
    .word   SN_PSG_JUMP_TICKS
    .byte   0, 0
    .long   sn_psg_skid_a
    .word   0x00A0
    .long   sn_psg_skid_b
    .word   0x00C0
    .word   SN_PSG_SKID_A_TICKS
    .byte   0, 0
    .long   sn_psg_dash
    .word   0x00E0
    .long   0
    .word   0x0000
    .word   SN_PSG_DASH_TICKS
    .byte   SN_PSG_DASH_FORM, 0

/* The spindash's climb, which is state and not score. */
.section .data
.align 2
sn_rev_bump:    .word   0
sn_rev_timer:   .word   0
/* The level: which desktop column every level column shows. */
sn_deskmap:     .space  SN_MAP_COLS
/* Set only while sonic_ground_land is inside sonic_ground. */
sn_gnd_wide:    .byte   0
/* The skid pose's latch, and the same byte carried into the next frame.  See
 * sonic_skid: in the cartridge this is `anim` simply not being written, and
 * here it has to be said out loud because the top of every frame clears it. */
sn_skid_on:     .byte   0
sn_skid_was:    .byte   0
.align 2
.section .blobdata,"a"
.align 2
.include "sonic/sfx_jump.inc"
.section .text
.align 2

.section .rodata
.align 2
/* SonAni_Walk and SonAni_Run, frame for frame. */
sonic_ani_walk: .byte SN_WALK13,SN_WALK14,SN_WALK15,SN_WALK16,SN_WALK11,SN_WALK12
.align 2
sonic_ani_run:  .byte SN_RUN11,SN_RUN12,SN_RUN13,SN_RUN14
.align 2
/* Ani_Sign, script for script: a duration, the cels, then afEnd.  Cycles 1 and
 * 2 are the same three turning cels with a different face leading them, which
 * is what makes Eggman's side become Sonic's. */
sign_ani_table:
    .word   sign_ani_0-sign_ani_table, sign_ani_1-sign_ani_table
    .word   sign_ani_2-sign_ani_table, sign_ani_3-sign_ani_table
sign_ani_0: .byte 15, 0, 0xFF
sign_ani_1: .byte 1, 0, 1, 2, 3, 0xFF
sign_ani_2: .byte 1, 4, 1, 2, 3, 0xFF
sign_ani_3: .byte 15, 4, 0xFF
.align 2

/* PORTED. On the cartridge the blob was .incbin'd here and every reader
 * said `lea sonic_blob,%aN'. Fifty-two thousand bytes will not go into a
 * payload that has thirty-two, so it lives in PRG RAM instead and the
 * readers say `movea.l sn_blob,%aN' -- the same nine sites, one word
 * longer each. main.S puts the window's address there.
 *
 * The Sonic 2 sound-effect bank that stood below it is gone with the
 * sound: the Z80 reached it through its own ROM window, and this port
 * is not a ROM. */
