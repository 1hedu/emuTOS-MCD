/* Standalone CDD driver test, run on the sub CPU with the CDBIOS resident.
 *
 * Eight discs failed to get the drive to answer inside EmuTOS, and every
 * correction to the protocol changed nothing: one command out, then
 * silence, exactly as on the first attempt. That pattern says the
 * protocol may not be what is wrong, and the only way to find out is to
 * run the same driver with everything else taken away.
 *
 * Timing matters: the BIOS needs the drive to load the payload off the
 * disc in the first place, so taking the CDD away from it during SP_Init
 * hangs the boot at the licence screen -- which is exactly what the
 * first attempt did, while reporting a confident verdict read off that
 * screen's background colour. The main CPU therefore says when it is
 * running, and only then does the test take over.
 *
 * So this runs from the SP's idle loop, before anything is evicted:
 *   - the CDBIOS is resident and untouched
 *   - EmuTOS is not running, so no VBL handler, no 200 Hz timer, no
 *     interrupt levels masking the drive out
 *   - the servant is not pumping the screen, so nothing takes the sub
 *     off its own bus with SBRQ
 *   - the drive's own level-4 interrupt drives it, exactly as it drives
 *     EmuTOS, so the one thing under test is unchanged
 *
 * If the drive answers here, the driver is right and the fault is
 * somewhere in how EmuTOS shares this CPU with it. If it refuses here
 * too, the driver is still wrong despite matching the disassembly. Both
 * answers cut the search in half, which is more than any of the last
 * eight discs managed.
 *
 * The BIOS's own CDD state machine has to be got out of the way first,
 * or it and this code both drive the same registers. Clearing bit 7 of
 * its flag byte at $5834 makes its interrupt handler return immediately
 * without touching them; nothing else about the BIOS is disturbed.
 *
 * Verdict, published for the main CPU to put on screen as a colour:
 *   0  nothing yet
 *   1  no status packet ever checksummed  (the link itself is broken)
 *   2  packets fine, the drive never named a question we asked
 *   3  it named one
 *   4  it named two different ones -- the round trip works
 */

        .equ    CDD_CTRL,   0xFF8037
        .equ    CDD_STAT,   0xFF8038
        .equ    CDD_CMD,    0xFF8042
        .equ    BIOS_FLAGS, 0x5834      | bit 7: the BIOS's CDD is enabled

        .equ    VERDICT,    0xFF8020    | main reads these at 0xA1202x
        .equ    NVALID,     0xFF8022
        .equ    NECHO,      0xFF8024
        .equ    LASTSTAT,   0xFF8026    | status nibbles 0 and 1
        .equ    NIRQ,       0xFF8028    | exchanges seen at all

        .equ    VEC_L4,     0x5F8A      | address field of the BIOS's
                                        | level-4 jmp at $5F88

        .globl  CddTestInit
        .globl  CddPoll

/* Level 4, replacing the BIOS's own handler. Polling was the first
 * attempt and it measured the wrong thing: with nothing to pace it the
 * loop ran millions of times a second, so it mostly read status the
 * drive had already replaced, and the echo it was looking for went past
 * between reads. Running on the interrupt paces it at one exchange, as
 * on the real thing -- and leaves the code under test identical to the
 * code EmuTOS runs, which is the whole point of the comparison. */
CddInt4:
        move.w  #0x2700,sr
        bsr     CddPoll
        rte

/* Called from the SP's idle loop. Does nothing until the main CPU has
 * been handed Word RAM and started the display payload, which it
 * announces by writing a magic word; before that the BIOS still needs
 * the drive it is about to lose. */
CddPoll_stub:
        tst.b   cdd_started
        bne     9f
        cmpi.w  #0x600D,0xFF8010
        bne     9f
        move.b  #1,cdd_started
        bsr     CddTestInit
9:      rts

CddTestInit:
        bclr    #7,BIOS_FLAGS           | the BIOS stops driving the CDD
        move.l  #CddInt4,VEC_L4         | and we take its interrupt
        ori.b   #0x10,0xFF8033          | level 4 enabled
        move.b  #4,CDD_CTRL             | HOCK on, DTS/DRS left alone
        move.w  #5,VERDICT              | stage marker: init ran
        clr.w   NVALID
        clr.w   NECHO
        clr.w   LASTSTAT
        clr.w   NIRQ
        /* Byte at a time and explicitly. The first version cleared
         * these with word writes, which reached one byte past each and
         * wiped the hold counter -- so the test asked a question it was
         * not expecting an answer to, and would have called a working
         * drive broken. */
        move.b  #2,cdd_want
        move.b  #32,cdd_hold
        clr.b   cdd_seen1
        clr.b   cdd_seen2
        rts

/* One exchange, in the order the BIOS does it: wait for the incoming
 * packet to finish, read it, acknowledge only if there is an outgoing
 * transfer to acknowledge, then load the next command. */
CddPoll:
        movem.l d0-d3/a0-a1,-(sp)
        addq.w  #1,NIRQ

        move.w  #256,d0                 | wait for DRS to go low
1:      btst    #1,CDD_CTRL
        dbeq    d0,1b
        bne     cp_out                  | still set: give up on this one

        lea     CDD_STAT,a0             | read the ten status nibbles
        lea     cdd_stat,a1
        move.l  (a0)+,(a1)+
        move.l  (a0)+,(a1)+
        move.w  (a0)+,(a1)+

        lea     cdd_stat,a0             | sum all ten: must come out zero
        moveq   #0,d1
        moveq   #4,d2
2:      add.b   (a0)+,d1
        add.b   (a0)+,d1
        dbra    d2,2b
        not.b   d1
        andi.b  #0x0F,d1
        bne     cp_ack                  | bad checksum, but still acknowledge

        addq.w  #1,NVALID
        move.b  cdd_stat,d3             | nibble 0: drive state
        lsl.w   #8,d3
        move.b  cdd_stat+1,d3           | nibble 1: which question answered
        move.w  d3,LASTSTAT

        cmpi.b  #0x0F,cdd_stat          | "invalid/not ready" is not an answer
        beq     cp_ack
        move.b  cdd_want,d3
        cmp.b   cdd_stat+1,d3
        bne     cp_ack

        addq.w  #1,NECHO                | it named the question we asked
        cmpi.b  #0,cdd_seen1
        bne     3f
        move.b  d3,cdd_seen1
        bra     cp_ack
3:      cmp.b   cdd_seen1,d3
        beq     cp_ack
        move.b  d3,cdd_seen2            | and a second, different one

cp_ack:
        btst    #0,CDD_CTRL             | only if a command is on its way out
        beq     cp_cmd
        move.b  #4,CDD_CTRL

cp_cmd:
        /* Ask for the total disc length and the current track by turns.
         * 3 and 2 are not values anything reports by accident, so a
         * reply naming them cannot be a coincidence.
         *
         * Hold each question for a while before switching. The reply
         * arrives an exchange or more after the request, so a test that
         * changes the question every exchange compares every reply
         * against a question it is no longer asking and can never see a
         * match -- which is exactly what the first version of this did,
         * and it would have reported a working drive as broken. The
         * driver under test repeats a command 75 times for the same
         * reason. */
        subq.b  #1,cdd_hold
        bne     cc_keep
        move.b  #32,cdd_hold
        move.b  cdd_want,d0
        eori.b  #1,d0                   | 3 <-> 2
        ori.b   #2,d0
        move.b  d0,cdd_want
cc_keep:
        move.b  cdd_want,d0

        lea     cdd_cmd,a0              | command 2, sub-command in nibble 3
        clr.l   (a0)
        clr.l   4(a0)
        clr.w   8(a0)
        move.b  #2,(a0)
        move.b  d0,3(a0)

        moveq   #0,d1                   | checksum over the first eight
        moveq   #3,d2
        lea     cdd_cmd,a0
4:      add.b   (a0)+,d1
        add.b   (a0)+,d1
        dbra    d2,4b
        not.b   d1
        andi.b  #0x0F,d1
        move.b  d1,cdd_cmd+9

        lea     cdd_cmd,a0              | long, long, word, as the BIOS does
        lea     CDD_CMD,a1
        move.l  (a0)+,(a1)+
        move.l  (a0)+,(a1)+
        move.w  (a0)+,(a1)+

cp_out:
        bsr     cp_verdict
        movem.l (sp)+,d0-d3/a0-a1
        rts

cp_verdict:
        moveq   #1,d0                   | reached only once level 4 fires                   | nothing has checksummed
        tst.w   NVALID
        beq     9f
        moveq   #2,d0                   | link fine, no answer
        tst.w   NECHO
        beq     9f
        moveq   #3,d0                   | one question named
        tst.b   cdd_seen2
        beq     9f
        moveq   #4,d0                   | two: the round trip works
9:      move.w  d0,VERDICT
        rts

        /* Kept in .text on purpose: this is a flat binary loaded as a
         * BIOS module, and a separate .data section gets placed at an
         * address of the linker's choosing with the gap zero-filled --
         * 8 KB of padding, and buffers outside the module the BIOS
         * actually loads. PRG-RAM is RAM, so .text is writable. */
cdd_started:    .byte   0
cdd_want:       .byte   2
cdd_hold:       .byte   32
cdd_seen1:      .byte   0
cdd_seen2:      .byte   0
        /* Alignment is not cosmetic here. These buffers are read and
         * written with move.l, which address-errors on an odd address,
         * and everything after them -- including the BIOS's own sector
         * buffer at the end of this module -- shifts with them. An odd
         * byte count in this block hung the boot outright. */
        .even
cdd_stat:       .space  12
        .even
cdd_cmd:        .space  12
        .even
