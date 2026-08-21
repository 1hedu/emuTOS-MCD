/* Minimal Sega CD BIOS call interface for the boot SP.
 * Ported from ProjectCD's _inc/cdbios.asm (BSD-2-Clause, Luke Usher) —
 * see vendor/projectcd/. Full call set lives there; this is only what
 * the boot loader uses.
 */

.equ _CDBIOS,   0x00005F22

.equ ROMREADN,  0x0020
.equ CDCSTOP,   0x0089
.equ CDCSTAT,   0x008A
.equ CDCREAD,   0x008B
.equ CDCTRN,    0x008C
.equ CDCACK,    0x008D

.macro CDBIOS fcode
        move.w  #\fcode,d0
        jsr     _CDBIOS
.endm

.macro BIOS_ROMREADN
        CDBIOS  ROMREADN
.endm
.macro BIOS_CDCSTOP
        CDBIOS  CDCSTOP
.endm
.macro BIOS_CDCSTAT
        CDBIOS  CDCSTAT
.endm
.macro BIOS_CDCREAD
        CDBIOS  CDCREAD
.endm
.macro BIOS_CDCTRN
        CDBIOS  CDCTRN
.endm
.macro BIOS_CDCACK
        CDBIOS  CDCACK
.endm
