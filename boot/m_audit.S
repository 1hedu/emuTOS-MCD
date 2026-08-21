/* E-A1 audit payload: runs on the main CPU from Word RAM at 0x200000.
 * The SP took two 0x6000-byte snapshots of sub PRG-RAM 0x0000-0x5FFF
 * (the BIOS-reserved region) into Word RAM at sub 0xA0000/0xA6000 —
 * main-side 0x220000/0x226000. Copy both into work RAM, where the
 * emulator harness's --dump-wram makes them a file on the host:
 *   snapshot A -> WRAM 0xFF1000  (right after boot handshake)
 *   snapshot B -> WRAM 0xFF7000  (after CD reads + ~2s of BIOS idle)
 * Marker 'AUD1' at 0xFF0FF0; backdrop cyan when done.
 */

        .equ    VDP_DATA, 0xC00000
        .equ    VDP_CTRL, 0xC00004

Entry:
        move.w  #0x8004,VDP_CTRL
        move.w  #0x8144,VDP_CTRL
        move.w  #0x8F02,VDP_CTRL

        lea     0x220000,a0             | snapshot A (Word RAM, main view)
        lea     0xFF1000,a1
        bsr     Copy24k
        lea     0x226000,a0             | snapshot B
        lea     0xFF7000,a1
        bsr     Copy24k

        move.l  #0x41554431,0xFF0FF0    | 'AUD1' marker for the analyzer

        move.l  #0xC0000000,VDP_CTRL
        move.w  #0x0EE0,VDP_DATA        | backdrop cyan: audit complete
Spin:
        bra     Spin

Copy24k:
        move.w  #0x17FF,d0              | 0x1800 longs
ck_loop:
        move.l  (a0)+,(a1)+
        dbra    d0,ck_loop
        rts
