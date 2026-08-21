/* m1boot.s -- boot the cart the way megadev boots a cart.
 *
 * Four builds went into a hand-rolled Mega Drive header and init, and
 * four builds came back from the console as "no change". The faults
 * were: a region field eight bytes late because fifty-two padding
 * bytes were written as sixty; a ROM size that was neither a power of
 * two nor a whole number of blocks; a Z80 bus request that waited
 * forever for an acknowledge; and a VDP write placed in front of the
 * TMSS handshake, which locks the 68000 on any console later than an
 * early Model 1.
 *
 * Every one of those is boilerplate, and this repository has had a
 * Mega Drive devkit vendored in it the whole time. megadev's header is
 * built from .org directives, so a field cannot land eight bytes late;
 * its init does the TMSS handshake before it touches the VDP, reads
 * the VDP control port to cancel any command the flash cart's menu
 * left half-written, clears work RAM, and hands the Z80 a dummy
 * program rather than fighting it for the bus.
 *
 * So the boot is megadev's and the Mode 1 work is ours. `main` is in
 * m1tool.S.
 *
 * megadev is MIT-licensed; see vendor/megadev/LICENSE.
 */

#include "md_vectors.s"

/* The header is ours, but built megadev's way: every field placed with
 * .org, so none of them can drift. That technique is the fix for the
 * bug that started this -- a region field eight bytes late because a
 * run of padding was miscounted.
 *
 * megadev's own md_header.s is not used. It omits the RAM start and
 * end longwords at 0x1A8/0x1AC and then places everything after them
 * by size rather than by offset, so its region field lands at 0x1EB;
 * its extra-memory field is a documented TODO. The vectors and the
 * init are what this repository wanted from it, and those are sound.
 */
.org 0x100
        .ascii  "SEGA MEGA DRIVE "
.org 0x110
        .ascii  "(C)EMUTOS 26.AUG"
.org 0x120
        .ascii  "MCD BRAM DUMP                                   "
.org 0x150
        .ascii  "MCD BRAM DUMP                                   "
.org 0x180
        .ascii  "GM MCDBRAM-00 "
.org 0x18E
        .word   0                       | checksum: not checked by hardware
.org 0x190
        .ascii  "JC              "      | 3-button pad, and a Mega CD
.org 0x1A0
        .long   0x00000000              | ROM start
        .long   0x00007FFF              | ROM end, corrected after padding
        .long   0x00FF0000              | RAM start
        .long   0x00FFFFFF              | RAM end
.org 0x1B0
        /* Extra memory: the save declaration. Filled in by
         * tools/build-rom.sh, which is also where it is checked. */
        .fill   12, 1, 0x20
.org 0x1BC
        .fill   12, 1, 0x20             | modem
.org 0x1C8
        .fill   40, 1, 0x20             | notes
.org 0x1F0
        .ascii  "JUE             "      | region
.org 0x200

#include "md_init.s"

GLOBAL INIT_SSP 0xFFFFFC00
GLOBAL INIT_PC init_system // init_system is located in md_init.s

/*
all exception vectors/traps are set to a simple `rte` opcode (ex_null in
md_init.s) sicne this is a simple example. You will want to assign the
exception vectors to your own handlers in a full program.
*/
GLOBAL ERR_BUS ex_null
GLOBAL ERR_ADDRESS ex_null
GLOBAL ERR_ILLEGAL ex_null
GLOBAL ERR_ZERODIV ex_null
GLOBAL EX_CHK ex_null
GLOBAL EX_TRAPV ex_null
GLOBAL ERR_VIOLATION ex_null
GLOBAL EXEXVEC_TRACE ex_null
GLOBAL EX_LINE_1010 ex_null
GLOBAL EX_LINE_1111 ex_null
GLOBAL EX_UNINITIALIZED ex_null
GLOBAL EX_SPURIOUS ex_null
GLOBAL INT1 ex_null
/* The three the cart example leaves to its own handlers. Nothing here
 * wants an interrupt: the program runs once, start to finish, with the
 * mask at 7. */
GLOBAL INT2_EXT ex_null
GLOBAL INT4_HBLANK ex_null
GLOBAL INT6_VBLANK ex_null
GLOBAL INT3 ex_null
GLOBAL INT5 ex_null
GLOBAL INT7 ex_null
GLOBAL TRAP_0 ex_null
GLOBAL TRAP_1 ex_null
GLOBAL TRAP_2 ex_null
GLOBAL TRAP_3 ex_null
GLOBAL TRAP_4 ex_null
GLOBAL TRAP_5 ex_null
GLOBAL TRAP_6 ex_null
GLOBAL TRAP_7 ex_null
GLOBAL TRAP_8 ex_null
GLOBAL TRAP_9 ex_null
GLOBAL TRAP_A ex_null
GLOBAL TRAP_B ex_null
GLOBAL TRAP_C ex_null
GLOBAL TRAP_D ex_null
GLOBAL TRAP_E ex_null
GLOBAL TRAP_F ex_null
