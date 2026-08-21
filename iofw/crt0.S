/* IOFW v2 entry. Relocate the whole image to work RAM at its linked
 * address 0xFF1000 and continue there. Interrupts stay masked; the IOFW
 * polls everything.
 *
 * Two callers now, and they load it in different places. On a CD boot
 * the IP jumps here at 0x200000 in Word RAM, which then has to be
 * returned to the sub CPU for EmuTOS. On a Mode 1 boot the cartridge
 * jumps here in ROM, wherever the image happens to sit in it.
 *
 * So the source is taken from the PC rather than written down. It used
 * to be `lea 0x200000,%a0`, which is the CD-boot address and nothing
 * else; a Mode 1 boot with that in place would relocate a quarter of a
 * megabyte of somebody else's memory over itself.
 */
        .global _start
_start:
        move.w  #0x2700,%sr
        move.l  #0x494F4657,0xFF09A4    | 'IOFW': entered, before anything
        lea     _start(%pc),%a0         | copy source: wherever we are
        lea     0xFF1000,%a1            | copy dest: where we're linked
        move.w  #6143,%d0               | 24 KB in longs
reloc:
        move.l  (%a0)+,(%a1)+
        dbra    %d0,reloc
        movea.l #0xFFFC00,%sp           | stack above the cache, below the
                                        |   BIOS jump table at 0xFFFD00
        lea     relocated,%a0           | absolute (linked) address
        jmp     (%a0)
relocated:
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
