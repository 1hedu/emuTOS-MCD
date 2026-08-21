/* m1tool.S -- lift the Mega CD's internal backup RAM into cart SRAM.
 *
 * Cart in the slot, Sega CD attached, tray empty. No disc is read, no
 * CD BIOS is used and no CD BIOS is even present in this address map:
 * booting from the cartridge is Mode 1, where the Mega CD hardware
 * appears at 0x400000 and its sub CPU comes up held in reset with
 * nothing but garbage in PRG-RAM. That is the easy case. All we want
 * from the sub CPU is that it can see 0xFE0000, which is on its own
 * bus and belongs to no BIOS.
 *
 * The vector table, the software header and the console bring-up are
 * megadev's -- see boot/m1boot.s for why. This file is entered as
 * `main` with the VDP unlocked, work RAM cleared and the Z80 parked.
 *
 * The screen is one colour, because a font is a lot of ROM to spend on
 * a program with a handful of outcomes:
 *
 *   blue     running
 *   white    no Mega CD answered the expansion port at all
 *   yellow   it is there, but the sub CPU never came back
 *   red      the copy came back wrong: SRAM is not being written
 *   magenta  the main CPU took an exception
 *   green    8192 bytes are in SRAM; power off and read the save
 *
 * What lands in SRAM is the backup RAM verbatim, byte for byte from
 * offset zero, so the save file is a filesystem image and not a format
 * of our own invention. tools/bram-extract.py pulls files out of it.
 * A sixteen-byte trailer at the very top of the 32 KB carries a magic,
 * the length and a checksum -- at the top precisely so that it cannot
 * disturb the image at the bottom.
 */

        .equ    GA_RESET,   0xA12001    | b0 SRES (1 = run), b1 SBRQ
        .equ    GA_PROTECT, 0xA12002    | PRG-RAM write protect
        .equ    GA_MEMMODE, 0xA12003    | b7:6 PRG bank
        .equ    GA_STAT0,   0xA12020    | sub->main word
        .equ    PRG_WIN,    0x420000    | 128 KB of PRG-RAM, Mode 1

        .equ    SRAM_CTL,   0xA130F1    | b0 enable, b1 write protect
        .equ    SRAM_DATA,  0x200001    | odd bytes: byte i at +2*i

        .equ    BRAM_BYTES, 8192
        .equ    SRAM_BYTES, 32768
        .equ    SUB_CODE,   0x000200    | where the sub program is put
        .equ    SUB_DEST,   0x010000    | ...and where it puts the copy
        .equ    DONE_MARK,  0xC0DE

        .equ    C_BLUE,     0x0E00
        .equ    C_YELLOW,   0x00EE
        .equ    C_RED,      0x000E
        .equ    C_GREEN,    0x00E0
        .equ    C_WHITE,    0x0EEE
        .equ    C_MAGENTA,  0x0E0E

        .equ    STAGE,      0xFF0000    | progress, for --dump-wram
        /* The mark index lives in work RAM, not in the ROM it is
         * written from, and is cleared here rather than trusted:
         * megadev clears work RAM only on a cold boot, and a flash cart
         * handing over from its menu is not one. */
        .equ    MARK_AT,    0xFF0100
        /* The same sixteen bytes, mirrored into work RAM. Not for the
         * console -- for the emulator, whose harness dumps work RAM and
         * cannot dump cart SRAM. Without it the only thing testable here
         * is how MANY marks were stamped, not which ones. */
        .equ    MARK_LOG,   0xFF0110

/* ---- main: megadev's init has already run ------------------------ */
        .globl  main
main:
        move    #0x2700,sr

        /* The VDP is not touched. Not one register, not one word of
         * VRAM.
         *
         * Six builds were spent on it -- a display left off, a Z80 bus
         * request that hung, a write placed before the TMSS handshake,
         * a pending command the flash cart's menu left half-written --
         * and the last of them cleared the screen to black and wedged
         * the machine so hard that reset would not return to the cart
         * menu. Which matters more than the black screen: returning to
         * the menu is how this cart writes its save file to the SD
         * card, so a run that cannot get back there cannot deliver the
         * dump even when the dump worked.
         *
         * So the progress log goes where the payload goes. SRAM is
         * mapped now and stays mapped, and each phase stamps a byte
         * into it. If the machine wedges, the last stamp says where --
         * and it says it on the card, not on a television.
         *
         * One CRAM write at the very end, after everything is safely in
         * SRAM, as a courtesy. If it works you see green; if it does
         * not, nothing is lost. */
        clr.b   MARK_AT
        clr.l   MARK_LOG
        clr.l   MARK_LOG+4
        clr.l   MARK_LOG+8
        clr.l   MARK_LOG+12
        move.b  #0x01,SRAM_CTL          | mapped and writable, for the run
        moveq   #1,d3
        bsr     mark

        /* ---- is there a Mega CD out there at all? ----
         *
         * Two checks because neither is sufficient on its own: bit 5 of
         * the version register reads 0 when something is in the
         * expansion port, but a cartridge carrying its own CD
         * implementation is not in that port and will not clear it. The
         * boot ROM's own signature at 0x400100 catches that case. */
        moveq   #2,d3
        bsr     mark
        btst    #5,0xA10001
        beq     cd_there
        cmpi.l  #0x53454741,0x400100    | 'SEGA' in the CD boot ROM
        beq     cd_there
        moveq   #0x10,d3                | 0x10: nothing answered out there
        bsr     mark
        bra     _halt
cd_there:

        /* ---- reset the gate array ----
         *
         * Not optional and not a delay loop with a write in it: this
         * exact sequence of four writes is what the gate array
         * recognises as a reset, and the LaserActive will not come up
         * without it. Nothing I wrote before this had it at all, which
         * is the difference between initialising the hardware and
         * hoping it is already initialised.
         *
         * 0xA12002 as a word, because the reset and bus-request bits
         * live in the LOW byte at 0xA12001 and a byte access to
         * 0xA12000 sets the wrong ones. */
        move.b  #3,STAGE
        moveq   #3,d3
        bsr     mark
        bsr     reset_ga

        /* ---- hold it in reset and take its memory ----
         *
         * The request is rewritten on every pass of the wait, which is
         * the community idiom and not decoration: a single write and a
         * poll is the version that hangs on some units. */
        move.b  #4,STAGE
        moveq   #4,d3
        bsr     mark
        moveq   #0x02,d2                | SRES=0 hold in reset, SBRQ=1
        bsr     wait_bus
        bne     fail_nobus

        move.w  #0x0000,0xA12002        | write protect off, PRG bank 0

        move.b  #5,STAGE
        moveq   #5,d3
        bsr     mark
        /* ---- plant the sub program ---- */
        /* To PRG-RAM zero, because the payload begins with its own
         * vector table. The sub CPU is an ordinary 68000: it fetches
         * its stack from 0 and its PC from 4, and the other sixty-two
         * vectors are whatever garbage the last session left in RAM.
         * One stray exception and it flies into the weeds with no
         * symptom at all on this side.
         *
         * Writing just the two longwords, as the first version did,
         * left those sixty-two pointing at nothing. */
        lea     subcode(pc),a0
        lea     PRG_WIN,a1
        move.w  #(subcode_end-subcode-1),d0
3:      move.b  (a0)+,(a1)+
        dbra    d0,3b

        move.w  #0,GA_STAT0             | clear the answer before asking
        move.l  PRG_WIN+4,STAGE+20      | did the plant actually land?
        move.w  PRG_WIN+(sub_entry-subcode),STAGE+24

        /* ---- and let it go ----
         *
         * Bus first, then reset, as two writes. One write that does
         * both at once asks the gate array to hand the memory back and
         * start the CPU in the same instruction. */
        move.b  #6,STAGE
        moveq   #6,d3
        bsr     mark
.ifndef NOSUB
        /* One write. Bus released and reset deasserted together.
         *
         * bclr then bset looked safer and is the documented silent
         * hang: bset is a read-modify-write, so it reads the register
         * back -- SBRQ still reading as granted -- and writes that 1
         * straight back out. The sub CPU comes out of reset into a bus
         * it does not have and executes nothing, which is exactly what
         * it did: planted correctly, released correctly, never ran a
         * single instruction. */
        move.b  #0x01,GA_RESET          | SBRQ=0, SRES=1: it runs
.endif

        move.b  #7,STAGE
        moveq   #7,d3
        bsr     mark
.ifdef NOSUB
        /* Bisect mode: never start the sub CPU, and read back the
         * buffer it would have filled. Proves the main-side path --
         * second bus grab, PRG window, SRAM write, verify -- with the
         * sub taken out of the picture entirely. */
        bra     8f
.endif
        /* Give it time, without asking it anything.
         *
         * The first version polled 0xA12020 in a tight loop while the
         * sub CPU was running, and Genesis Plus GX froze the main 68000
         * on the third read -- every time, at exactly three. Whatever
         * that is, the handshake does not need it: 8192 byte copies is
         * a few milliseconds, so wait on a counter that touches nothing
         * but work RAM, then halt the sub and ask once with the bus in
         * hand. */
        clr.l   STAGE+4
        move.l  #0x00040000,d0
4:      addq.l  #1,STAGE+4
        subq.l  #1,d0
        bne     4b
5:
        /* ---- take the memory back and read the copy ---- */
        /* Read the sub's answer BEFORE resetting the gate array,
         * because the reset puts the comm registers back to a known
         * state and the answer is in one of them. Comm registers are
         * readable from this side without holding any bus. */
        move.w  GA_STAT0,STAGE+16       | what the sub said, if anything
        move.w  0xA12022,STAGE+18       | ...and whether it started at all

        /* Then take the memory back the only way that has ever worked
         * here: the full gate array reset, then the same request as the
         * first time.
         *
         * Everything invented instead of this has cost a build -- 0x03
         * to leave the sub running, a halt-and-settle before asking,
         * a read-modify-write that quietly re-requested the bus. The
         * sub's work is finished by now; there is nothing left to
         * preserve on that side except the eight kilobytes it wrote,
         * and a gate array reset does not touch RAM. */
        bsr     reset_ga
        moveq   #0x02,d2
        bsr     wait_bus
        bne     fail_nobus
        move.b  #0x7A,STAGE
        move.b  #0x7A,d3
        bsr     mark
        move.w  #0x0000,0xA12002        | bank 0 back in the window
.ifndef NOSUB
        /* Now ask, once, with the bus ours and the sub stopped. */
        cmpi.w  #DONE_MARK,STAGE+16     | sampled before the gate array reset
        bne     fail_nosub
.endif

        /* ---- into SRAM, and read every byte back ----
         *
         * Verified inline rather than afterwards. A flash cart with the
         * wrong save type configured accepts the writes and returns
         * them from a RAM that is never written to the card, so a
         * readback proves less than it looks like -- but a readback
         * that FAILS proves the write never happened at all, which is
         * the failure worth catching here. */
        /* 0x03, not 0x01. Bit 0 maps SRAM in, bit 1 ENABLES writes --
         * 0x01 maps it read-only, which would have accepted the whole
         * loop and stored nothing. */
        move.b  #8,STAGE
        moveq   #8,d3
        bsr     mark
        /* 0x01. Bit 0 maps SRAM in; bit 1 is write PROTECT, not write
         * enable -- the research report has that backwards. 0x03 fails
         * the readback and 0x01 passes it, tested on the core one build
         * apart, which is why the readback is in the loop at all. */
        move.b  #0x01,SRAM_CTL          | mapped, and writable
        lea     PRG_WIN+SUB_DEST,a0
        lea     SRAM_DATA,a1
        move.w  #(BRAM_BYTES-1),d0
6:      move.b  (a0),d1
        move.b  d1,(a1)
        cmp.b   (a1),d1
        bne     fail_sram
        addq.l  #1,a0
        addq.l  #2,a1
        dbra    d0,6b

        /* ---- trailer, at the top where it disturbs nothing ---- */
        lea     PRG_WIN+SUB_DEST,a0
        moveq   #0,d2                   | checksum
        move.w  #(BRAM_BYTES-1),d0
7:      moveq   #0,d1
        move.b  (a0)+,d1
        add.l   d1,d2
        rol.l   #1,d2
        dbra    d0,7b

        lea     SRAM_DATA+2*(SRAM_BYTES-16),a1
        move.b  #'M',(a1)
        move.b  #'C',2(a1)
        move.b  #'D',4(a1)
        move.b  #'B',6(a1)
        move.b  #(BRAM_BYTES>>8),8(a1)
        move.b  #(BRAM_BYTES&0xFF),10(a1)
        /* High byte first, like the length field above it. The first
         * version swapped and then rotated the low word, which stored
         * bits 23:16 before 31:24 -- the right checksum, backwards. */
        move.l  d2,d3
        rol.l   #8,d3
        move.b  d3,12(a1)               | bits 31:24
        rol.l   #8,d3
        move.b  d3,14(a1)               | bits 23:16

        move.b  #0xFF,STAGE
        move.b  #0xFF,d3
        bsr     mark
        move.w  #C_GREEN,d1
        bsr     colour
        bra     _halt

fail_nobus:
        move.b  #0xF0,STAGE
        move.b  #0xF0,d3
        bsr     mark
        bra     1f
fail_nosub:
        move.b  #0xF1,STAGE
        move.b  #0xF1,d3
        bsr     mark
1:      bra     _halt
fail_sram:
        move.b  #0xF2,STAGE
        move.b  #0xF2,d3
        bsr     mark
        bra     _halt
_oops:
        move.b  #0xEE,STAGE+1           | leave the last good stage alone
        moveq   #0x0E,d3                | 0x0E: the main CPU took an exception
        bsr     mark
_halt:
        stop    #0x2700
        bra     _halt

/* The four writes the gate array recognises as a peripheral reset. It
 * puts the comm registers, the memory-mode register and the sub-CPU
 * reset logic into a known state; the LaserActive will not come up
 * without it and it costs nothing anywhere else. RAM is untouched. */
reset_ga:
        move.w  #0xFF00,0xA12002
        move.b  #0x03,0xA12001
        move.b  #0x02,0xA12001
        move.b  #0x00,0xA12001
        moveq   #0x7F,d0
1:      dbra    d0,1b
        rts

/* Ask for PRG-RAM and wait, bounded. d2 is the byte to write: 0x02 to
 * take it with the sub held in reset, 0x03 to take it from a sub that
 * is running. Rewritten every pass, which is the idiom the community
 * settled on and not decoration. Z set on success. */
wait_bus:
        addq.l  #1,STAGE+12
        clr.l   STAGE+8
        move.l  #0x00080000,d0
1:      addq.l  #1,STAGE+8
        move.b  d2,GA_RESET             | keep asking, every pass
        btst    #1,GA_RESET
        bne     2f
        subq.l  #1,d0
        bne     1b
        moveq   #1,d0                   | Z clear: never granted
        rts
2:      moveq   #0,d0                   | Z set
        rts

/* Stamp d3 into the progress log in SRAM, and advance.
 *
 * Sixteen bytes just past the eight kilobytes of dump, so the log and
 * the payload never collide. SRAM is odd-byte: logical byte N lives at
 * 0x200001 + 2N. */
mark:
        movem.l d0/a0,-(sp)
        lea     SRAM_DATA+2*BRAM_BYTES,a0
        moveq   #0,d0
        move.b  MARK_AT,d0
        add.w   d0,d0
        move.b  d3,0(a0,d0.w)
        lsr.w   #1,d0
        lea     MARK_LOG,a0
        move.b  d3,0(a0,d0.w)
        addq.b  #1,MARK_AT
        andi.b  #0x0F,MARK_AT           | sixteen, then it wraps
        movem.l (sp)+,d0/a0
        rts

/* One CRAM write, at the end, once the dump is safe. Never before. */
colour:
        move.l  #0xC0000000,0xC00004
        move.w  d1,0xC00000
        rts


/* ---- the sub program ----
 *
 * Copied into PRG-RAM and entered from the reset vector, so it must be
 * position independent: absolute addressing and short branches only,
 * nothing PC-relative that points at itself.
 *
 * The internal backup RAM is 8 KB living on odd bytes at 0xFE0000, on
 * the sub CPU's own bus. No BIOS mediates it and nothing needs
 * unlocking to read it. */
subcode:
        /* Sub-CPU payload, assembled for the sub's view of PRG-RAM
         * starting at zero. Position independent by construction: the
         * vectors are computed as offsets from the start of this block
         * and everything else is absolute or a short branch. */
        .long   0x00080000              | initial stack: top of PRG-RAM
        .long   sub_entry-subcode       | initial PC
        .rept   62
        .long   sub_null-subcode        | and every other vector, safely
        .endr

sub_null:
        rte

sub_entry:
        move.w  #0x2700,sr              | polled, never interrupted
        move.w  #0xBEEF,0xFF8022        | "I am alive", before anything else
        lea     0xFE0001,a0             | internal backup RAM, odd bytes
        lea     SUB_DEST,a1
        move.w  #(BRAM_BYTES-1),d0
1:      move.b  (a0),(a1)+
        addq.l  #2,a0
        dbra    d0,1b
        move.w  #DONE_MARK,0xFF8020

        /* Spin, as the reference payload does.
         *
         * This was a `stop` for a while, on the theory that a sub CPU
         * spinning on `bra *` keeps fetching from the PRG-RAM the main
         * CPU wants and blocks the grant. That theory was formed while
         * the sub CPU was not executing at all -- the vector table was
         * missing and the release was a read-modify-write -- so the
         * evidence for it was a machine that had never run this code.
         * With the sub actually running, a halted CPU is the thing that
         * does not answer a bus request. */
2:      bra     2b
subcode_end:
        .even
