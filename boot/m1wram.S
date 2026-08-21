/* m1wram.S -- can the cartridge fill Word RAM and hand it to the sub?
 *
 * Step one of Mode 1. Everything the full boot needs is already proven
 * on hardware by boot/m1tool.S -- gate array reset, bus request,
 * planting a payload with its own vector table, the single-move.b
 * release -- except this: putting 256 KB into Word RAM from the cart
 * side and having the sub CPU find it there.
 *
 * It is separated out because it has an ordering problem the CD-boot
 * path does not. There, the sub is already running Sega's firmware and
 * sets 2M mode itself (boot/sp.S: `andi.b #0xFA,0xFF8003`) before
 * anyone copies anything. Here the main CPU has to fill Word RAM
 * BEFORE a sub program exists to set the mode, so it runs against
 * whatever the gate array reset leaves behind -- and this ROM does not
 * assume what that is, it reports it.
 *
 * Three questions, one run:
 *
 *   1. Does the main CPU own Word RAM after a gate array reset? It
 *      writes a pattern and reads it back before involving the sub at
 *      all. Failing here means everything after it is meaningless.
 *   2. What is $FF8003 on the sub side at reset? The sub reports it
 *      raw, before touching it, so the mode default is measured rather
 *      than guessed.
 *   3. Does DMNA hand the filled memory across intact? The sub walks
 *      all 65536 longwords and reports the first index that disagrees.
 *
 * The pattern is the longword's own index, so a mismatch index and a
 * mismatch value together say whether the memory is absent, stale,
 * or shifted.
 *
 * No VDP, for the reason boot/m1tool.S gives at length: a wedge that
 * cannot return to the cart menu cannot deliver its own findings,
 * because returning to the menu is what flushes SRAM to the SD card.
 * Progress goes into SRAM instead, and one CRAM write at the very end.
 */

        .equ    GA_RESET,   0xA12001    | b0 SRES (1 = run), b1 SBRQ
        .equ    GA_MEMMODE, 0xA12003    | b7:6 PRG bank, b1 DMNA, b0 RET
        .equ    GA_STAT0,   0xA12020    | sub->main comm words
        .equ    PRG_WIN,    0x420000    | 128 KB of PRG-RAM, Mode 1
        .equ    WRAM_MAIN,  0x600000    | 256 KB of Word RAM, main's view
        .equ    WRAM_SUB,   0x080000    | ...and the sub's view of it
        .equ    WRAM_LONGS, 65536       | 256 KB

        .equ    SRAM_CTL,   0xA130F1    | b0 enable, b1 write protect
        .equ    SRAM_DATA,  0x200001    | odd bytes: byte i at +2*i

        .equ    OK_MARK,    0xC0DE
        .equ    BAD_MARK,   0xBAD0
        .equ    NOWRAM,     0xBAD1      | DMNA never arrived

        .equ    C_GREEN,    0x00E0

        .equ    STAGE,      0xFF0000    | for the emulator's work-RAM dump
        .equ    MARK_AT,    0xFF0100
        .equ    MARK_LOG,   0xFF0110

        .globl  main
main:
        move    #0x2700,sr

        clr.b   MARK_AT
        clr.l   MARK_LOG
        clr.l   MARK_LOG+4
        clr.l   MARK_LOG+8
        clr.l   MARK_LOG+12
        move.b  #0x01,SRAM_CTL          | mapped and writable for the run
        moveq   #1,d3
        bsr     mark

        /* Is there a Mega CD out there at all? Bit 5 reads 0 when
         * something is in the expansion port; the boot ROM signature
         * catches a cartridge carrying its own CD implementation, which
         * is not in that port and will not clear the bit. */
        moveq   #2,d3
        bsr     mark
        btst    #5,0xA10001
        beq     cd_there
        cmpi.l  #0x53454741,0x400100    | 'SEGA' in the CD boot ROM
        beq     cd_there
        moveq   #0x10,d3
        bsr     mark
        bra     _halt
cd_there:

        moveq   #3,d3
        bsr     mark
        bsr     reset_ga

        moveq   #4,d3
        bsr     mark
        moveq   #0x02,d2                | SRES=0 hold in reset, SBRQ=1
        bsr     wait_bus
        bne     fail_nobus

        move.w  #0x0000,0xA12002        | write protect off, PRG bank 0

        /* ---- fill Word RAM from this side, and prove it took ----
         *
         * Read back inside the loop, not after it. If the main CPU does
         * not own Word RAM here the writes go nowhere and every later
         * result is about the wrong thing; catching that now costs one
         * compare per longword and turns a confusing failure into a
         * named one. */
        moveq   #5,d3
        bsr     mark
        lea     WRAM_MAIN,a0
        moveq   #0,d0                   | longword index, and the pattern
1:      move.l  d0,(a0)
        cmp.l   (a0)+,d0
        bne     fail_nowr
        addq.l  #1,d0
        cmpi.l  #WRAM_LONGS,d0
        bne     1b

        /* ---- plant the sub program ----
         *
         * At PRG-RAM zero, because it carries its own vector table: the
         * sub is an ordinary 68000 and fetches SP from 0 and PC from 4.
         * The other sixty-two matter too -- one stray exception and it
         * flies into the weeds with no symptom on this side. */
        moveq   #6,d3
        bsr     mark
        lea     subcode(pc),a0
        lea     PRG_WIN,a1
        move.w  #(subcode_end-subcode-1),d0
2:      move.b  (a0)+,(a1)+
        dbra    d0,2b

        move.w  #0,GA_STAT0             | clear the answer before asking
        move.l  PRG_WIN+4,STAGE+20      | did the plant land?

        /* ---- hand Word RAM over, then let the sub go ----
         *
         * DMNA is bit 1 of the memory mode register, and this is the
         * same write boot/ip.S and iofw/main.c already make on this
         * hardware every boot. It goes before the release rather than
         * after so the sub never sees a moment where the memory is
         * still ours.
         *
         * bset, not a bare move: bits 7:6 of this register are the PRG
         * bank and clobbering them would move the window out from under
         * the payload just planted through it. That is the opposite of
         * the GA_RESET case one line below, where a read-modify-write
         * is the documented silent hang. */
        moveq   #7,d3
        bsr     mark
        bset    #1,GA_MEMMODE           | DMNA: Word RAM to the sub

        /* One write. Bus released and reset deasserted together: bclr
         * then bset is the documented hang, because bset reads SBRQ
         * back as granted and writes that 1 straight out again. */
        move.b  #0x01,GA_RESET          | SBRQ=0, SRES=1: it runs

        /* Give it time without asking it anything. Polling a comm
         * register while the sub runs froze the main 68000 at exactly
         * the third read. A single read is fine; a tight loop is not.
         * 65536 longword compares is a few million cycles. */
        moveq   #8,d3
        bsr     mark
        clr.l   STAGE+4
        move.l  #0x00300000,d0
3:      addq.l  #1,STAGE+4
        subq.l  #1,d0
        bne     3b

        /* Sample everything the sub had to say BEFORE the gate array
         * reset, which puts the comm registers back to a known state. */
        move.w  GA_STAT0,STAGE+16       | verdict
        move.w  0xA12022,STAGE+18       | alive
        move.l  0xA12024,STAGE+24       | first disagreeing index
        move.w  0xA12028,STAGE+28       | $FF8003 as the sub first saw it
        move.l  0xA1202A,STAGE+30       | ...and the value it read there
        move.w  0xA1202E,STAGE+34       | how far the sub got

        bsr     reset_ga
        moveq   #0x02,d2
        bsr     wait_bus
        bne     fail_nobus
        move.b  #0x7A,d3
        bsr     mark

        /* ---- write the findings where a wedge cannot eat them ---- */
        moveq   #9,d3
        bsr     mark
        lea     SRAM_DATA,a1
        lea     STAGE+16,a0
        moveq   #(32-1),d0              | sixteen words of report
4:      move.b  (a0)+,d1
        move.b  d1,(a1)
        cmp.b   (a1),d1
        bne     fail_sram
        addq.l  #2,a1
        dbra    d0,4b

        cmpi.w  #OK_MARK,STAGE+16
        bne     fail_verdict

        move.b  #0xFF,d3
        bsr     mark
        move.w  #C_GREEN,d1
        bsr     colour
        bra     _halt

fail_nobus:
        moveq   #0x70,d3                | 0xF0 without the sign trouble
        bsr     mark
        bra     _halt
fail_nowr:
        move.b  #0xF5,d3                | main cannot write Word RAM
        bsr     mark
        bra     _halt
fail_sram:
        move.b  #0xF2,d3
        bsr     mark
        bra     _halt
fail_verdict:
        /* Not a failure of this ROM: the sub ran and disagreed, and its
         * verdict word says how. Distinct code so the log does not read
         * as a crash. */
        move.b  #0xF6,d3
        bsr     mark
        bra     _halt
_oops:
        moveq   #0x0E,d3
        bsr     mark
_halt:
        stop    #0x2700
        bra     _halt

/* The four writes the gate array recognises as a peripheral reset. */
reset_ga:
        move.w  #0xFF00,0xA12002
        move.b  #0x03,0xA12001
        move.b  #0x02,0xA12001
        move.b  #0x00,0xA12001
        moveq   #0x7F,d0
1:      dbra    d0,1b
        rts

/* Ask for PRG-RAM and wait, bounded. Rewritten every pass, which is the
 * idiom the community settled on and not decoration. Z set on success. */
wait_bus:
        move.l  #0x00080000,d0
1:      move.b  d2,GA_RESET
        btst    #1,GA_RESET
        bne     2f
        subq.l  #1,d0
        bne     1b
        moveq   #1,d0                   | Z clear: never granted
        rts
2:      moveq   #0,d0
        rts

/* Stamp d3 into the progress log: sixteen bytes at SRAM logical 16384,
 * clear of the report at the bottom. Mirrored into work RAM because the
 * emulator harness dumps that and cannot dump cart SRAM. */
mark:
        movem.l d0/a0,-(sp)
        lea     SRAM_DATA+2*16384,a0
        moveq   #0,d0
        move.b  MARK_AT,d0
        add.w   d0,d0
        move.b  d3,0(a0,d0.w)
        lsr.w   #1,d0
        lea     MARK_LOG,a0
        move.b  d3,0(a0,d0.w)
        addq.b  #1,MARK_AT
        andi.b  #0x0F,MARK_AT
        movem.l (sp)+,d0/a0
        rts

colour:
        move.l  #0xC0000000,0xC00004
        move.w  d1,0xC00000
        rts

/* ---- the sub program ----
 *
 * Position independent by construction: the vectors are offsets from
 * the start of the block and everything else is absolute or a short
 * branch. Entered from the reset vector with nothing else in memory.
 */
subcode:
        .long   0x00080000              | initial stack: top of PRG-RAM
        .long   sub_entry-subcode
        .rept   62
        .long   sub_null-subcode
        .endr

sub_null:
        rte

sub_entry:
        move.w  #0x2700,sr
        move.w  #0xBEEF,0xFF8022        | alive, before anything else

        /* Report the memory mode register exactly as found, before
         * touching it. This is the number the design has been guessing
         * at: whether a gate array reset leaves Word RAM in 2M mode and
         * which side owns it. */
        moveq   #0,d0
        move.b  0xFF8003,d0
        move.w  d0,0xFF8028
        move.l  #0,0xFF802A
        move.w  #0x01,0xFF802E          | heartbeat: mode reported

        /* Wait for ownership, bounded.
         *
         * Bit 1, not bit 0. On the sub side of this register bit 0 is
         * RET -- what the sub writes to give Word RAM BACK -- and bit 1
         * is DMNA, which the main sets to hand it over. The first
         * version waited on RET and hung: the register already read
         * 0x02 on the very first look, so the memory was ours before
         * the loop began and the bit being waited on was never going to
         * set. */
        /* At 0x2E, clear of the longword at 0x2A: the first version
                 * put it at 0x2C, inside the four bytes the mismatch value
                 * is read from, and a clean run reported its own heartbeat
                 * as the offending word. */
        move.w  #0x11,0xFF802E          | heartbeat: reached the wait
        move.l  #0x00080000,d1
1:      btst    #1,0xFF8003
        bne     2f
        subq.l  #1,d1
        bne     1b
        move.w  #NOWRAM,0xFF8020
        bra     9f
2:      move.w  #0x22,0xFF802E          | heartbeat: own the memory
        /* Walk every longword. The pattern is the index, so the first
         * disagreement locates itself and its value says whether the
         * memory is absent, stale or shifted. */
        lea     WRAM_SUB,a0
        moveq   #0,d0
3:      cmp.l   (a0)+,d0
        bne     4f
        addq.l  #1,d0
        cmpi.l  #WRAM_LONGS,d0
        bne     3b
        move.l  #0,0xFF8024
        move.w  #0x33,0xFF802E          | heartbeat: walked all of it
        move.w  #OK_MARK,0xFF8020
        bra     9f
4:      move.l  d0,0xFF8024             | index that disagreed
        move.l  -4(a0),0xFF802A         | ...and what was there instead
        move.w  #BAD_MARK,0xFF8020
9:      bra     9b
subcode_end:
