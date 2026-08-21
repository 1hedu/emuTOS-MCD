/* Main-CPU half of the standalone CDD test: put the sub's verdict on the
 * screen as a colour, because a colour needs no font and cannot be
 * misread off a photograph of a CRT.
 *
 *   blue    the test has not reached a verdict yet
 *   red     no status packet ever checksummed -- the link itself is down
 *   yellow  the link is fine, but the drive never named a question
 *   cyan    it named one
 *   green   it named two different ones: the round trip works
 *
 * Green here with the console still failing under EmuTOS would mean the
 * driver is right and the fault is in how EmuTOS shares this CPU with
 * the drive. Yellow here would mean the driver is still wrong.
 */

        .equ    VDP_DATA, 0xC00000
        .equ    VDP_CTRL, 0xC00004
        .equ    GA_VERDICT, 0xA12020

Entry:
        move.w  #0x8004,VDP_CTRL        | reg 0: normal operation
        move.w  #0x8144,VDP_CTRL        | reg 1: display on, V28
        move.w  #0x8F02,VDP_CTRL        | reg 15: autoincrement 2
        move.w  #0x8700,VDP_CTRL        | backdrop = colour 0

        | Tell the sub it may take the CD drive now: the BIOS has
        | finished with it, because we are the thing it loaded.
        | Release the SP first: it waits for the command byte to clear
        | before it reaches its idle loop, and the boot loader that
        | normally clears it is us now.
        move.b  #0,0xA1200E
        move.w  #0x600D,0xA12010

Loop:
        move.w  GA_VERDICT,%d0
        andi.w  #7,%d0
        add.w   %d0,%d0
        lea     Colours(%pc),%a0
        move.w  0(%a0,%d0.w),%d1

        move.l  #0xC0000000,VDP_CTRL    | CRAM entry 0
        move.w  %d1,VDP_DATA

        | Mirror the sub's counters where the test harness can read them
        | as numbers. A colour is the right output for a photograph of a
        | television; it is a poor one for debugging the test itself.
        move.w  GA_VERDICT,0xFF0F40
        move.w  GA_VERDICT+2,0xFF0F42
        move.w  GA_VERDICT+4,0xFF0F44
        move.w  GA_VERDICT+6,0xFF0F46
        bra     Loop

Colours:
        .word   0x0E00                  | 0: blue, no verdict yet
        .word   0x000E                  | 1: red, link down
        .word   0x00EE                  | 2: yellow, no answer
        .word   0x0EE0                  | 3: cyan, one question named
        .word   0x00E0                  | 4: green, round trip works
        .word   0x0E0E                  | 5: magenta, init ran, no interrupt
        .word   0x0EEE
        .word   0x0EEE
