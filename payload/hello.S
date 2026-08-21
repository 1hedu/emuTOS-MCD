| HELLO.MDP -- the smallest thing that proves the handover.
|
| Genesis-side code, staged off a disc and run with the VDP handed to
| it. It does what only Genesis-side code can do -- reprograms the VDP
| from nothing, paints a backdrop no ST program could produce, reads the
| pad directly -- holds until Start is pressed, and returns. Then the
| servant puts the desktop back.
|
| That is the whole of what has to work before a real payload is worth
| writing: the bytes arrive, the jump lands, the pad is readable, the
| return is clean, and the machine comes back. See docs/payload.md.
|
| Linked for 0xFF7000, which is Genesis work RAM and is therefore at the
| same address whichever way the console booted.

        .text
        .globl  payload_start

| ---- the header, as docs/payload.md defines it ---------------------
payload_start:
        .ascii  "MDPL"                  | +0   magic
        .word   1                       | +4   version
        .word   0                       | +6   flags
        .long   entry - payload_start   | +8   entry offset
        .long   image_end - payload_start   | +12  image length
        .long   256                     | +16  working space wanted
        .ascii  "HELLO   "              | +20  name

| ---- registers ------------------------------------------------------
        .equ    VDP_DATA,  0xC00000
        .equ    VDP_CTRL,  0xC00004
        .equ    IO_CTRL1,  0xA10009
        .equ    IO_DATA1,  0xA10003

| ---- entry ----------------------------------------------------------
| a0 = the ST screen (32000 bytes, planar, read-only)
| a1 = working space
| d0 = bit 0 set on a Mode 1 boot
entry:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        movea.l #VDP_CTRL,%a3
        movea.l #VDP_DATA,%a4

| The VDP from nothing. A payload owes the servant no state on the way
| out -- the register file is write-only, so nothing could have been
| saved anyway and the servant rewrites all of it on return. So this can
| simply program what it wants.
        lea     vdp_regs(%pc),%a2
        moveq   #0,%d1
        moveq   #23,%d2                 | 24 registers
1:      move.w  %d1,%d3
        lsl.w   #8,%d3
        ori.w   #0x8000,%d3
        move.b  (%a2)+,%d3
        move.w  %d3,(%a3)
        addq.w  #1,%d1
        dbra    %d2,1b

| One colour into CRAM entry 0, which is the backdrop register 7 points
| at: a flat field in a colour the desktop's palette does not hold, so
| "the payload ran" is unmistakable in a photograph or a frame dump.
        move.l  #0xC0000000,(%a3)       | CRAM write, address 0
        move.w  #0x0E0E,(%a4)           | bright cyan-white

| Wait for Start. Read straight off the pad: the servant's own input
| code is not running, and this is the point -- Genesis-side code talks
| to the hardware itself.
        move.b  #0x40,IO_CTRL1
wait:   move.b  #0x40,IO_DATA1
        nop
        nop
        move.b  #0x00,IO_DATA1
        nop
        nop
        move.b  IO_DATA1,%d4            | low half: Start in bit 5
        move.b  #0x40,IO_DATA1
        not.b   %d4
        btst    #5,%d4
        beq.s   wait

| Let Start go, so the press does not carry back into the desktop.
release:
        move.b  #0x00,IO_DATA1
        nop
        nop
        move.b  IO_DATA1,%d4
        move.b  #0x40,IO_DATA1
        not.b   %d4
        btst    #5,%d4
        bne.s   release

        movem.l (%sp)+,%d0-%d7/%a0-%a6
        rts

| Display on, 40 cells wide, everything else quiet. The servant will
| overwrite all of this on the way back.
vdp_regs:
        .byte   0x04, 0x44, 0x30, 0x3C, 0x07, 0x6C, 0x00, 0x00
        .byte   0x00, 0x00, 0x00, 0x00, 0x81, 0x3F, 0x00, 0x02
        .byte   0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        .align  2
image_end:
