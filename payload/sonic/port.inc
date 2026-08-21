| Sonic on the Mega CD servant: what changes, and why.
|
| The engine is GEOS-Genesis's, unmodified except where this file says so.
| It was written for a machine where GEOS owns all of Genesis work RAM,
| draws a 1bpp bitmap at logical $A000, and has its own VDP layout. Here
| it is a payload -- Genesis-side code handed the machine by a servant
| that spends the rest of its life converting an Atari ST screen into
| tiles. Four things differ, and only four.
|
| THE ANCHOR. GEOS puts its 64KB image at $FF0000 and points A6 at
| $FF8000, so `(sym-GEOS_BIAS)(%a6)` reaches any of it. All of that is
| the servant's memory here. But the engine only ever names 68 symbols,
| spanning logical $8000..$A000 -- eight kilobytes, and 56 of them in one
| page. So A6 points at an 8KB block of the payload's own workspace and
| the same addressing works untouched: logical L lands at A6 + (L-$8000).
|
| THE GROUND. Sonic's foot sensors read the desktop as terrain. On GEOS
| that is a 1bpp bitmap of 8-byte cards; here it is the ST screen, 320x200
| in four interleaved bitplanes, which the servant hands over at entry.
| sonic_pixel is rewritten for that layout. The rule is unchanged -- a
| pixel is solid if it is not the background colour, and three in a row
| make a surface -- and so is every caller.
|
| THE WINDOW. The art pool is fifty thousand bytes and a payload has
| thirty-two. It lives in PRG RAM above the C: ramdisk instead, reached
| through the Mega CD's 128KB window, which shows one bank at a time. The
| art's bank is the one selected for the whole run, because every frame
| reads it; sonic_pixel switches to the screen's for four words and back,
| and it is the only thing in the engine that reads the screen. Neither
| address nor bank is written down here -- the servant hands both over.
|
| THE VDP MAP. GEOS's nametables and tile pages are not where the
| servant's are, so the addresses below are re-based onto free space in
| ours. Nothing else about the engine's use of them changes.

| ---- the servant's VDP layout, which this payload must fit into ------
| plane A     $C000   (64x32, the ST screen at tiles 0..999)
| window      $B000
| plane B     $E000
| SAT         $F800
| hscroll     $FC00
| tiles       0..999 the screen, 1000 blank, 1001 paper, 1024.. keyboard
|
| Free tile space is therefore $9800..$AFFF and $D000..$DFFF, which is
| what the three pools below are placed in.

.equ SN_PORT,           1               /* engine.S tests this */

.equ DESKTOP_PLANE_A,   0xC000
.equ HSCROLL_BASE,      0xFC00
.equ SAT_BASE,          0xF800
.equ GEOS_TILE_BASE,    0               /* the servant numbers cards from 0;
                                         * GEOS left tile 0 blank and started
                                         * at 1 */
.equ SONIC_TILE,        1216            /* $9800, 32 tiles */
.equ DUST_TILE,         1248            /* $9C00, 16 tiles */
.equ SIGN_TILE,         1664            /* $D000, 58 tiles -- between plane A's
                                         * page and plane B */
.equ SONIC_PAL_LINE,    2               /* line 0 is the ST screen's sixteen;
                                         * 2 and 3 are the keyboard's and it is
                                         * not up. The servant reloads all four
                                         * on the way out. */
.equ SONIC_SAT_FIRST,   0               /* the whole sprite table.
                                         * On GEOS entry 0 was the mouse
                                         * pointer and 1-7 were the
                                         * application's; the servant draws
                                         * the pointer into the ST screen
                                         * like everything else and parks
                                         * the table as one dummy entry
                                         * whose link is zero -- so a chain
                                         * starting at 8 was never walked
                                         * and nothing appeared at all. */
.equ GEOS_DISPLAY_Y,    12              /* THE SERVANT CENTRES THE SCREEN, and
                                         * this said it did not.  iofw's
                                         * SCREEN_YOFF is 12: the display is
                                         * 224 lines, the ST screen is 200, and
                                         * plane A is scrolled down by twelve so
                                         * the picture sits in the middle with a
                                         * border above and below, the way a
                                         * television shows an ST.  Sprites are
                                         * not scrolled with the plane, so this
                                         * offset is theirs to add -- and GEOS's
                                         * own constant was 12 for the same
                                         * reason on the same kind of screen.
                                         *
                                         * With it at zero every sprite was
                                         * drawn twelve lines above the desktop
                                         * it stood on: he landed on a program
                                         * icon's bottom edge and appeared in
                                         * the middle of the hollow, he stood on
                                         * the trash can's lid and appeared over
                                         * it, and there was never anything
                                         * under his shoes because what he was
                                         * standing on was twelve lines lower.
                                         *
                                         * Measured, in the end, by correlating
                                         * every row of the framebuffer against
                                         * every row of the rendered frame: the
                                         * fit is exact at twelve and the mean
                                         * error is zero. */

| ---- the Mega CD window ---------------------------------------------
| The one register this port touches that the engine never heard of.
| Bits 7-6 choose which 128K bank of PRG RAM the window shows; the art
| is in one and the ST screen is in another, and which is which comes
| from the servant at entry rather than from here. See main.S.
.equ GA_MEMMODE_A,      0xA12003

| The smallest blob that could be the art: the header alone is $44 bytes
| and the frame index follows it. Purely a refusal test -- NATIVE.PRG
| has already checked the file -- so it only has to be too big to be an
| accident.
.equ SN_BLOB_MIN,       0x1000

| ---- the pad ---------------------------------------------------------
| These were engine.S's, where genesis_input_update was the only writer
| and the engine the only reader. Here shim.S is the writer, so they
| belong to both files and live in neither.
.equ PAD_UP,    0
.equ PAD_DOWN,  1
.equ PAD_LEFT,  2
.equ PAD_RIGHT, 3
.equ PAD_A,     4
.equ PAD_B,     5
.equ PAD_C,     6
.equ PAD_START, 7
