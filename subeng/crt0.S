/* Sub-engine entry: reached by the SP's jmp 0x10000, running under the
 * CDBIOS (its interrupts stay live). Own stack in ST-RAM territory well
 * clear of the code, planar buffer and BIOS area.
 */
        .global _start
_start:
        movea.l #0x50000,%sp
        lea     __bss_start,%a0
        lea     __bss_end,%a1
clr_bss:
        cmpa.l  %a0,%a1
        beq.s   bss_done
        clr.w   (%a0)+
        bra.s   clr_bss
bss_done:
        jsr     main
hang:
        bra.s   hang
