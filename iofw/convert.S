| Planar -> Genesis tile inner loop.
|
| This is the servant's hot path: measured at 22 of the 23.8 frames a
| full-screen repaint costs, with the VDP accounting for only 1.8. The
| C version spent most of that on address arithmetic the 68000 makes
| expensive -- it has no scaled indexing, so every table lookup pays its
| own shift, and the compiler reloaded the four table bases every row.
|
| Here the bases live in a1-a4 for the whole tile, the eight rows are
| unrolled behind fixed displacements so the source pointer never moves,
| and each row needs one index register and one accumulator, both
| call-clobbered, so only a2-a4 have to be saved.
|
| void scd_tile(const uint8_t *src, uint32_t vdpcmd)
|   src    - cache address of the tile's top-left plane byte. The four
|            plane bytes are at stride 2; the next row is 160 bytes on.
|   vdpcmd - ready-made VDP control long for this tile's VRAM address.

        .text
        .globl  scd_tile

VDP_DATA_PORT = 0xC00000
VDP_CTRL_PORT = 0xC00004

| One tile row: four plane bytes -> one 32-bit run of eight 4bpp pixels.
        .macro  ROW off
        moveq   #0,%d0
        move.b  \off+0(%a0),%d0
        add.w   %d0,%d0                 | byte * 4: the tables are longs
        add.w   %d0,%d0
        move.l  0(%a1,%d0.w),%d1
        moveq   #0,%d0
        move.b  \off+2(%a0),%d0
        add.w   %d0,%d0
        add.w   %d0,%d0
        or.l    0(%a2,%d0.w),%d1
        moveq   #0,%d0
        move.b  \off+4(%a0),%d0
        add.w   %d0,%d0
        add.w   %d0,%d0
        or.l    0(%a3,%d0.w),%d1
        moveq   #0,%d0
        move.b  \off+6(%a0),%d0
        add.w   %d0,%d0
        add.w   %d0,%d0
        or.l    0(%a4,%d0.w),%d1
        move.l  %d1,VDP_DATA_PORT
        .endm

scd_tile:
        movem.l %a2-%a4,-(%sp)
        move.l  16(%sp),%a0             | src
        move.l  20(%sp),%d1             | vdpcmd
        move.l  %d1,VDP_CTRL_PORT       | one address command per tile
        lea     tab8,%a1
        lea     1024(%a1),%a2
        lea     2048(%a1),%a3
        lea     3072(%a1),%a4

        ROW 0
        ROW 160
        ROW 320
        ROW 480
        ROW 640
        ROW 800
        ROW 960
        ROW 1120

        movem.l (%sp)+,%a2-%a4
        rts
