/* m1hello.S -- how many words can be written to the VDP data port?
 *
 * Two runs, read together, point at one instruction. The first
 * staircase painted a colour before it wrote any VDP register, and the
 * console showed it: red. The second wrote the registers first and
 * painted nothing until after a 64 KB VRAM clear, and the console
 * showed no colour at all -- just the flash cart's own text, redrawn
 * through our name tables, which is itself proof the register writes
 * landed.
 *
 * So the registers are fine and the clear is not. This asks how much
 * of it is fine: sixteen words, then two hundred and fifty-six, then
 * four thousand, then the full sixteen thousand, with a colour and a
 * pause after each. The colour it stops on is the count that killed it.
 *
 *   flash cart's text, frozen   the register writes landed and the
 *                               very first data-port write did not
 *   RED                         registers in, CRAM visible, no VRAM
 *                               written yet
 *   YELLOW                      16 words to the data port
 *   GREEN                       272 words
 *   BLUE                        4368 words
 *   WHITE                       all 16384 -- VRAM is clear
 *   colours cycling             everything works
 *
 * Boot, header and console bring-up are megadev's; see boot/m1boot.s.
 */

        .equ    VDP_DATA,   0xC00000
        .equ    VDP_CTRL,   0xC00004

        .equ    C_RED,      0x000E
        .equ    C_YELLOW,   0x00EE
        .equ    C_GREEN,    0x00E0
        .equ    C_BLUE,     0x0E00
        .equ    C_WHITE,    0x0EEE

        .globl  main
main:
        move    #0x2700,sr

        /* Cancel any half-written VDP command before touching a
         * register.
         *
         * The control port takes commands as two words. If the flash
         * cart's menu was interrupted between them, the VDP is still
         * waiting for a second word -- and the first register write
         * becomes that second word. Every write after it is then read
         * as a fresh command instead of a register, and some pair of
         * them will be a DMA the VDP performs while holding the bus.
         * A single read of the control port throws the pending half
         * away.
         *
         * megadev's init does exactly this, and then skips it in
         * precisely our case: `tst.l IO_CTRL1-1 / bne skip_init` treats
         * an already-initialised console as nothing left to do, which
         * is right after a hard reset and wrong after a menu hands over
         * without one. So it is done here, unconditionally, where it
         * cannot be skipped.
         *
         * This is what the console has been saying all along. Painting
         * a colour before the register writes showed the colour;
         * painting one after them showed nothing, ever, at any batch
         * size -- because there was nothing wrong with the colour and
         * everything wrong with the twenty-four writes in front of it. */
        move.w  0xC00004,d0             | discard: cancels the pending half

        /* The registers, display ON and staying on. */
        lea     VDP_CTRL,a0
        lea     regs(pc),a1
        move.w  #(regs_end-regs)/2-1,d0
1:      move.w  (a1)+,(a0)
        dbra    d0,1b

        /* Before a single word goes to the data port. This is the same
         * write that worked in the first staircase, so it should still
         * work here -- and if it does not, the register writes are what
         * broke it and everything below is beside the point. */
        move.w  #C_RED,d1
        bsr     colour
        bsr     pause

        /* Each batch re-points at VRAM itself, because writing the
         * colour leaves the VDP's address register aimed at CRAM. The
         * first version of this let the second batch dump four
         * thousand zeroes into the palette. */
        move.l  #0x40000000,d6          | VRAM, word 0
        moveq   #16-1,d0
        bsr     fill
        move.w  #C_YELLOW,d1
        bsr     colour
        bsr     pause

        move.l  #0x40200000,d6          | VRAM, word 16
        move.w  #256-1,d0
        bsr     fill
        move.w  #C_GREEN,d1
        bsr     colour
        bsr     pause

        move.l  #0x42200000,d6          | VRAM, word 272
        move.w  #4096-1,d0
        bsr     fill
        move.w  #C_BLUE,d1
        bsr     colour
        bsr     pause

        move.l  #0x62200000,d6          | VRAM, word 4368
        move.w  #12016-1,d0
        bsr     fill
        move.w  #C_WHITE,d1
        bsr     colour
        bsr     pause

        moveq   #0,d2
2:      move.w  d2,d1
        andi.w  #0x0EEE,d1
        bsr     colour
        addq.w  #1,d2
        move.w  #0x0C00,d3
3:      nop
        dbra    d3,3b
        bra     2b

/* d0+1 words of zero to the data port, starting at the address in the
 * control longword d6. */
fill:
        move.l  d6,VDP_CTRL
        moveq   #0,d1
1:      move.w  d1,VDP_DATA
        dbra    d0,1b
        rts

colour:
        move.l  #0xC0000000,VDP_CTRL
        move.w  d1,VDP_DATA
        rts

/* About two seconds. */
pause:
        move.w  #22,d4
1:      move.w  #0xFFFF,d5
2:      nop
        dbra    d5,2b
        dbra    d4,1b
        rts

regs:
        .word   0x8004, 0x8144, 0x8230, 0x8300, 0x8407, 0x8500
        .word   0x8600, 0x8700, 0x8800, 0x8900, 0x8A00, 0x8B00
        .word   0x8C81, 0x8D00, 0x8E00, 0x8F02, 0x9001, 0x9100
        .word   0x9200, 0x9300, 0x9400, 0x9500, 0x9600, 0x9700
regs_end:
