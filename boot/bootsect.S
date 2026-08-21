/* Sega CD boot area (16 sectors, 32 KB): disc header + IP + SP.
 * Ported to GNU as from ProjectCD's bootsect.asm (BSD-2-Clause, Luke Usher).
 * Region: --defsym REGION_JAP=1 | REGION_USA=1 | REGION_EUR=1 (must match
 * the security block assembled into ip.bin).
 * Assemble with -I pointing at the directory containing ip.bin / sp.bin;
 * link at 0, objcopy to binary, must come out exactly 0x8000 bytes.
 */

/* ---- System header (sector 0) ---- */
DiscType:       .ascii  "SEGADISCSYSTEM  "
VolumeName:     .ascii  "EMUTOSMD   "
                .byte   0
VolumeSystem:   .word   0x100,1
SystemName:     .ascii  "EMUTOSMD   "
                .byte   0
SystemVersion:  .word   0,0
IP_Addr:        .long   0x800           | fixed by BIOS contract
IP_Size:        .long   0x800
IP_Entry:       .long   0
IP_WorkRAM:     .long   0
SP_Addr:        .long   0x1000          | fixed by BIOS contract
SP_Size:        .long   0x7000
SP_Entry:       .long   0
SP_WorkRAM:     .long   0

/* ---- Game header (standard Mega Drive layout at 0x100) ---- */
        .org    0x100
HardwareType:   .ascii  "SEGA MEGA DRIVE "
        .org    0x110
Copyright:      .ascii  "(C)EMUTOS-MD    "
        .org    0x120
NativeName:     .ascii  "EMUTOS FOR MEGA CD"
        .org    0x150
OverseasName:   .ascii  "EMUTOS FOR MEGA CD"
        .org    0x180
DiscID:         .ascii  "GM 00-0000-00   "
        .org    0x190
IO:             .ascii  "J               "

/* ---- Region code at 0x1F0 ---- */
        .org    0x1F0
Region:
.ifdef REGION_JAP
        .ascii  "J               "
.endif
.ifdef REGION_USA
        .ascii  "U               "
.endif
.ifdef REGION_EUR
        .ascii  "E               "
.endif

/* ---- IP (security block + bootstrap), then SP ---- */
        .org    0x200
        .incbin "ip.bin"

        .org    0x1000
        .incbin "sp.bin"

/* ---- Pad the boot area to exactly 16 sectors ---- */
        .org    0x7FFC
        .long   0
