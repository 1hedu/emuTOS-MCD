/* m1emu.S -- boot EmuTOS on the sub CPU from the cartridge.
 *
 * Step two of Mode 1. The CD is not the boot device: the cart is. This
 * ROM carries EmuTOS and the A: ramdisk, plants both in the Mega CD's
 * memory, and starts the sub 68000 on them.
 *
 * EmuTOS still runs on the sub, which is the whole point of the port --
 * address 0 has to be RAM for the ST low-memory ABI to be byte-exact,
 * and on this side of the bus address 0 is ROM. The cart is where
 * EmuTOS lives, not where it runs.
 *
 * Everything here is proven except the arrangement of it:
 *
 *   gate array reset, bus request, planting a payload, the one-write
 *     release      -- boot/m1tool.S, on hardware
 *   Word RAM filled from the cart and handed across with DMNA
 *                  -- boot/m1wram.S, in emulation
 *   0x080000 and 0x060000 as the destinations, and the jump into the
 *     image's TOS header
 *                  -- boot/sp.S Op_BootEmutos, which does exactly this
 *                     from the disc side every boot
 *
 * There is no bootstrap program. The sub CPU is an ordinary 68000: it
 * fetches its stack pointer from 0 and its PC from 4. Writing 0x080000
 * into the second longword IS the `jmp 0x80000` that Op_BootEmutos
 * performs, minus the code to perform it.
 *
 * Success is visible without a display. EmuTOS writes a boot trace to
 * $FF8020 as it comes up -- E001 _main entered, E002 probes done, E003
 * entering meminit, E004 meminit done, E005 biosmain -- and the main
 * CPU can read that at $A12020 without owning any bus, so the sub keeps
 * running while it is read. E005 in the SRAM log means EmuTOS booted on
 * the sub CPU out of a cartridge.
 *
 * No VDP: see boot/m1tool.S. A wedge that cannot reach the cart menu
 * cannot deliver its own findings.
 */

        .equ    GA_RESET,   0xA12001    | b0 SRES (1 = run), b1 SBRQ
        .equ    GA_MEMMODE, 0xA12003    | b7:6 PRG bank, b1 DMNA, b0 RET
        .equ    GA_STAT0,   0xA12020    | sub->main comm words
        .equ    PRG_WIN,    0x420000    | 128 KB of PRG-RAM, Mode 1
        .equ    WRAM_MAIN,  0x600000    | 256 KB of Word RAM, main's view

        .equ    EMU_SUB,    0x00080000  | where EmuTOS lives and starts
        .equ    ADISK_BANK, 3           | sub 0x060000 is PRG bank 3
        .equ    SUB_STACK,  0x00080000
        .equ    VEC_STUB,   0x00000400  | past the vector table, before
                                        | EmuTOS claims the sysvars

        .equ    SRAM_CTL,   0xA130F1
        .equ    SRAM_DATA,  0x200001    | odd bytes: byte i at +2*i

/* The cartridge's save RAM is a drive now -- C:, ahead of the disc --
 * so this loader can no longer scribble its own findings across the
 * front of it. 32768 bytes of it, one 512-byte sector reserved at the
 * top for the loader and the other 63 given to the filesystem.
 *
 * The reservation is not tidiness. This report is the only thing that
 * can speak when the boot fails before IOFW exists, so it has to
 * survive; and a report written over sector 0 is a report written over
 * the boot sector of the volume it is reporting about. */
        .equ    CART_FS_SECTORS, 63
        .equ    LOADER_AT,  CART_FS_SECTORS*512     | byte offset, reserved
        .equ    RPT_AT,     SRAM_DATA+2*LOADER_AT
        .equ    MARKS_AT,   SRAM_DATA+2*(LOADER_AT+64)
        .equ    GA_CART_CNT, 0xA1201A               | main->sub: C: sectors

/* Waking the drive with the console's own firmware.
 *
 * On a CD boot the Mega CD's BIOS has spun the drive up and read the
 * table of contents before EmuTOS ever sees the hardware. In Mode 1
 * nothing has, and a hardware diagnostic run says exactly what that
 * costs: drive state 0 and 7 round after round -- which Sega's own
 * handler table calls "known to be nothing", not "unknown" -- the
 * sector ring never taking a single entry, no header ever decoded.
 *
 * Pier Solar boots from a cartridge and reads a CD, and does not solve
 * this by writing a cold-start CDD sequence. It decompresses the sub
 * CPU's CDBIOS out of the console's own BIOS ROM into PRG-RAM and lets
 * Sega's firmware do it. So does the BIOS itself, at ROM 0x656:
 *
 *      lea 0x15800,a0 ; lea 0x20000,a1 ; bsr <decompressor>
 *      lea 0x13400,a0 ; lea 0x26000,a1 ; bsr <decompressor>
 *      lea 0x19800,a0 ; lea 0x38000,a1 ; bsr <decompressor>
 *      move.b #42,(fp)                      <- the write protect
 *      bset #0,(a5) ; poll                  <- out of reset
 *      bclr #1,(a5) ; poll                  <- and give the bus back
 *
 * Three, and this used to unpack the first. Neither the blobs nor the
 * decompressor is at a fixed address across regions and neither needs
 * a table: the three-instruction shape above occurs three times in
 * every image here, so one scan finds all of them and a BIOS revision
 * nobody has tabulated is found too.
 *
 * The decompressor is 159 bytes of (a0)+ to (a1)+ with a stack word for
 * its bit buffer, dbf and short branches only -- no absolute operand,
 * no data reference, nothing that cares where it runs. So it can be
 * called in place at 0x400000 + its offset.
 *
 * This is not redistribution and nothing is transcribed: the console
 * unpacks its own firmware, out of its own ROM, into its own RAM.
 * tools/subbios.py does the same thing on a host, for reading.
 *
 * Unconditional. It was gated on Down, and the gate saved the DRV_INIT
 * timeout on a boot with an empty tray -- megadev's note is that
 * DRV_INIT waits for a DRV_OPEN request when there is no disc, so it
 * simply never returns. The wait here is bounded, so that costs time
 * and nothing else, and it cost less than a button whose registering
 * could not be seen from outside the machine. */
        .equ    BIOS_ROM,   0x400000    | the CD boot ROM, Mode 1
        .equ    SUB_CDBIOS, 0x005F22    | firmware entry, d0 = function
        .equ    BIOS_DRV_INIT, 0x0010   | close the tray, read the TOC
        .equ    SUB_WAKE,   0x6000      | where a sub program lives
        .equ    BIOS_CDBSTAT,  0x0081   | ask the drive its status
        .equ    BIOS_FDRSET,   0x0085   | fader, after every DRV_INIT
        .equ    BIOS_CDCSTOP,  0x0089
        .equ    BIOS_ROMREAD,  0x0020   | read sectors, a0 = { lba, count }
        .equ    BIOS_CDCSTAT,  0x008A   | ...carry clear when one is ready
        .equ    WAKE_CALL,  0xD09C      | the stub ran, DRV_INIT is away
        .equ    WAKE_DRV,   0xD09D      | ...came back, now trying a read
        /* WAKE_DONE is the floor of the terminal words, not a state.
         * It was written before the read attempt and compared with
         * bcc, so the loader called the wake finished the moment
         * DRV_INIT returned and never waited for the verdict. */
        .equ    WAKE_DONE,  0xD09E
        .equ    WAKE_RD1,   0xD09F      | ...and the firmware read LBA 1600
        .equ    WAKE_RD2,   0xD0A0      | ...at 1600+150, so our base is wrong
        .equ    WAKE_NORD,  0xD0A1      | ...and could not read either
        .equ    WAKE_RDT,   0xD0A2      | ...1750 worked and so does 1600 now
        .equ    TEST_LBA,   1600        | where our own driver always fails
        .equ    GA_STAT7,   0xA1202E    | ...read here; sub writes 0xFF802E
        .equ    BOOTFLAGS,  0xFF09B0    | the pad word, kept for the wake
        /* One character, on the screen, for the whole of the wake.
         *
         * The marks tell the whole story and they live in cartridge
         * save RAM, which means power off, pull the cart, read the card
         * -- to answer "did the button register". That is not a
         * diagnostic, it is a chore, and it made every wake test cost a
         * round trip. IOFW prints this byte on the status line as W,
         * so the answer is on the television before the desktop is. */
        .equ    WAKE_SAY,   0xFF09B2

        .equ    TRACE_OK,   0xE005      | EmuTOS reached biosmain

        .equ    C_GREEN,    0x00E0

/* Everything this loader leaves behind for someone else to read lives
 * in one page, 0xFF0900..0xFF09FF, and it lives there because the old
 * addresses were inside somebody's buffer.
 *
 * STAGE was 0xFF0000, the marks 0xFF0100, the Mode 1 flag 0xFF0140 --
 * and iofw/main.c has CDSECT_WRAM at 0xFF0000 with 2048 bytes of disc
 * sector in it. Every one of those was sitting under it. The marks
 * disappearing partway through a boot was that, not a mystery; and
 * M1_FLAG is read inside IOFW's main loop, so a sector landing on it
 * would have changed how the servant behaves halfway through a run.
 *
 * 0xFF0900 is clear: CDSTAT_WRAM starts at 0xFF0800 and is 36 words,
 * the PRG dump starts at 0xFF0A00. A whole page for a hundred bytes,
 * because the last arrangement was tight and wrong. */
        .equ    STAGE,      0xFF0900    | ...through 0xFF097F
        .equ    MARK_AT,    0xFF0980
        .equ    MARK_LOG,   0xFF0990

        .globl  main
main:
        move    #0x2700,sr

        clr.b   MARK_AT
        clr.l   MARK_LOG
        clr.l   MARK_LOG+4
        clr.l   MARK_LOG+8
        clr.l   MARK_LOG+12
        move.b  #0x01,SRAM_CTL
        moveq   #1,d3
        bsr     mark

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

        /* ---- the boot-flag word, before the sub can read it ----
         *
         * EmuTOS samples $FF8012 early in its own startup and spins
         * until the high byte reads 0x5A, then takes its options from
         * the low byte. On a CD boot IOFW publishes that, and publishing
         * it late is how a direction had to be held for thirty seconds
         * to register at all. Here the cartridge is the only thing
         * running, so it writes the word before the sub exists to read
         * it -- and a button tapped at power-on is simply there.
         *
         * The bit assignments and the read sequence are IOFW's, from
         * iofw/input.c and the boot-flag publisher in iofw/main.c, so
         * that a button means the same thing whichever boot planted the
         * word:
         *
         *   TH=1 -> ~data b0 Up b1 Down b2 Left b3 Right b4 B b5 C
         *   TH=0 -> ~data b4 A  b5 Start
         *
         *   flag b0 A      link passive, no drive commands at all
         *        b1 B      the decoder's reset-time CTRL pair
         *        b2 Down   the D: diagnostic instead of the desktop
         *        b3 Left   command reads two frames early, not twenty
         *        b4 Up     the sector self-test
         *        b5 Right  hand D: to the parked firmware
         *
         * IOFW republishes this word from its own pad read a moment
         * later, which is harmless -- the sub has already taken its
         * copy -- and is also why it cannot be the only publisher: by
         * then the sub is several thousand instructions into its own
         * startup and has long since read the register. */
        move.b  #0x40,0xA10009          | TH is an output
        move.b  #0x40,0xA10003
        nop
        nop
        move.b  0xA10003,d0             | the TH=1 half
        move.b  #0x00,0xA10003
        nop
        nop
        move.b  0xA10003,d1             | the TH=0 half
        move.b  #0x40,0xA10003          | leave the port as it was found
        not.b   d0                      | the pad is active low
        not.b   d1
        moveq   #0,d2
        btst    #4,d1                   | A
        beq     9f
        bset    #0,d2
9:      btst    #4,d0                   | B
        beq     9f
        bset    #1,d2
9:      btst    #1,d0                   | Down
        beq     9f
        bset    #2,d2
9:      btst    #2,d0                   | Left
        beq     9f
        bset    #3,d2
9:      btst    #0,d0                   | Up
        beq     9f
        bset    #4,d2
9:      btst    #3,d0                   | Right
        beq     9f
        bset    #5,d2
9:      ori.w   #0x5A00,d2              | 0x5A: this word has been answered
        move.w  d2,0xA12012
        move.w  d2,STAGE+52
        move.w  d2,BOOTFLAGS
        move.b  #'-',WAKE_SAY           | until something says otherwise

        /* ---- wake the drive, with the console's own firmware ----
         *
         * Before the payloads, not after: the sub runs Sega's BIOS for
         * a few seconds here, and there is no reason to have EmuTOS
         * already sitting in Word RAM while it does.
         *
         * Unconditional. It was gated on Down, and the gate bought one
         * thing -- an empty tray costs the DRV_INIT timeout, and there
         * is no reason to pay that on a boot that only wants a desktop.
         * What it cost was worse: a combination nobody can confirm from
         * the outside. On hardware the status byte read '-' on every
         * attempt, meaning this test was false every time, and the pad
         * read above is the same sequence IOFW runs successfully
         * thousands of times a second -- so the fault is somewhere in
         * one early read that would take a round trip per hypothesis to
         * find.
         *
         * Not worth finding. The wait is bounded, so an empty tray
         * still reaches a desktop; it just takes longer. A drive that
         * wakes on every boot is worth more than a button that saves a
         * few seconds on the boots that do not need it. */
        moveq   #0x11,d3
        bsr     mark
        bsr     wake_drive
        /* Put the machine back the way the rest of this loader expects
         * to find it.
         *
         * Sega's firmware has just been running on the sub for several
         * seconds and it does not leave the gate array alone: Word RAM
         * ownership and the memory mode are its business while it runs,
         * and everything below here assumes the state a gate array
         * reset leaves -- 2M, main owning Word RAM, because that is what
         * boot/m1wram.S measured. Taking only the bus back leaves the
         * EmuTOS copy writing into memory it does not own, which fails
         * silently and surfaces as a checksum mismatch three steps
         * later.
         *
         * The drive does not care. Resetting the gate array resets the
         * sub side of the ASIC; the disc keeps spinning and the drive
         * keeps its own focus and tracking, which is the entire point
         * of having woken it. */
        moveq   #0x15,d3
        bsr     mark
        bsr     reset_ga
        moveq   #0x02,d2                | and take the bus back
        bsr     wait_bus
        bne     fail_nobus
        moveq   #0x16,d3
        bsr     mark

        /* ---- park the firmware, so it survives EmuTOS ----
         *
         * The CD branch photographs the running CDBIOS into Word RAM at
         * sub 0xBA000 on its way past (boot/sp.S), and segacd.c's whole
         * timeshare hangs off finding it there. A Mode 1 boot has never
         * parked anything, which is why segacd_bios_probe() answers
         * 0xFFFF and two of the diagnostic's eight arms are inert.
         * There is a copy sitting in PRG bank 0 right now and nothing
         * has overwritten it yet.
         *
         * 24 KB, sub 0x0000-0x5FFF, which is what fits the slot and is
         * also what the firmware actually needs: blob 1 is the CDBIOS
         * core, blob 2 is its own sub program (we replaced it) and blob
         * 3 is a named overlay bank, mc_BNK02.S28, the only one in the
         * ROM.
         *
         * After the wake, not after the unpack. The entry vector at
         * 0x5F22 is not in any blob -- the firmware's own startup
         * builds it, blob 1 at 0x3C2 doing `lea 0x5f22,a0 / lea
         * 0x2e18,a1` -- so a copy taken before the sub ran would be a
         * CDBIOS with no way in. */
        movea.l #PRG_WIN,a0                     | sub 0x0000, bank 0
        movea.l #WRAM_MAIN+(0xBA000-0x80000),a1
        move.w  #(0x6000/4)-1,d0
1:      move.l  (a0)+,(a1)+
        dbra    d0,1b
        /* Same test bios_parked() makes, so a park that did not take
         * is caught here rather than three layers away. */
        move.l  WRAM_MAIN+(0xBA000-0x80000)+4,d0
        move.l  d0,STAGE+68
        cmpi.l  #0x200,d0
        bcs     2f
        cmpi.l  #0x6000,d0
        bcc     2f
        moveq   #0x1E,d3                        | parked and plausible
        bsr     mark
        bra     no_wake
2:      move.b  #0xFB,d3                        | parked, but not firmware
        bsr     mark
no_wake:

        /* ---- EmuTOS into Word RAM ----
         *
         * 2M mode is the reset default and the main CPU owns Word RAM
         * at this point -- both measured by boot/m1wram.S rather than
         * assumed, because nothing has run on the sub yet to configure
         * either. So this is a flat copy into a 256 KB window with no
         * paging and no handshake in front of it. */
        moveq   #5,d3
        bsr     mark
        /* Absolute, not PC-relative: the payload is a quarter of a
         * megabyte further down the ROM and a PC-relative displacement
         * is sixteen signed bits. The ROM is linked at zero and the
         * cart is at zero, so an absolute label is its own address. */
        lea     emutos_img,a0
        lea     WRAM_MAIN,a1
        move.w  #(emutos_end-emutos_img)/4-1,d0
1:      move.l  (a0)+,(a1)+
        dbra    d0,1b

        /* Checked by summing both sides rather than comparing in the
         * loop: a compare would double a quarter-megabyte copy, and a
         * mismatch here is not a subtle fault -- it is Word RAM not
         * being ours, which is one number either way. */
        lea     emutos_img,a0
        lea     WRAM_MAIN,a1
        moveq   #0,d1
        moveq   #0,d2
        move.w  #(emutos_end-emutos_img)/4-1,d0
2:      add.l   (a0)+,d1
        add.l   (a1)+,d2
        dbra    d0,2b
        move.l  d1,STAGE+40
        move.l  d2,STAGE+44
        cmp.l   d1,d2
        bne     fail_wram

        /* ---- the A: ramdisk into PRG-RAM bank 3 ----
         *
         * Sub 0x060000 is bank 3 of four 128 KB banks, and the Mode 1
         * window shows one bank at a time, so bank 3 at window offset
         * zero is exactly sub 0x060000. This is where boot/sp.S puts it
         * when it reads it off the disc. */
        moveq   #6,d3
        bsr     mark
        move.b  #(ADISK_BANK<<6),GA_MEMMODE
        lea     adisk_img,a0
        lea     PRG_WIN,a1
        move.w  #(adisk_end-adisk_img)/4-1,d0
3:      move.l  (a0)+,(a1)+
        dbra    d0,3b

        /* ---- the vector table, and nothing else ----
         *
         * Bank 0 back in the window so PRG-RAM zero is reachable. Two
         * longwords do the work: the stack, and a PC of 0x080000 --
         * which is the jump Op_BootEmutos makes, expressed as a reset
         * vector instead of an instruction.
         *
         * The other sixty-two are not decoration. Between the sub
         * leaving reset and EmuTOS installing its own table, one stray
         * exception through a garbage vector goes somewhere unknowable
         * with no symptom on this side. They point at a two-word stub
         * at 0x400, past the vector table and below anything EmuTOS
         * cares about until it is running -- by which time it owns
         * these vectors anyway. */
        moveq   #7,d3
        bsr     mark
        /* Bank 0 and, in the same word, the write protect off.
         *
         * reset_ga leaves 0xFF in the high byte of this register, which
         * protects the first 0xFF * 0x200 bytes of PRG-RAM -- 130560 of
         * them -- from the SUB CPU. The cart writes through its own
         * window and is unaffected, so the vector table plants
         * perfectly and the fault appears much later and somewhere
         * else: EmuTOS's very first act is to install an exception
         * vector at address 16 and then deliberately raise that
         * exception, and with the write swallowed it faults through
         * whatever the cart left there instead. A silent stop at
         * trace E011, three layers from the cause.
         *
         * Measured, not guessed: startup.S writes 0x5A5AA5A5 to 16 and
         * reads back 0x00000400, which is the stub address the cart
         * planted. On a CD boot the firmware clears this before the sub
         * ever runs; in Mode 1 nothing does. */
        move.w  #0x0000,0xA12002        | write protect off, bank 0
        move.l  #SUB_STACK,PRG_WIN+0
        move.l  #EMU_SUB,PRG_WIN+4
        lea     PRG_WIN+8,a1
        move.w  #62-1,d0
4:      move.l  #VEC_STUB,(a1)+
        dbra    d0,4b
        move.w  #0x4E72,PRG_WIN+VEC_STUB        | stop #0x2700
        move.w  #0x2700,PRG_WIN+VEC_STUB+2
        move.w  #0x60FA,PRG_WIN+VEC_STUB+4      | bra.s back to it

        move.l  PRG_WIN+4,STAGE+20      | did the vector land?

        /* ---- hand Word RAM over, then let it go ----
         *
         * bset, because bits 7:6 of this register are the PRG bank and
         * a bare move would swing the window. The opposite of the line
         * below it, where a read-modify-write is the documented hang:
         * bset on GA_RESET reads SBRQ back as granted and writes that 1
         * straight out again, and the sub never runs. */
        moveq   #8,d3
        bsr     mark


        /* ---- how big C: is, answered before the sub can ask ----
         *
         * EmuTOS takes this word in segacd_floppy_init, which is early
         * in its own startup -- and IOFW does not exist yet, because
         * the cart releases the sub and only afterwards jumps into the
         * servant. A count the servant publishes arrives after the
         * question. Exactly the boot flag's problem, and the same fix:
         * the thing that is already running answers.
         *
         * 32 KB is not a measurement, it is this ROM's own header
         * (0x200001..0x20FFFF), so the size is known and only the
         * presence is in doubt. Both ends of the window are written and
         * read back, and put back afterwards; a cart with no save RAM
         * fitted fails the first one and C: simply does not appear. */
        moveq   #0,d2
        lea     SRAM_DATA,a0
        bsr     sram_holds
        beq     9f
        lea     SRAM_DATA+2*(CART_FS_SECTORS*512-1),a0
        bsr     sram_holds
        beq     9f
        move.w  #CART_FS_SECTORS,d2
9:      move.w  d2,GA_CART_CNT
        move.w  d2,STAGE+54

        /* And the flag that tells IOFW this is a Mode 1 boot, planted
         * below its relocation target so its own copy cannot land on
         * it. Without it IOFW waits forever for an SP that does not
         * exist. */
        move.l  #0x4D314F4B,0xFF09A0    | 'M1OK'

        bset    #1,GA_MEMMODE           | DMNA: Word RAM to the sub
        move.b  #0x01,GA_RESET          | SBRQ=0, SRES=1: it runs

        /* ---- watch it boot ----
         *
         * No bus is needed to read a comm register, so EmuTOS keeps
         * running while this looks. Sampled on a slow counter rather
         * than a tight poll: polling $A12020 while the sub runs froze
         * the main 68000 at exactly the third read.
         *
         * The furthest trace seen is what gets recorded, not the last:
         * EmuTOS reuses $FF8020 for other things once it is up, so the
         * last value is not the high-water mark. */
        moveq   #9,d3
        bsr     mark
        clr.w   STAGE+16
        move.w  #8-1,d4                 | eight samples: long enough for
                                        | the trace, short enough that the
                                        | screen is not held back for it
5:      move.l  #0x00020000,d0
6:      subq.l  #1,d0
        bne     6b
        move.w  GA_STAT0,d1
        cmp.w   STAGE+16,d1
        bls     7f
        move.w  d1,STAGE+16             | a new high-water mark
7:      dbra    d4,5b

        move.w  0xA12022,STAGE+18
        move.l  0xA12024,STAGE+36       | low PRG-RAM write, read back

        /* ---- report ---- */
        moveq   #0x0A,d3
        bsr     mark
        lea     RPT_AT,a1
        lea     STAGE+16,a0
        moveq   #(40-1),d0
8:      move.b  (a0)+,d1
        move.b  d1,(a1)
        cmp.b   (a1),d1
        bne     fail_sram
        addq.l  #2,a1
        dbra    d0,8b

        /* Record how far EmuTOS got, but hand over regardless.
         *
         * This used to require the trace to equal E005 exactly and halt
         * otherwise, which was fine while E005 was the highest code
         * that existed. The moment bios_init got its own traces the
         * high-water mark ran past it and the loader stopped before
         * ever reaching IOFW -- so adding a diagnostic disabled the
         * thing that would have displayed it.
         *
         * At-least, not equal-to. And a shortfall is a mark in the log,
         * not a reason to withhold the screen: a booted EmuTOS that
         * stalls has more to say through IOFW than a halted cart does. */
        cmpi.w  #TRACE_OK,STAGE+16
        bcc     1f
        move.b  #0xF7,d3
        bsr     mark
        bra     2f
1:      move.b  #0xFF,d3
        bsr     mark
2:

        /* ---- and now be the servant ----
         *
         * IOFW is the other half of this port: it reads EmuTOS's planar
         * framebuffer out of PRG-RAM through the window, converts it,
         * and feeds the VDP, and it injects the pad as IKBD packets
         * going the other way. On a CD boot it arrives as a file and
         * the IP jumps to it in Word RAM. Here it is simply further
         * down the same ROM.
         *
         * Its crt0 relocates itself to work RAM from wherever it is
         * entered, so this is a jump and nothing else. No colour first:
         * green means "EmuTOS booted and nothing is drawing", and from
         * here on something is. */
        move.b  #0x0B,d3                | last thing the cart does
        bsr     mark
        move.l  0xFF09A0,STAGE+48       | did the flag survive to here?
        jmp     iofw_img

fail_nobus:
        moveq   #0x70,d3
        bsr     mark
        bra     _halt
fail_wram:
        move.b  #0xF5,d3                | the copy did not land
        bsr     mark
        bra     _halt
fail_sram:
        move.b  #0xF2,d3
        bsr     mark
        bra     _halt
_oops:
        moveq   #0x0E,d3
        bsr     mark
_halt:
        stop    #0x2700
        bra     _halt

reset_ga:
        move.w  #0xFF00,0xA12002
        move.b  #0x03,0xA12001
        move.b  #0x02,0xA12001
        move.b  #0x00,0xA12001
        moveq   #0x7F,d0
1:      dbra    d0,1b
        rts

wait_bus:
        move.l  #0x00080000,d0
1:      move.b  d2,GA_RESET
        btst    #1,GA_RESET
        bne     2f
        subq.l  #1,d0
        bne     1b
        moveq   #1,d0
        rts
2:      moveq   #0,d0
        rts

/* Find the compressed sub CDBIOS and the routine that unpacks it, run
 * that routine, plant a stub that calls DRV_INIT, and let it.
 *
 * The search is for the three-instruction shape the BIOS uses to do it
 * itself, `lea <blob>,a0 / lea <dest>,a1 / bsr.w <unpacker>`, with the
 * destination inside the main CPU's PRG window. No version table: the
 * shape is the same in every image here and the addresses are read out
 * of whichever one is fitted.
 *
 * There are three of them, not one, and this used to unpack only the
 * first. ROM 0x66E, 0x688 and 0x698 in the US and EU images:
 *
 *      0x15800 -> 0x20000    sub 0x00000, 22528 bytes
 *      0x13400 -> 0x26000    sub 0x06000, 14312 bytes
 *      0x19800 -> 0x38000    sub 0x18000, 32768 bytes
 *
 * The first ends at sub 0x57FF, so on its own it does not even reach
 * the CDBIOS entry vector at 0x5F22 that the stub below calls -- that
 * area is built by the firmware's own startup, which cannot run
 * correctly with two thirds of itself missing. Whatever else was wrong
 * with the wake, it was being asked to run a third of a firmware.
 *
 * All three destinations are in PRG bank 0 and inside one 128 KB
 * window, so they need no bank switching between them. The stub is
 * copied over 0x6000 afterwards, which is what loading a sub program
 * means: on a disc boot the second blob is discarded exactly the same
 * way, by the SP being loaded on top of it. */
/* Pier Solar's detect routine (ROM 0xF95C) also tests 'BR' at 0x400180
 * and the two BIOS-version digits after it, but this loader has already
 * checked 'SEGA' at 0x400100 and refused to boot without it, so there
 * is nothing left here for a second look to find. */
wake_drive:
        moveq   #0x12,d3
        bsr     mark
        move.w  #0x0000,0xA12002        | write protect off, bank 0

        /* Zero the firmware's half of PRG-RAM before unpacking into it.
         *
         * Pier Solar clears all four banks -- the whole 512 K -- before
         * it loads anything (ROM 0xFAC4 through 0xFBDE, four 32 K
         * clears per bank across all four banks). That is a lot of
         * bus time to spend on nothing, so it is not decoration: the
         * firmware has a work area in low PRG-RAM and cold silicon
         * powers up holding whatever it feels like.
         *
         * Only bank 0 is cleared here. That is the 128 K the firmware
         * unpacks into and runs from, and every other bank is about to
         * be overwritten by EmuTOS anyway. */
        movea.l #PRG_WIN,a0
        move.w  #0x8000-1,d0            | 32768 longs = 128 K
1:      clr.l   (a0)+
        dbra    d0,1b

        /* Every `lea <blob>,a0 / lea <dest>,a1 / bsr.w <unpacker>` in
         * the boot ROM whose destination lands in the PRG window, in
         * ROM order, which is the order the firmware does them in. */
        lea     BIOS_ROM,a5
        move.l  #0x1FFF0,d5
        moveq   #0,d6                   | blobs unpacked
1:      cmpi.w  #0x41F9,(a5)            | lea <abs>,a0
        bne     2f
        cmpi.w  #0x43F9,6(a5)           | lea <abs>,a1
        bne     2f
        cmpi.w  #0x6100,12(a5)          | bsr.w <unpacker>
        bne     2f
        move.l  8(a5),d7                | destination, main's Mode 2 view
        sub.l   #0x20000,d7             | ...as a window offset
        cmpi.l  #0x20000,d7
        bcc     2f                      | not in the PRG window at all
        move.l  2(a5),d1                | the blob, as a ROM offset
        add.l   #BIOS_ROM,d1            | ...and where it is in Mode 1
        move.w  14(a5),d0               | the bsr.w displacement
        ext.l   d0
        lea     14(a5),a2
        add.l   d0,a2                   | a2 = the unpacker, in place
        movea.l d1,a0
        lea     PRG_WIN,a1
        add.l   d7,a1                   | a1 = where this blob belongs
        movem.l d5-d7/a5,-(sp)
        jsr     (a2)                    | the console unpacks its own
        movem.l (sp)+,d5-d7/a5
        addq.l  #1,d6
        lea     16(a5),a5               | past this triple: 6 + 6 + 4
        subq.l  #8,d5
        subq.l  #8,d5
        bra     3f
2:      addq.l  #2,a5                   | instructions are word aligned
        subq.l  #2,d5
3:      tst.l   d5
        bgt     1b

        move.l  d6,STAGE+56             | how many blobs were found
        tst.l   d6
        bne     4f
        move.b  #0xF3,d3                | no BIOS shape here
        move.b  #'B',WAKE_SAY
        bsr     mark
        rts
4:

        /* Did anything arrive? The firmware begins with its own
         * exception vectors, and vector 1 -- the initial PC -- is the
         * one longword that must point into its own code. Same test
         * bios_parked() uses on the CD side. */
        move.l  PRG_WIN+4,d0
        move.l  d0,STAGE+64
        cmpi.l  #0x200,d0
        bcs     6f
        cmpi.l  #0x6000,d0
        bcc     6f
        bra     7f
6:      move.b  #0xF6,d3                | unpacked, but not into firmware
        move.b  #'U',WAKE_SAY
        bsr     mark
        rts
7:
        /* The stub goes where a sub program always goes, and looks like
         * one: the BIOS reads the module header at 0x6000 and calls the
         * init entry out of its jump table. */
        moveq   #0x13,d3
        bsr     mark
        lea     subwake,a0
        lea     PRG_WIN+SUB_WAKE,a1
        move.w  #(subwake_end-subwake)-1,d0
1:      move.b  (a0)+,(a1)+
        dbra    d0,1b

        move.b  #0x2A,0xA12002          | the BIOS's own write protect

        /* Clear the whole main->sub comm block before the firmware can
         * read it.
         *
         * This is the entire body of the routine Pier Solar calls first
         * when it starts its sub program (ROM 0xF710): clr.l on
         * 0xA12010, 0xA12014, 0xA12018 and 0xA1201C, then clr.b on
         * 0xA1200E. That is COMM0 through COMM7 and the main->sub flag
         * byte -- every register the main CPU owns and the sub can see.
         * A gate array reset does not touch any of them, so on a cold
         * boot the firmware reads whatever the ASIC powered up holding.
         * We were handing it eight words of noise. */
        clr.l   0xA12010
        clr.l   0xA12014
        clr.l   0xA12018
        clr.l   0xA1201C
        clr.b   0xA1200E

        /* ...and put the boot flags straight back, because clearing
         * 0xA12010 as a longword clears 0xA12012 with it, and 0xA12012
         * is where the buttons held at power-on are waiting for EmuTOS
         * to read them. The first cut of this wiped them and the
         * machine booted to a desktop with Down held: EmuTOS spun out
         * its wait for the 0x5A00 marker, fell through with whatever
         * the register happened to hold, and read every boot flag off
         * the live pad instead. That is the copy BOOTFLAGS was kept
         * for -- the comment on it says "kept for the wake", written
         * before there was a wake to keep it for. */
        move.w  BOOTFLAGS,0xA12012

        /* Let it go: reset first, then the bus, each one confirmed.
         *
         * The single write used further up -- SRES and SBRQ in one byte
         * -- works when the sub program is EmuTOS, because EmuTOS
         * starts by waiting. Sega's firmware starts by running, so this
         * does it the way the shipping reference does (Pier Solar, ROM
         * 0xFC24): ori.b #1 to lift reset and poll until the gate array
         * reads it back, then andi.b #0xFD to drop the bus and poll
         * until that is really gone.
         *
         * Two read-modify-writes, which is safe only because they are
         * separate. One of them doing both at once is the documented
         * hang noted at the Word RAM handover above: the read returns
         * SBRQ as *granted*, the write puts that 1 straight back, and
         * the sub never gets its bus. Split in two, each RMW preserves
         * a bit it wants to keep. */
        move.l  #0x10000,d0
1:      bset    #0,GA_RESET             | out of reset, bus still held
        btst    #0,GA_RESET
        bne     2f
        subq.l  #1,d0
        bne     1b
        move.b  #0xF9,d3                | reset never came off
        move.b  #'H',WAKE_SAY
        bsr     mark
        rts
2:      moveq   #0x17,d3
        bsr     mark
        move.l  #0x10000,d0
1:      bclr    #1,GA_RESET             | and now the bus
        btst    #1,GA_RESET
        beq     2f
        subq.l  #1,d0
        bne     1b
        move.b  #0xFA,d3                | still holding the bus
        move.b  #'H',WAKE_SAY
        bsr     mark
        rts
2:      moveq   #0x18,d3
        bsr     mark

        /* DRV_INIT spins the disc up, reads the table of contents and
         * then pauses two seconds, so this waits in seconds rather than
         * milliseconds. Bounded, because the same call is documented to
         * wait forever for a tray that is open.
         *
         * And sampled on a counter rather than polled. Reading a comm
         * register in a tight loop while the sub runs freezes the main
         * 68000 at about the third read -- already written down, and
         * still the first thing this got wrong. */
        /* Two stages, because they have very different deadlines and
         * lumping them together is what made every boot cost twenty
         * seconds for nothing.
         *
         * One: has the firmware called the module at all? It does that
         * within a moment of starting or it never does, so this is
         * short. The first hardware run of the split marker said F4 --
         * never called -- so twenty seconds were being spent waiting
         * for something that was never coming, on every boot.
         *
         * IFL2 is pulsed while waiting. Pier Solar's handshake (ROM
         * 0xF9B8) sets bit 0 of $A12000 over and over until its sub
         * program answers, and leaving that out was a guess: that the
         * firmware would call our module unprompted because ours
         * answers from init rather than from INT2. The guess is now
         * the leading suspect for why it never calls us. */
        move.w  #10-1,d4                | ~1.7 s
6:      bset    #0,0xA12000             | IFL2: poke the sub
        move.l  #0x00020000,d0
7:      subq.l  #1,d0
        bne     7b
        cmpi.w  #WAKE_DONE,GA_STAT7     | terminal already?
        bcc     5f
        cmpi.w  #WAKE_CALL,GA_STAT7     | ...or at least entered?
        bcc     9f
        dbra    d4,6b
        move.b  #0xF4,d3                | the firmware never called the stub
        move.b  #'T',WAKE_SAY
        bsr     mark
        rts

9:      moveq   #0x19,d3                | the module is running
        bsr     mark
        /* Keep whatever the drive last said about itself, so a failure
         * can name the state it failed in rather than just failing. */
        move.w  0xA1202C,d0
        lsr.w   #8,d0
        lsr.w   #4,d0
        andi.w  #0x0F,d0
        addi.b  #'0',d0
        cmpi.b  #'9',d0
        bls     91f
        addi.b  #7,d0                   | A-F
91:     move.b  d0,WAKE_SAY+1
        /* Two: DRV_INIT itself, which spins the disc up, reads the
         * table of contents and then pauses two seconds -- so this one
         * waits in seconds. Only reached once the module has answered,
         * so an empty tray or a firmware that ignores us no longer
         * pays for it.
         *
         * Sampled on a counter rather than polled. Reading a comm
         * register in a tight loop while the sub runs freezes the main
         * 68000 at about the third read -- already written down, and
         * still the first thing this got wrong. */
        move.w  #120-1,d4               | ~20 s at a sample every 1/6 s
4:      move.l  #0x00020000,d0
8:      subq.l  #1,d0
        bne     8b
        cmpi.w  #WAKE_DONE,GA_STAT7
        bcc     5f
        dbra    d4,4b
        move.b  #0xF7,d3                | the stub ran; DRV_INIT is still away
        move.b  #'S',WAKE_SAY
        bsr     mark
        rts

        /* The drive is awake. What did the firmware make of the disc? */
5:      move.w  GA_STAT7,d0
        cmpi.w  #WAKE_RD1,d0
        beq     51f
        cmpi.w  #WAKE_RD2,d0
        beq     52f
        cmpi.w  #WAKE_NORD,d0
        beq     53f
        cmpi.w  #WAKE_RDT,d0
        beq     54f
        moveq   #0x14,d3                | awake, the read never finished
        move.b  #'A',WAKE_SAY
        bsr     mark
        rts
51:     moveq   #0x1A,d3                | the firmware read LBA 1600
        move.b  #'R',WAKE_SAY
        bsr     mark
        rts
52:     moveq   #0x1B,d3                | ...at 1600+150: our base is wrong
        move.b  #'Q',WAKE_SAY
        bsr     mark
        rts
53:     moveq   #0x1C,d3                | the firmware cannot read it either
        move.b  #'X',WAKE_SAY
        bsr     mark
        rts
54:     moveq   #0x1D,d3                | 1600 works on a retry: it was time
        move.b  #'Z',WAKE_SAY
        bsr     mark
        rts

/* The sub program the firmware starts: a module header, a jump table,
 * and one BIOS call. Everything in it is PC-relative or an address in
 * the sub's own map, so it does not care where in the cart it was
 * assembled. Laid out exactly as megadev's sp_header.s describes. */
        .align  2
subwake:
        .ascii  "MAIN       "
        .byte   0                       | flag
        .word   0x0100                  | version
        .word   0                       | type
        .long   0                       | next module
        .long   0x100                   | module size
        .long   swjmp-subwake           | offset to the jump table
        .long   0                       | work RAM wanted
swjmp:
        .word   swinit-swjmp
        .word   swmain-swjmp
        .word   swint2-swjmp
        .word   swuser-swjmp
        .word   0
swinit:
        /* Two words, not one. F4 was covering two failures that want
         * opposite fixes: the firmware never calling this module at all
         * -- wrong header, wrong jump table, firmware not really
         * running -- and DRV_INIT being called and never returning,
         * which is what an empty tray does and is nothing to do with
         * us. One marker cannot tell those apart and we have been
         * reading one for both. */
        move.w  #WAKE_CALL,0x00FF802E

        /* Bring the drive up the way a working Mode 1 program does.
         *
         * Doom CD32X Fusion's sub program is called by the BIOS every
         * frame, and the first thing it does every time (ROM 0xC7E) is
         * CDBSTAT, test the high byte of the returned status word for
         * $40 or $10, and on anything else re-run DRV_INIT, FDRSET
         * $8400 and CDCSTOP before it will accept any command at all.
         * It never assumes the drive stays ready and it never assumes
         * one DRV_INIT took.
         *
         * We called DRV_INIT once. The hardware logs have the drive
         * sitting at state 0 and 7 -- neither of the two this program
         * accepts -- which is exactly the case it re-initialises out
         * of. Eight rounds, and the last status seen is reported
         * whether it worked or not. */
        moveq   #8-1,d6
1:      move.w  #BIOS_CDBSTAT,d0
        jsr     SUB_CDBIOS
        move.w  (a0),d0
        move.w  d0,0x00FF802C           | the loader picks this up
        tst.w   d0
        bmi     2f                      | bit 15: not ready at all
        lsr.w   #8,d0
        cmpi.b  #0x40,d0
        beq     3f
        cmpi.b  #0x10,d0
        beq     3f
        /* 2 as well as Doom's 4 and 1.
         *
         * Doom takes only $40 and $10, but its loop runs every frame
         * and can afford to reject a state the drive is passing
         * through. Ours runs eight times and then gives up, and this
         * project's own note on the drive nibble says "on every CD boot
         * it reads 1, 2 or 4 -- the firmware has already moved the
         * drive". Hardware now reports 2 here, where before the wake it
         * sat at 0 and 7, so rejecting it means re-initialising a drive
         * that is already alive eight times and reporting the state as
         * if it were a failure. */
        cmpi.b  #0x20,d0
        beq     3f
2:      lea     swparm(pc),a0
        move.w  #BIOS_DRV_INIT,d0
        jsr     SUB_CDBIOS
        move.w  #BIOS_FDRSET,d0
        move.w  #0x8400,d1
        jsr     SUB_CDBIOS
        move.w  #BIOS_CDCSTOP,d0
        jsr     SUB_CDBIOS
        dbra    d6,1b
3:      move.w  #WAKE_DRV,0x00FF802E    | main reads this at 0xA1202E

        /* And now the question the whole port is stuck on: can Sega's
         * own firmware read the sector our driver cannot?
         *
         * Waking the drive works on hardware and does not fix D:. Every
         * decoder configuration the firmware knows has been tried and
         * none decodes a header. That leaves two possibilities which
         * look identical from inside our driver -- our driver is wrong,
         * or this disc in this drive cannot be read -- and the firmware
         * is running right here, in this boot, able to settle it.
         *
         * The sequence is the BIOS reading its own filesystem, US
         * 0x410C: ROMREAD once with { lba, count }, then CDCSTAT until
         * the carry clears, with a wait between and a bounded retry
         * count. The parameter block layout is from the same routine --
         * 0x4104 is `move.l d0,4(a0)` with d0 the sector count.
         *
         * Twice, because the base is the one thing not written down
         * anywhere citable: our driver subtracts a 150-frame lead-in
         * and ROMREAD may want it back. One extra attempt beats
         * reporting a false negative from an off-by-150. */
        lea     swrd1(pc),a0
        bsr     swtry
        beq     1f
        move.w  #WAKE_RD1,0x00FF802E
        rts
1:      lea     swrd2(pc),a0
        bsr     swtry
        beq     2f
        /* 1750 answered where 1600 did not -- but the attempts are
         * sequential, so "the second address worked" and "the drive
         * had finished settling by the second attempt" produce exactly
         * the same result. Ask for 1600 again. If it answers now, the
         * first failure was time and the address is a red herring; if
         * it still refuses, the offset is real. */
        lea     swrd1(pc),a0
        bsr     swtry
        beq     3f
        move.w  #WAKE_RDT,0x00FF802E    | it was settling, not the base
        rts
3:      move.w  #WAKE_RD2,0x00FF802E    | 1600 really is the wrong number
        rts
2:      move.w  #WAKE_NORD,0x00FF802E
        rts

/* a0 = { lba, count }. Z clear if a sector became ready.
 * d7 survives the BIOS calls because Sega's own loop keeps its retry
 * counter there across exactly these two. */
swtry:
        movem.l d6-d7/a0,-(sp)
        move.w  #BIOS_ROMREAD,d0
        jsr     SUB_CDBIOS
        /* 150, not the 600 Sega uses. Its 600 pairs with its own delay
         * routine and mine is a guess at the length: 600 of these
         * overran the outer wait and reported "the read never
         * finished", which is the least useful answer available. About
         * three seconds per attempt, which is many times what a seek
         * and a sector need. */
        move.w  #150,d7
3:      move.l  #0x4000,d6
4:      subq.l  #1,d6
        bne     4b
        move.w  #BIOS_CDCSTAT,d0
        jsr     SUB_CDBIOS
        dbcc    d7,3b
        movem.l (sp)+,d6-d7/a0
        bcs     5f
        moveq   #1,d0                   | Z clear: a sector is ready
        rts
5:      moveq   #0,d0                   | Z set: it never came
        rts

        .align  2
swrd1:  .long   TEST_LBA
        .long   1
swrd2:  .long   TEST_LBA+150
        .long   1
swmain: rts
swint2: rts
swuser: rts
swparm: .byte   0x01                    | first track of the TOC read
        .byte   0xFF                    | ...to the last one there is
        .align  2
subwake_end:

/* Does the byte at (a0) remember two different values? Z set if not.
 * Non-destructive: whatever was there goes back either way. Open bus
 * and ROM both read back plausibly once, so it takes two. */
sram_holds:
        move.b  (a0),d1
        move.b  #0x5A,(a0)
        cmpi.b  #0x5A,(a0)
        bne     1f
        move.b  #0xA5,(a0)
        cmpi.b  #0xA5,(a0)
        bne     1f
        move.b  d1,(a0)
        moveq   #1,d0                   | Z clear: it holds
        rts
1:      move.b  d1,(a0)
        moveq   #0,d0                   | Z set: it does not
        rts

mark:
        movem.l d0/a0,-(sp)
        lea     MARKS_AT,a0
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

/* ---- the payloads ----
 *
 * Longword-aligned because the copies above move longwords, and padded
 * to a longword for the same reason: a trailing odd byte would make the
 * count wrong rather than the copy short. */
        .align  2
emutos_img:
        .incbin "emutos-segacd.img"
        .align  2
emutos_end:

        .align  2
adisk_img:
        .incbin "ADISK.IMG"
        .align  2
adisk_end:

/* The servant. Entered in place: its crt0 takes its copy source from
 * the PC, so it relocates itself out of ROM into work RAM. */
        .align  2
iofw_img:
        .incbin "iofw.bin"
        .align  2
iofw_end:
