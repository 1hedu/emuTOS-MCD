/* Initial Program (main CPU): security block + Word-RAM handoff bootstrap.
 * Ported to GNU as from ProjectCD's ip.asm (BSD-2-Clause, Luke Usher).
 * Region security block selected at assembly time:
 *   --defsym REGION_JAP=1 | REGION_USA=1 | REGION_EUR=1
 * Assemble with -I pointing at vendor/projectcd/src/bootsect (for security/).
 * Link at 0; the disc build places this at offset 0x200 of the boot area.
 */

SecurityCode:
.ifdef REGION_JAP
        .incbin "security/jap.bin"
.endif
.ifdef REGION_USA
        .incbin "security/usa.bin"
.endif
.ifdef REGION_EUR
        .incbin "security/eur.bin"
.endif

        /* Security blocks differ in size; code entry is a fixed jump past
         * the largest of them so region can vary without moving offsets. */
        bra     IP_Start

        .org    0x600

/* Bootstraps the main program: hand Word RAM to the sub CPU, ask its SP to
 * load the boot payload from ISO9660 into Word RAM, take Word RAM back,
 * jump to it. */
IP_Start:
        move.w  #0x000E,d1              | stage RED: IP entered
        bsr     SetBG

        bset    #1,0xA12003             | give Word RAM to sub CPU
ip_initwait:
        tst.b   0xA1200F                | sub CPU finished init?
        bne     ip_initwait

        move.w  #0x00EE,d1              | stage YELLOW: sub init done
        bsr     SetBG

.ifdef AUDIT
        move.b  #3,d0                   | E-A1: snapshot A (post-boot state)
        bsr     SubCommand
.endif

        move.b  #1,d0                   | command 1: load boot file
        bsr     SubCommand

        move.w  #0x0E00,d1              | stage BLUE: payload loaded
        bsr     SetBG

.ifdef AUDIT
        move.l  #1500000,d2             | ~2s: let BIOS idle interrupts run
audit_delay:
        subq.l  #1,d2
        bne     audit_delay
        move.b  #3,d0                   | E-A1: snapshot B (post CD activity)
        bsr     SubCommand
.endif

        move.b  #2,d0                   | command 2: return Word RAM to main
        bsr     SubCommand

        jmp     0x200000                | enter payload in Word RAM

/* Set backdrop color (CRAM entry 0) to d1 — boot stage indicator. */
SetBG:
        move.l  #0xC0000000,0xC00004
        move.w  d1,0xC00000
        rts

/* Send command in d0 to the sub CPU via the comm flag bytes and wait. */
SubCommand:
        tst.b   0xA1200F                | sub CPU free?
        bne     SubCommand

        move.b  #0,0xA1200E             | clear command
sc_wait_ready:
        tst.b   0xA1200F                | sub CPU ready?
        beq     sc_wait_ready

        move.b  d0,0xA1200E             | issue command
sc_wait_done:
        tst.b   0xA1200F                | sub CPU done?
        bne     sc_wait_done
        rts
