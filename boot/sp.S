/* Sub CPU boot program (SP): tiny command server + ISO9660 boot-file loader.
 * Ported to GNU as from ProjectCD's sp.asm (BSD-2-Clause, Luke Usher).
 * Loaded by the Sega CD BIOS at 0x6000 in PRG-RAM — link with -Ttext 0x6000.
 *
 * Commands (main writes 0xA1200E, reads 0xA1200F; sub sees 0xFF800E/F):
 *   1 = load M_INIT.PRG from the ISO into Word RAM (2M, sub-owned)
 *   2 = hand Word RAM to the main CPU
 */

        .include "cdbios.inc"

/* ---- Sub module header (BIOS contract) ---- */
ModuleName:     .ascii  "MAIN-SUBCPU"
                .byte   0
ModuleVersion:  .word   0,0
NextModule:     .long   0
ModuleSize:     .long   0
ModuleStartAddr:.long   0x20
ModuleWorkRAM:  .long   0
JumpTable:      .word   SP_Init-JumpTable
                .word   SP_Main-JumpTable
                .word   SP_IRQ-JumpTable
                .word   0

/* Called once by the BIOS after load. */
SP_Init:
        andi.b  #0xFA,0xFF8003          | Word RAM to 2M mode
        bsr     Init9660
        clr.b   0xFF800F                | status: init done
        rts                             | BIOS will call SP_Main next

/* Command loop. No input validation — boot-time trust, as upstream. */
SP_Main:
        tst.b   0xFF800E                | wait for command register clear
        bne     SP_Main
        move.b  #1,0xFF800F             | status: ready
spm_wait:
.ifdef CDDTEST
        bsr     CddPoll_stub            | the interrupt does the work
.endif
        tst.b   0xFF800E                | wait for a command
        beq     spm_wait

        moveq   #0,d0
        move.b  0xFF800E,d0
        add.w   d0,d0
        add.w   d0,d0                   | 4 bytes per jump-table slot
        jsr     OpTable(pc,d0.w)
        move.b  #0,0xFF800F             | status: done
        bra     SP_Main

OpTable:
        bra.w   Op_Null
        bra.w   Op_LoadBootFile
        bra.w   Op_GetWordRAM
        bra.w   Op_Snapshot
        bra.w   Op_RunSubProg
        bra.w   Op_BootEmutos
        bra.w   CddTraceProbe

Op_Null:
        rts

/* Op 3: copy PRG-RAM 0x0000-0x5FFF (BIOS-reserved region) into Word RAM.
 * First call writes 0xA0000, each call advances 0x6000 — the E-A1 audit
 * uses two calls to bracket CD activity and diff the BIOS's live bytes. */
Op_Snapshot:
        movem.l d0/a0-a1,-(sp)
        suba.l  a0,a0                   | source: PRG-RAM 0
        movea.l snap_dst(pc),a1
        move.w  #0x17FF,d0              | 0x1800 longs = 0x6000 bytes
snap_loop:
        move.l  (a0)+,(a1)+
        dbra    d0,snap_loop
        lea     snap_dst(pc),a0
        move.l  a1,(a0)                 | next snapshot lands after this one
        movem.l (sp)+,d0/a0-a1
        rts
snap_dst:
        .long   0xA0000

Op_LoadBootFile:
        lea     bootfile(pc),a0
        bsr     FindFile                | -> d0 start sector, d1 sector count
        move.l  #0x80000,a0             | destination: Word RAM (sub view)
        bsr     ReadCD
        rts
bootfile:
        .asciz  "M_INIT.PRG"
        .balign 2

Op_GetWordRAM:
        bset    #0,0xFF8003             | Word RAM to main CPU
        rts

SP_IRQ:
        rts

/* Op 4: load SUBPROG.BIN from the ISO to PRG-RAM 0x10000 and enter it.
 * Never returns — the loaded program owns the sub CPU's main thread from
 * here (BIOS interrupt handling stays live underneath). Status is cleared
 * before the jump so the main CPU's command handshake completes. */
Op_RunSubProg:
        lea     subprog(pc),a0
        bsr     FindFile
        move.l  #0x10000,a0
        bsr     ReadCD
        move.b  #0,0xFF800F             | complete the handshake first
        jmp     0x10000
subprog:
        .asciz  "SUBPROG.BIN"
        .balign 2

/* Op 5: the E2 handoff. Load EMUTOS.IMG from the ISO into Word RAM at
 * 0x080000 (the main CPU must have returned Word RAM to us first), then
 * evict the CDBIOS per audit E-A1: interrupts hard-masked, jump into the
 * image's TOS reset header. EmuTOS owns the whole sub CPU from here —
 * vectors, sysvars, everything. There is no way back but reset. */
Op_BootEmutos:
        lea     adiskimg(pc),a0         | A: ramdisk -> PRG bank 3
        bsr     FindFile
        move.l  #0x60000,a0
        bsr     ReadCD
        /* Which EmuTOS?
         *
         * The driver that opened a folder on C: and showed a file in it
         * is somewhere behind forty commits of fixes, and finding it by
         * burning a disc per commit is a stack of CD-Rs nobody has. But
         * the only thing that differs between those builds is this one
         * file, and the disc has twelve megabytes of padding, so the
         * disc carries four of them and the pad chooses at boot.
         *
         * Nothing held is the current build. Left, Up and Right pick the
         * three earlier ones, oldest to newest. The old builds read only
         * A and B out of this word and ignore the rest, so a direction
         * held all the way through boot means nothing to them -- which
         * is exactly what makes it safe to steal for this. */
        lea     emutosimg(pc),a0
        move.w  0xFF8012,d0
        move.w  d0,d1
        andi.w  #0xFF00,d1
        cmpi.w  #0x5A00,d1              | has the servant answered yet?
        bne     ebt_named
        btst    #3,d0                   | Left
        beq     ebt_no3
        lea     emutos2(pc),a0
ebt_no3:
        btst    #4,d0                   | Up
        beq     ebt_no4
        lea     emutos3(pc),a0
ebt_no4:
        btst    #5,d0                   | Right
        beq     ebt_named
        lea     emutos4(pc),a0
ebt_named:
        bsr     FindFile
        move.l  #0x80000,a0
        bsr     ReadCD
        | fold a checksum of the image head into the status words so the
        | main CPU can verify the CD load delivered EmuTOS intact
        lea     0x80000,a0
        moveq   #0,d0
        move.w  #63,d1
cksum:
        add.l   (a0)+,d0
        dbra    d1,cksum
        swap    d0
        add.w   d0,d0                   | fold high into low (cheap hash)
        swap    d0
        move.w  d0,0xFF8022
        move.w  0x80006,0xFF8024        | os_version word as a second check

        /* Photograph the CDBIOS on the way past.
         *
         * Everything from 0 to 0x5FFF is the BIOS -- its vectors, its
         * code, its variables -- and in a moment EmuTOS will own that
         * space because the Atari low-memory map wants the same bytes.
         * That was always the trade. But the drive's own driver lives
         * in there, the only one on this planet that has never failed
         * to read this disc, and once it is gone it cannot be fetched
         * back: fetching needs a working read.
         *
         * So it goes to Word RAM at 0xBA000, above the end of the
         * EmuTOS image and outside the ST memory map entirely, where
         * EmuTOS will never look. From there it can be exchanged back
         * into low memory when a sector is wanted and exchanged out
         * again afterwards -- Sega's driver doing Sega's reads on
         * Sega's hardware, with nothing copied anywhere it should not
         * be, because it never leaves the machine it came with. */
        movea.l #0,a0
        movea.l #0xBA000,a1
        move.w  #0x17FF,d0              | 0x1800 longs = 0x6000 bytes
biossave:
        move.l  (a0)+,(a1)+
        dbra    d0,biossave

        move.b  #0,0xFF800F             | complete the handshake
        move.w  #0x2700,sr              | CDBIOS eviction point
        jmp     0x80000                 | TOS header reset branch
emutosimg:
        .asciz  "EMUTOS.IMG"
        .balign 2
emutos2:
        .asciz  "EMUTOS2.IMG"
        .balign 2
emutos3:
        .asciz  "EMUTOS3.IMG"
        .balign 2
emutos4:
        .asciz  "EMUTOS4.IMG"
        .balign 2
adiskimg:
        .asciz  "ADISK.IMG"
        .balign 2

/* ReadCD: d0 = start sector, d1 = sector count, a0 = destination. */
ReadCD:
        movem.l d0-d7/a0-a6,-(sp)
        lea     BiosPacket(pc),a5
        move.l  d0,(a5)                 | start sector
        move.l  d1,4(a5)                | count
        move.l  a0,8(a5)                | destination
        movea.l a5,a0
        BIOS_CDCSTOP
        BIOS_ROMREADN
rcd_stat:
        /* Watch the BIOS talk to the drive while we wait for it. This
         * loop is where nearly all of a read is spent, so sampling here
         * sees the whole conversation; see cddtrace.S. */
        bsr     CddTraceLog
        BIOS_CDCSTAT
        bcs     rcd_stat                | data not ready yet
rcd_read:
        BIOS_CDCREAD
        bcc     rcd_read                | sector not decoded yet
        movea.l 8(a5),a0                | destination
        lea     12(a5),a1               | header scratch
        BIOS_CDCTRN
        bcc     rcd_read                | transfer incomplete
        BIOS_CDCACK
        addq.l  #1,(a5)                 | next sector
        addi.l  #0x0800,8(a5)           | advance destination
        subq.l  #1,4(a5)
        bne     rcd_stat
        movem.l (sp)+,d0-d7/a0-a6
        rts

BiosPacket:
        .long   0,0,0,0,0
Header:
        .space  128

/* Load the PVD (sector 16) then the root directory into SectorBuffer. */
Init9660:
        movem.l d0-d7/a0-a6,-(sp)
        move.l  #0x10,d0                | PVD sector
        move.l  #2,d1
        lea     SectorBuffer,a0
        bsr     ReadCD

        lea     SectorBuffer,a0
        lea     156(a0),a1              | root directory record in the PVD
        move.b  6(a1),d0                | big-endian half of the LBA field
        lsl.l   #8,d0
        move.b  7(a1),d0
        lsl.l   #8,d0
        move.b  8(a1),d0
        lsl.l   #8,d0
        move.b  9(a1),d0
        move.l  #0x20,d1                | read 32 sectors of directory
        bsr     ReadCD
        movem.l (sp)+,d0-d7/a0-a6
        rts

/* FindFile: a0 = filename (NUL-terminated).
 * Returns d0 = start sector, d1 = size in sectors.
 * Upstream's naive scan: substring search over the directory buffer. */
FindFile:
        movem.l a1-a2/a6,-(sp)
        lea     SectorBuffer,a1
ff_restart:
        movea.l a0,a6
        move.b  (a6)+,d0
ff_first:
        movea.l a1,a2
        cmp.b   (a1)+,d0
        bne.b   ff_first
ff_chars:
        move.b  (a6)+,d0
        beq     ff_found
        cmp.b   (a1)+,d0
        bne.b   ff_restart
        bra.b   ff_chars
ff_found:
        sub.l   #33,a2                  | back to directory entry start
        move.b  6(a2),d0                | LBA, big-endian bytes
        lsl.l   #8,d0
        move.b  7(a2),d0
        lsl.l   #8,d0
        move.b  8(a2),d0
        lsl.l   #8,d0
        move.b  9(a2),d0
        move.b  14(a2),d1               | size, big-endian bytes
        lsl.l   #8,d1
        move.b  15(a2),d1
        lsl.l   #8,d1
        move.b  16(a2),d1
        lsl.l   #8,d1
        move.b  17(a2),d1
        addi.l  #2047,d1                | round up so a partial sector counts
        lsr.l   #8,d1                   | bytes -> sectors (/2048)
        lsr.l   #3,d1
        movem.l (sp)+,a1-a2/a6
        rts

.ifdef CDDTEST
        /* Before SectorBuffer, emphatically: everything past that label
         * is overwritten with disc sectors as the BIOS loads files, so
         * the first version of this had its whole test scribbled over
         * during boot and then wondered why nothing ran. */
        .include "cddtest.S"
        .even
.endif

        /* Same placement rule, and not optional: the CDD trace is read
         * by the main CPU out of PRG-RAM after the boot files have
         * loaded, so it has to sit somewhere the loading did not
         * scribble on. */
        .include "cddtrace.S"
        .even

/* Everything past here is clobbered at runtime as the read buffer. */
SectorBuffer:
        .long   0
