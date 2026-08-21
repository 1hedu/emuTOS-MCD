/* E0 proof-of-boot payload (M_INIT.PRG): runs on the main CPU from Word RAM
 * at 0x200000, paints the whole screen green via the VDP backdrop, spins.
 * If the frame dump is green, the entire chain worked: BIOS boot, security
 * block accepted, SP loaded us from ISO9660 over the CDC, Word RAM handoff,
 * main CPU jump.
 */

        .equ    VDP_DATA, 0xC00000
        .equ    VDP_CTRL, 0xC00004

Entry:
        move.w  #0x8004,VDP_CTRL        | reg 0: normal operation
        move.w  #0x8144,VDP_CTRL        | reg 1: display on, V28
        move.w  #0x8F02,VDP_CTRL        | reg 15: autoincrement 2

        move.l  #0xC0000000,VDP_CTRL    | CRAM address 0
        move.w  #0x0EEE,VDP_DATA        | color 0 (backdrop) = white (payload proof)

Spin:
        bra     Spin
