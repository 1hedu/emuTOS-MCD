/*
 * GEOS 64 logical memory map on Sega Genesis.
 *
 * Every original 16-bit GEOS address maps to Genesis WRAM at:
 *     physical = 0x00FF0000 + logical
 *
 * The byte layout remains little-endian exactly as on the 6502.  Native
 * 68000 routines must use LOAD_LE16 / STORE_LE16 for GEOS words.
 *
 * ---------------------------------------------------------------------------
 * EVERY NUMBER IN THIS FILE IS A GEOS LOGICAL ADDRESS.
 *
 * `dirEntryBuf' is $8400 and that is where it is.  Nothing has to be added to
 * it, taken off it, or looked up.  It is the number the kernal API passes about
 * in r0..r15, the number the original documentation uses, and the number an
 * application means when it says where its data is.
 *
 * It was not always so.  These used to be displacements off A6 -- the anchor at
 * logical $8000 -- so `dirEntryBuf' read `0x0400', thirty-two kilobytes from
 * where it lives, while `CBM_R0 = 0x0400' beside it really was $0400.  Two kinds
 * of number that looked identical, and they were read wrong.
 *
 * The anchor comes off at the point of use instead:
 *
 *     (dirEntryBuf-GEOS_BIAS)(%a6)
 *
 * and in the LOAD_LE16 / STORE_LE16 / SET_LE16_IMM family it comes off inside
 * the macro, so those call sites name the address plain.  A6 is the base because
 * a signed displacement off it reaches all sixty-four kilobytes where A5 reaches
 * half; A5 is not a base for anything named here.
 *
 * tools/verify_geos_map.py refuses a bare displacement and refuses a negative
 * value, a negative being an offset by definition:
 *
 *     python3 tools/verify_geos_map.py        # check
 *     python3 tools/verify_geos_map.py --md   # the whole map, by address
 * ---------------------------------------------------------------------------
 */

.equ GEOS_BASE,          0x00FF0000
.equ GEOS_SIZE,          0x00010000
.equ GEOS_BIAS,          0x00008000
.equ GEOS_ANCHOR,        0x00FF8000
.equ GEOS_STACK_TOP,     0x00FF0400  /*native stack grows through original $0100-$03FF pre-APP gap */

/* Original zero page registers. */
.equ zpage,              0x0000
.equ r0,                 0x0002
.equ r1,                 0x0004
.equ r2,                 0x0006
.equ r3,                 0x0008
.equ r4,                 0x000A
.equ r5,                 0x000C
.equ r6,                 0x000E
.equ r7,                 0x0010
.equ r8,                 0x0012
.equ r9,                 0x0014
.equ r10,                0x0016
.equ r11,                0x0018
.equ r12,                0x001A
.equ r13,                0x001C
.equ r14,                0x001E
.equ r15,                0x0020
.equ r0L,                r0
.equ r0H,                r0+1
.equ r1L,                r1
.equ r1H,                r1+1
.equ r2L,                r2
.equ r2H,                r2+1
.equ r3L,                r3
.equ r3H,                r3+1
.equ r4L,                r4
.equ r4H,                r4+1
.equ r5L,                r5
.equ r5H,                r5+1
.equ r6L,                r6
.equ r6H,                r6+1
.equ r7L,                r7
.equ r7H,                r7+1
.equ r8L,                r8
.equ r8H,                r8+1
.equ r9L,                r9
.equ r9H,                r9+1
.equ r10L,               r10
.equ r10H,               r10+1
.equ r11L,               r11
.equ r11H,               r11+1
.equ r12L,               r12
.equ r12H,               r12+1
.equ r13L,               r13
.equ r13H,               r13+1
.equ r14L,               r14
.equ r14H,               r14+1
.equ r15L,               r15
.equ r15H,               r15+1

.equ curPattern,         0x0022
.equ string,             0x0024
.equ baselineOffset,     0x0026
.equ curSetWidth,        0x0027
.equ curHeight,          0x0029
.equ curIndexTable,      0x002A
.equ cardDataPntr,       0x002C
.equ currentMode,        0x002E
.equ dispBufferOn,       0x002F
.equ mouseOn,            0x0030
.equ msePicPtr,          0x0031
.equ windowTop,          0x0033
.equ windowBottom,       0x0034
.equ leftMargin,         0x0035
.equ rightMargin,        0x0037
.equ pressFlag,          0x0039
.equ mouseXPos,          0x003A
.equ mouseYPos,          0x003C
.equ returnAddress,      0x003D
.equ graphMode,          0x003F

.equ a2,                 0x0070
.equ a3,                 0x0072
.equ a4,                 0x0074
.equ a5,                 0x0076
.equ a6,                 0x0078
.equ a7,                 0x007A
.equ a8,                 0x007C
.equ a9,                 0x007E
.equ a0,                 0x00FB
.equ a1,                 0x00FD

/* Major original regions. */
.equ APP_RAM,            0x0400
.equ BACK_SCR_BASE,      0x6000

/* Firmware BASIC's memory: the C64's forty kilobytes, in the two runs of this
 * machine's RAM that are free once GEOS has been torn down.
 *
 * `Special -> BASIC' is a teardown -- DeskTop.main.s:786 GoToBasic jumps into
 * the lokernal and GEOS does not come back -- so what BASIC is entitled to is
 * everything nothing else is still using.  That is the application window and
 * the back screen ($0400..$7FFF, 31744 bytes) and the canonical GEOS kernal
 * mirror ($BF40..$FE7F, 16192), which exists only so _Rectangle can dereference
 * a pattern and _PutChar a BSW glyph, and neither runs here.  What is NOT free
 * is $8000..$9FFF, the OS variables and the disk window, which the interrupt
 * handler and the storage driver keep using; and $A000..$BF3F, the bitmap the
 * text is stamped into.
 *
 * So the map is in two pieces and the seam is at $7C00:
 *
 *     C64 $0000..$7BFF   31744 bytes   ->  $0400..$7BFF
 *     C64 $7C00..$9FFF    9216 bytes   ->  $BF40..$E33F
 *
 * Forty kilobytes exactly, which is what MEMSIZ-TXTTAB = 38911 says on the
 * banner.  Every access goes through cb_ptr (src/kernal/basic/cbm_mem.S), so
 * the seam is one comparison and nothing above it has to know.
 *
 * $C000..$CFFF is not backed, and it is the one region whose absence costs
 * nothing: it is where a C64 program puts machine code, and the 6502 is never
 * executed here.  PEEK of it reads zero and POKE is dropped, which is stated
 * rather than pretended.
 *
 * The four arrays after the second region are the Genesis's, not the C64's.
 * Colour RAM is a separate chip on a C64 and a separate array here.  CBM_ATTR
 * is what GEN_COLOR_PTR wants -- (ink<<4)|paper per card -- because the
 * renderer takes a pair and the C64 stores only the ink.  CBM_SHADOW and
 * CBM_ASHADOW are the screen and the attributes as last stamped: the VIC-II
 * reads $0400 every frame and needs no such thing, and a machine whose screen
 * is somewhere else needs to know what changed. */
.equ CBM_R0,             0x0400  /*C64 $0000..$7BFF */
.equ CBM_R1,             0xBF40  /*C64 $7C00..$9FFF */
.equ CBM_SPLIT,          0x7C00  /*the C64 address the second region starts at */
.equ CBM_TOP,            0xA000  /*one past the last backed C64 RAM address */
.equ CBM_COLOR,          0xE340  /*the C64's $D800..$DBFF */
.equ CBM_ATTR,           0xE740  /*1000 bytes: GEN_COLOR_PTR's matrix */
.equ CBM_SHADOW,         0xEB28  /*1000 bytes: codes last stamped */
.equ CBM_ASHADOW,        0xEF10  /*1000 bytes: attributes last uploaded */
.equ CBM_VIC,            0xF2F8  /*the C64's $D000..$D02E, as a register file */
.equ CBM_SCRATCH,        0xF328  /*64 bytes the interpreter needs and the C64
                                  * does not: the READY loop's saved stack
                                  * pointer, which a 6502 gets for free by
                                  * doing LDX #$FA / TXS on an error, the two
                                  * packed operands every arithmetic step goes
                                  * through, and the GOSUB and FOR frames the
                                  * 6502 keeps on its own stack and a 68000
                                  * with a real call stack cannot */
.equ CBM_END,            0xF728

/* Record 17's own image: geoBASIC's debugger, 162 bytes laid out exactly as
 * $4EEC..$4F8D are on the disk, so the offsets in the listing are the offsets
 * here.
 *
 * It is HERE, above CBM_END, and not at its own C64 addresses -- which was
 * tried first, on the reasoning that C64 $4EEC is backed for real and putting
 * it anywhere else invents a home for something that has one.  That reasoning
 * is wrong, and the way it is wrong is worth writing down: CBM_R0 puts C64
 * $0000..$7BFF at GEOS $0400..$7FFF, and GEOS $0400..$5FFF is the APPLICATION
 * WINDOW.  C64 $4EEC is GEOS $52EC, which is in the middle of whatever
 * application is loaded -- and the application that would be loaded here is
 * geoBASIC, whose panel this block belongs to.  On the disk record 17 really
 * does land on top of the editor and that is fine, because the editor is 6502
 * code that has been swapped out; in this port the application is native and
 * is still there.
 *
 * $F728 up to $FFFF is the last unclaimed band in GEOS RAM -- past the screen
 * at $A000, past the C64 image's second region, past the colour and shadow
 * arrays and past CBM_SCRATCH -- and 162 bytes of it are this. */
.equ GEN_GB_DBG,         0xF728  /*162: $4EEC..$4F8D, offset for offset */
.equ GB_DBG_PAGE,          0x00  /*byte: $4EEC, which page is up */
.equ GB_DBG_HIT,           0x01  /*byte: $4EED, which check fired, $FF none */
.equ GB_DBG_SAVE,          0x03  /*2: $4EEF, $0A53/$0A54 across the record */
.equ GB_DBG_BPLO,          0x05  /*4: $4EF1, the low halves */
.equ GB_DBG_BPHI,          0x09  /*4: $4EF5, and the high -- a SPLIT table */
.equ GB_DBG_EDIT,          0x0D  /*15: $4EF9, the field being typed */
.equ GB_DBG_CHECK,         0x1D  /*4 x 16: $4F09, the expressions $488E runs */
.equ GB_DBG_VALUE,         0x5D  /*4 x 16: $4F49, and the ones $4C34 prints */
.equ GB_DBG_SLOT,          0x9D  /*byte: $4F89, the slot in hand */
.equ GB_DBG_YTAB,          0x9E  /*4: $4F8A, 50 64 78 92 */
.equ GB_DBG_LEN,           0xA2
.equ GB_DBG_SLOTS,            4
.equ GB_DBG_SLOTLEN,         16
/* And what is in it, in one place, because two of these overlapped once: the
 * digits FIN reads a number out of were on top of the FOR/GOSUB stack pointer,
 * so parsing `10' set the pointer to $31 and the next FOR said OUT OF MEMORY. */
.equ CBM_SC_SP,          0     /*long: the READY loop's stack pointer */
.equ CBM_SC_RUN,         4     /*byte: a program is running */
.equ CBM_SC_JMP,         5     /*byte: a statement moved TXTPTR itself */
.equ CBM_SC_SINK,        6     /*byte: PRINT is going to a channel on a disk,
                                * so the screen must not see it */
/* The two dialects CBM_SC_DIALECT selects between. */
.equ DIA_CBM,            0
.equ DIA_GEOB,           1
.equ CBM_SC_DIALECT,     7     /*byte: which BASIC's tables the interpreter is
                                * reading -- 0 Commodore V2, 1 geoBASIC.  They
                                * are the same interpreter; see the note beside
                                * DIA_CBM in cbm_interp.S */
.equ CBM_SC_GBCOL,      10     /*byte: geoBASIC's SETCOL, which is both the
                                * colour DrawLine and DrawPoint take as a carry
                                * ($54FF) and the pattern byte FrameRectangle
                                * takes in A ($5544).  $0A2E on the disk */
.equ CBM_SC_GSP,         8     /*byte: how many FOR/GOSUB frames are live */
.equ CBM_SC_ERRGB,       9     /*byte: the number cb_error is holding is one of
                                * geoBASIC's fifty rather than one of the C64's
                                * twenty-nine.  The two sets have nothing to do
                                * with each other -- SYNTAX is 11 there and 1
                                * here -- so which table to read from cannot be
                                * worked out from the number */
.equ CBM_SC_L,           16    /*the packed left operand */
.equ CBM_SC_R,           24    /*and the right */
.equ CBM_SC_T,           32    /*a third, for the ones that need it */
.equ CBM_SC_X,           40    /*two more packed operands, for SQR */
.equ CBM_SC_Y,           48
.equ CBM_SC_NUM,         64    /*FIN's digits, 24 bytes */
.equ CBM_SC_OUT,         96    /*FOUT's answer, 32 */
.equ CBM_SC_GST,         128   /*25 frames of 12 */
.equ CBM_SC_FOR,         432   /*and 25 limit/step pairs of 10 */
/* geoBASIC's printer.  $0A4B is whether printing is on, $1242 whether the
 * driver has been started, $08DF the print column, and the line the driver is
 * handed is $0428 on the disk -- which in this port is the emulated C64 screen,
 * so the line lives here instead.  684..768 is the gap between the FOR frames
 * and the QA scoreboard, and 84 bytes is exactly what is left in it. */
.equ CBM_SC_GBPR,       684   /*84 */
.equ GB_PR_ON,            0   /*byte: $0A4B, $80 on and 0 off */
.equ GB_PR_OPEN,          1   /*byte: $1242, the driver has been started */
.equ GB_PR_COL,           2   /*byte: $08DF, the print column */
.equ GB_PR_NLQ,           3   /*byte: PRNTER asked for near-letter-quality */
.equ GB_PR_LINE,          4   /*80: the line, the disk's $0428 */
.equ CBM_SC_QA,          768   /*the QA selftest's own diagnosis */
/* And the QA chain's proof that RstrFrmDialogue does not come back: a two-byte
 * dialog descriptor -- a defined position and the terminator, which is a box
 * with no commands and so no way out but the answer -- followed by a byte the
 * routine that answered sets AFTER its call, which canon never reaches.  It is
 * here rather than in the free band because it is QA scratch and this block is
 * the QA scratch; the descriptor's logical address is CBM_SCRATCH + this. */
.equ CBM_SC_QADLG,       776   /*2, and a byte at +2 */
.equ CBM_SC_SUB,         800   /*a subscript list: 16 words.  The C64 keeps
                                * these on the 6502 stack between $B1DB and
                                * $B2F2, which is why it has no fixed limit;
                                * this does, and says BAD SUBSCRIPT past it. */
.equ CBM_SC_SUBMAX,      16
.equ CBM_SC_PW,          832   /*four more packed slots: the transcendentals
                                * evaluate a polynomial while the operators
                                * they call are already using SC_L and SC_R */
.equ CBM_SC_PX,          840
.equ CBM_SC_PY,          848
.equ CBM_SC_PZ,          856
/* geoBASIC's pen.  On the disk it is $B6..$B8 in geoBASIC's own zero page and
 * there is nowhere to put it here, so it lives in this scratch.
 *
 * Its WINDOW does NOT live here, and the first reading of it said otherwise.
 * $33..$38 on the disk are annotated windowTop, windowBottom, leftMargin and
 * rightMargin because that is exactly what they are: GEOS's own graphics
 * window, which geoBASIC writes directly.  The reason given for moving them --
 * that $33 is FRETOP, $35 is FRESPC and $37 is MEMSIZ -- was about the C64
 * BASIC zero page, and that is a different memory from GEOS's globals in this
 * port.  So geoBASIC's window is the port's windowTop/windowBottom/leftMargin/
 * rightMargin, and every kernal routine that clips to the window now honours
 * it, which is what makes WINDOW a window on the disk. */
.equ CBM_SC_GBPEN,      972    /*4: x word, y byte */
/* geoBASIC's named objects -- the menus, dialogs, icons, bitmaps and sprites
 * the visual editor puts in the document, which MENU/DIALOG/ICON/BITMAP/SPRITE
 * reach BY NAME rather than by address.  The disk keeps the table's base in
 * $85/$86 and its length in $0A2F, binary-searches it at $222F on a six-byte
 * key, and the entries are eight bytes with the data pointer at +6. */
.equ CBM_SC_GBOBJ,      984    /*14: base word, count byte, 6-byte key, the
                                * descriptor just found and its item table.
                                * 964..972 is free again, where the window was */
.equ GB_OBJ_BASE,        0     /*word: where the table is.  $85/$86 */
.equ GB_OBJ_N,           2     /*byte: how many entries.  $0A2F */
.equ GB_OBJ_KEY,         4     /*6: the kind byte then five name characters.
                                * $0251 and $0252..$0256.  At 4 and not at 3 so
                                * that the two words after it stay even -- a
                                * `clr.w' on an odd displacement off A6 is a
                                * 68000 address error, and the machine stops
                                * with a red screen rather than misbehaving */
.equ GB_OBJ_DESC,       10     /*word: the one the last lookup found.  $C3/$C4 */
.equ GB_OBJ_ITEMS,      12     /*word: and its item table.  $0A53/$0A54 */
.equ GB_OBJ_ENTRY,       8     /*bytes an entry.  $11C0 shifts left three */
/* The kind byte each statement asks for, from the LDA in front of its JSR
 * $0729: $0400 SPRITE, $0602 DIALOG, $0738 MENU, $0781 BITMAP, $07E5 ICON. */
.equ GB_K_MENU,       0x80
.equ GB_K_DIALOG,     0x81
.equ GB_K_ICON,       0x82
.equ GB_K_SPRITE,     0x83
.equ GB_K_BITMAP,     0x84
/* QA only: the seven things DoIcons takes and case 70 has to put back, so that
 * the desktop's D-pad walk later in the same boot starts where it expects. */
/* geoBASIC's open file.  The disk keeps these in the resident -- $0A3D open,
 * $6F10 VLIR rather than sequential, $0A3E/$0A3F ReadByte's r5, $0A40 bytes
 * pending in the record buffer, $0A42/$0A43 the track and sector it reads from
 * -- and eight bytes is exactly what was left here, where the window used to
 * be. */
.equ CBM_SC_GBFILE,     964   /*8 */
.equ GB_FI_OPEN,          0   /*byte: $0A3D, $40 when a file is open */
.equ GB_FI_SPARE,         1   /*byte: was $6F10, which is not scratch at all --
                               * see GB_FI_HDR */
.equ GB_FI_LIM,           2   /*byte: $0A3E, which is ReadByte's r5L -- how
                               * many of the block's bytes are real */
.equ GB_FI_IDX,           3   /*byte: $0A3F, ReadByte's r5H, the index into the
                               * block.  EOF's DEC $0A3F un-reads its byte */
.equ GB_FI_WLIM,          4   /*byte: $0A40, the WRITE side's r5L */
.equ GB_FI_WIDX,          5   /*byte: $0A41, its r5H -- and $7355 writes this
                               * one into the last block's link as the count */
.equ GB_FI_PEND,          4   /*the two above as a word, which is $7270's
                               * `LDA $0A40 / ORA $0A41': anything in either
                               * means the buffer has something to flush */
.equ GB_FI_TS,            6   /*word: $0A42/$0A43, which is r1 -- the track and
                               * sector ReadByte walks the chain from, NOT a
                               * length.  OPEN takes it from fileHeader+2/+3
                               * (the VLIR index block's record 0) and PTREC
                               * from what PointRecord leaves in r1 */
/* And the two buffers, which are NOT in this scratch: they are machine RAM,
 * and this port backs C64 $7C00..$9FFF at CBM_R1, so the record buffer sits at
 * the address the disk uses.  $7270 hands WriteRecord r4 = $8000. */
.equ GB_FI_BUF,      0x8000   /*C64: the record buffer, the disk's own place */
.equ GB_FI_HDR,      0x9C00   /*C64: 256, THE GEOS FILE HEADER BLOCK, which is
                               * what the disk keeps at $6ECA -- and its three
                               * named cells are fields of it, not variables:
                               * $6F0F is +69 O_GHGEOS_TYPE, $6F10 is +70
                               * O_GHSTR_TYPE (the `VLIR rather than sequential'
                               * flag), and $6F17 is +77 O_GHFNAME, the
                               * permanent name SaveFile puts in the directory.
                               * HEADER writes all three and CREATE hands the
                               * block to SaveFile as r9 */
.equ GB_FI_SEP,      0x9DFF   /*C64: the `,' $59A1 puts at $08E3, which the
                               * item-taker has to be standing on */
.equ GB_FI_STR,      0x9E00   /*C64: 256, the string DREAD reads.  The disk
                               * puts it at $08E4 and this port cannot: $08E4
                               * is above TXTTAB here, which is to say it is
                               * inside the BASIC program.  $9E00 is in the
                               * same reserved stretch as the record buffer
                               * and the name, all of it above GB_FI_MEMTOP */
.equ GB_FI_NUM,      0x9F80   /*C64: 32, where a NUMBER written by WRITE is
                               * turned into its text.  $7387 hands the FAC to
                               * $6484 first and writes what comes back */
.equ GB_FI_NAME,     0x9F00   /*C64: 17, the NUL-terminated name OpenRecordFile
                               * wants, which a length-counted BASIC string is
                               * not */
.equ GB_FT_BUF,      0x8100   /*C64: 5632, where FONT's record lands.  Above
                               * GB_FI_MEMTOP, so BASIC never reaches it, and
                               * clear of GB_FI_BUF's 256 at $8000.  It was
                               * 6656 and is a PORT-CHOSEN number either way --
                               * the disk reads whatever the font's own table
                               * says and California's largest is 2110 -- so
                               * the thousand COLRECT needs came off the end of
                               * it rather than out of a GEOS RAM image that is
                               * exactly full. */
/* geoBASIC's colour matrix.  On the disk COLRECT ($6CC3) fills GEOS's own
 * colorMatrix at $8C00, a thousand bytes of (foreground<<4)|background, one a
 * card, forty to a row -- and this port has no GEOS colorMatrix: what it has
 * is GEN_COLOR_PTR, which takes a matrix of exactly that shape from whoever
 * owns the screen.  So geoBASIC gets one of its own and publishes it.
 *
 * It starts as `screencolors' in every card, which is what a null
 * GEN_COLOR_PTR already means, so publishing it changes nothing until the
 * first COLRECT has put something in it. */
.equ GB_CM,          0x9700   /*C64: 1000 */
.equ GB_CM_CARDS,    1000
.equ GB_CM_WIDTH,    40
.equ GB_FT_HDR,      0x9B00   /*C64: 256, the FONT FILE's own header block --
                               * the disk reads it to $02CF and walks the point
                               * sizes at +130 and their byte counts at +97 */
/* geoBASIC's soft sprites -- SPRITE, SPRT and SPRCOL, and the per-tick engine
 * at $2441 that moves them.  Sixteen byte tables and four word tables, all
 * indexed by the VIC sprite number, which is the geoBASIC sprite number less
 * one: $0400's `LDA ($C3),Y / TAX / DEX' and $2454's walk from 2 to 7 agree
 * that 1 and 2 are geoBASIC's own and 3..8 are the program's.
 *
 * They are C64 addresses because on the disk they are C64 addresses -- $02A5,
 * $0A65..$0AD4 and eight bytes of zero page each -- and a program is entitled
 * to PEEK them.  They go in the reserved stretch above GB_FI_MEMTOP with the
 * FONT buffers, rather than at their own addresses, because $0A71 is inside
 * the BASIC program here and $C3 is inside the interpreter's zero page. */
/* geoBASIC's dialog boxes.  $0602 copies the descriptor's table to $02E0 and
 * rewrites it there -- it cannot rewrite the document -- and keeps four bytes
 * a fixup at $02BF: the offset the terminator went to, the character it went
 * over, and where.  Both are C64 addresses because they are C64 addresses on
 * the disk, in the page RAMTAS clears. */
.equ GB_DL_BUF,      0x02E0   /*C64: the table DoDlgBox is handed */
.equ GB_DL_MAX,      256
.equ GB_DL_FIX,      0x02BF   /*C64: four bytes a fixup, and $0A1 counts them */
.equ GB_DL_FMAX,     8
.equ GB_DL_NUL,      (GB_DL_FIX-1)  /*$6F2F: what an empty answer points at */
.equ GB_DL_TXT,      0x0200   /*where a DBVARSTR's expression is evaluated from */

.equ GB_SP_STATE,    0x9D00   /*C64: 200, and NOT $9C00: GB_FI_HDR is there,
                               * and $9C00 cost case 76 -- CREATE builds the
                               * GEOS file header block in it */
.equ GB_SP_MODE,          0   /*8 bytes: $02A5, the runtime mode -- bit 7 work
                               * out a course, 6 the group follows, 5 on its
                               * way somewhere, 3 double width, 2 double
                               * height, 1 the animation loops, 0 stop on
                               * arrival */
.equ GB_SP_COL,           8   /*8: $0A71, and $2481 writes it to $D027,X */
.equ GB_SP_XH,           16   /*8: $C3, which is x/2 -- a VIC x is nine bits */
.equ GB_SP_XF,           24   /*8: $0ABF, and its fraction */
.equ GB_SP_Y,            32   /*8: $CF */
.equ GB_SP_YF,           40   /*8: $0AC5 */
.equ GB_SP_DXL,          48   /*8: $D5, the fraction of dx -- or the low byte
                               * of a destination, which is the same pair read
                               * the other way round */
.equ GB_SP_DXH,          56   /*8: $DB */
.equ GB_SP_DYL,          64   /*8: $C9 */
.equ GB_SP_DYH,          72   /*8: $E1 */
.equ GB_SP_ADLY,         80   /*8: $0A77, ticks until the next frame */
.equ GB_SP_ARLD,         88   /*8: $0A7D, and what it reloads from */
.equ GB_SP_AFRM,         96   /*8: $0A8F, the frame it is on */
.equ GB_SP_ANUM,        104   /*8: $0A89, how many there are */
.equ GB_SP_ALST,        112   /*8: $0A95, the one last drawn -- $FF forces a
                               * redraw and $254A draws nothing when they agree */
.equ GB_SP_GRP,         120   /*8: $0A83, how many sprites follow this one,
                               * which is the count in the descriptor less one */
.equ GB_SP_IMG,         128   /*8 words: $0A9B/$0AA1, frame 0's bitmap */
.equ GB_SP_STEP,        144   /*8 words: $0AA7/$0AAD, ticks left to arrival */
.equ GB_SP_HDLR,        160   /*8 words: $0AB3/$0AB9, the program text $54A9
                               * evaluates a line number out of when it does */
.equ GB_SP_SHAPE,       176   /*8 words: $0A65/$0A6B, the group's offsets */
.equ GB_SP_CUR,         192   /*byte: $0ACD, the one the tick is on */
.equ GB_SP_EVP,         194   /*word: $0ACE/$0ACF, the arrival handler's text */
.equ GB_SP_EVF,         196   /*byte: $0AD4, $80 when one has arrived */
.equ GB_SP_SIZE,        200
.equ GB_FI_MEMTOP,   0x8000   /*and so the top of BASIC comes down to here
                               * under the geoBASIC dialect */
.equ CBM_SC_QAUI,       998    /*20 bytes: the vectors, the pointer, the
                                * mouse region, faultData and the sprite
                                * enable -- everything DoIcons takes */
/* geoBASIC's ONERR: the line the next error is to go to, and the number of the
 * error that went there.  On the disk the line is $0AD0/$0AD1 -- which $1048,
 * the one error entry, looks at before it prints anything -- and the number is
 * $0A5D, with bit 7 set when nothing was armed and the error was reported the
 * ordinary way. */
/* geoBASIC's three drive bytes, which $294C sets from curDrive at startup and
 * only DBFILE moves afterwards: $0A35 the drive the selector is SHOWING,
 * $0A36 the document's, $0A37 the one geoBASIC came up on.  $5243 puts the
 * DISK icon in the box only while the shown drive is not the start drive, and
 * $53A0's `EOR #$01' is what the user icon toggles. */
.equ CBM_SC_GBDRV,       900   /*3 */
.equ GB_DV_SHOWN,          0   /*byte: $0A35 */
.equ GB_DV_DOC,            1   /*byte: $0A36 */
.equ GB_DV_START,          2   /*byte: $0A37 */

/* geoBASIC's DEBUG, and the debugger under it.  Four of the $0Axx bytes, which
 * are geoBASIC's own variables rather than the C64's, so they are here with the
 * rest of the port's geoBASIC state and not in the RAM image:
 *
 *   $0A1E  the mode $53CE puts 2 in
 *   $0A4C  the flag $5CDE puts $80 in
 *   $0A4D  and the one that MATTERS, because the check is in the resident and
 *          not in record 17 at all: record 5's NEWSTT ends `$544F LDA $0A4D /
 *          BEQ $5457 / JMP $482E'.  Nought and the interpreter loops straight
 *          back to $5401; $01 run, $40 step, $80 trace, from the three items
 *          $4DE8/$4DF2/$4DF9 set.
 *   $0A34  the stop $1036 looks at before every statement, which $2451 sets
 *          and $4E04 clears
 *
 * The rest of the debugger -- eight expression slots and four breakpoints --
 * is 162 bytes and is NOT here.  It is record 17's own image, at record 17's
 * own addresses in the C64 RAM this port backs; see GB_DBG_PAGE in
 * src/kernal/basic/geob_lang.S for why that is the right place for it rather
 * than a compromise. */
.equ CBM_SC_GBDBG,       903   /*4: the mode, the flag, the check, the stop */
.equ GB_DB_MODE,          0    /*byte: $0A1E, which $53CE puts 2 in */
.equ GB_DB_FLAG,          1    /*byte: $0A4C, which $5CDE puts $80 in */
.equ GB_DB_ON,            2    /*byte: $0A4D, which $544F reads every statement */
.equ GB_DB_STOP,          3    /*byte: $0A34, the stop $1036 looks at */
.equ GB_DB_RUN,        0x01    /*$4DE8 `*run' */
.equ GB_DB_STEP,       0x40    /*$4DF2 ` step' -- bit 6, which $4831 BVCs on */
.equ GB_DB_TRACE,      0x80    /*$4DF9 ` trace' */


/* geoBASIC's processes -- the six timed lines PROCESS arms.  On the disk the
 * count is $6D51 and the table is $6D39, four bytes an entry, and both live
 * INSIDE record 5's own code: $6D39..$6D50 is a run of zeroes the overlay
 * carries and $6D9D self-modifies $6DA1 between SEC and CLC to search it two
 * different ways. */
.equ CBM_SC_GBPROC,      872   /*26: the count, then six slots of four */
.equ GB_PC_N,             0    /*byte: how many are live.  $6D51 */
.equ GB_PC_SLOT,          2    /*and the first of them.  $6D39 */
.equ GB_PC_LINE,          0    /*word: the line to enter */
.equ GB_PC_COUNT,         2    /*byte: ticks left.  $6D3B */
.equ GB_PC_PERIOD,        3    /*byte: and what it reloads from.  $6D3C */
.equ GB_PC_MAX,           6    /*$6E0F walks six; $6D7E refuses the seventh */

.equ CBM_SC_GBERR,       868   /*4: the armed line as a word, the error
                                * number, one spare.  Above the QA scoreboard,
                                * which is one byte at 864 and used to be
                                * sixty-four -- see cs_score. */
.equ GB_ER_LINE,          0
.equ GB_ER_NO,            2

/* geoBASIC's event handlers -- the lines BUTTON, REDRAW and the icon/menu
 * tables arm, and the two latches that keep one press from being dispatched
 * twice.  On the disk the button line is $6E2C, the redraw line is $0AD2, the
 * deferred one is $6DE7, and the latches are $0AD5 and $0AD6. */
.equ CBM_SC_GBUI,       976    /*8: button word, redraw word, pending word,
                                * busy byte, flags byte */
.equ GB_UI_BUTTON,       0
.equ GB_UI_REDRAW,       2
.equ GB_UI_PEND,         4
.equ GB_UI_BUSY,         6     /*0 idle, 1 in a handler, $80 handled and
                                * waiting for the release.  $0AD5 */
.equ GB_UI_FLAGS,        7     /*bit 7 a deferred handler is armed, bit 6 the
                                * handler asked to stay latched.  $0AD6 */
.equ CBM_SC_FO,          956   /*8: FOUT's own MOVAF slot.  $BC0C rounds the
                                * accumulator and copies it to ARG, and every
                                * multiply and divide in $BDDD starts with one;
                                * it needs a packed five bytes to go through
                                * and none of the operand slots above is free
                                * while PRINT is running. */
.equ CBM_SC_QARES,       864   /*the CBM selftest's scoreboard: ONE byte, the
                                * first failing case's number plus one.  It was
                                * a bit per case in a data register until there
                                * were more than thirty-two cases -- `bset
                                * Dn,Dm' takes the bit number modulo 32, so
                                * case 32 quietly shared case 0's bit, and
                                * since bset only ever sets, a late case could
                                * fail into a bit an early one had already
                                * passed into and the failure did not show at
                                * all -- and then a BYTE per case, in sixty-four
                                * of them, which ran off its end into
                                * CBM_DK_REL at 928 as soon as there were
                                * sixty-five.  The summary only ever wanted the
                                * first failure. */

/* The disk BASIC talks to, which is a different thing from the disk GEOS is
 * using.  A C64 does not share a filesystem layer with the drive: it says OPEN
 * and the drive keeps the buffer, the position in the file and the error that
 * happened last, and hands back one byte at a time.  So BASIC gets its own copy
 * of all of that -- and it gets it HERE, in the kernal's disk buffers, because
 * that is what they are.
 *
 * TWO sector buffers, and a channel claims one.  That is a 1541 with two
 * buffers instead of five, and it answers the same way when they are gone:
 * `70, NO CHANNEL'.  Two is what a copy needs -- one file being read and one
 * being written -- and it is what is left once curDirHead is spoken for.
 *
 * curDirHead is NOT one of them, and that is the whole reason this list is
 * written down: it is the BAM, and st_alloc_block reloads it from the disk
 * under whoever is holding it.  A name kept there is a name that has become
 * something else by the time the file is created.
 *
 * They were put above CBM_END first, on the strength of nothing in this file
 * naming an address up there.  Nothing does, and it is used all the same: a QA
 * capture has 987 non-zero bytes in $F728..$FBFF, and writing over them cost
 * the geoBASIC startup dialog a 48x18 block of its own artwork.  An address is
 * not free because the map does not mention it. */
.equ CBM_DK_BUF0,        diskBlkBuf   /*256: a sector, for whoever claimed it */
.equ CBM_DK_BUF1,        fileHeader   /*256: and the other one */
.equ CBM_DK_STATE,       fileTrScTab  /*256: the channels and everything else */
.equ CBM_DK_NAME,        (CBM_DK_STATE+0)    /*24: the name OPEN or LOAD parsed */
.equ CBM_DK_LINE,        (CBM_DK_STATE+24)   /*40: the directory line being sent */
.equ CBM_DK_OWNER0,      (CBM_DK_STATE+64)   /*byte: which channel has BUF0, $FF none */
.equ CBM_DK_OWNER1,      (CBM_DK_STATE+65)
.equ CBM_DK_LOWNER,      (CBM_DK_STATE+66)   /*byte: and which has the line */
.equ CBM_DK_STATUS,      (CBM_DK_STATE+67)   /*byte: ST, the KERNAL's own */
.equ CBM_DK_ERRNO,       (CBM_DK_STATE+68)   /*byte: the drive's last error */
.equ CBM_DK_ERRTR,       (CBM_DK_STATE+69)   /*byte: and the track it was on */
.equ CBM_DK_ERRSC,       (CBM_DK_STATE+70)   /*byte: and the sector */
.equ CBM_DK_B0TR,        (CBM_DK_STATE+71)   /*byte: which sector BUF0 holds, 0 none */
.equ CBM_DK_B0SC,        (CBM_DK_STATE+72)
.equ CBM_DK_B1TR,        (CBM_DK_STATE+73)
.equ CBM_DK_B1SC,        (CBM_DK_STATE+74)
.equ CBM_DK_WCHAN,       (CBM_DK_STATE+75)   /*byte: which channel is writing, $FF
                                              * none.  One, because the drive has
                                              * one place to build a sector in */
.equ CBM_DK_NTYPE,       (CBM_DK_STATE+76)   /*byte: the type the name asked for,
                                              * out of its `,S' or `,P' */
.equ CBM_DK_NDIR,        (CBM_DK_STATE+77)   /*byte: and `R' or `W' out of the
                                              * comma after that */
.equ CBM_DK_NRL,         (CBM_DK_STATE+78)   /*byte: and the record length after
                                              * a `,L', which is a byte and not
                                              * digits -- `"X,L,"+CHR$(10)' */
.equ CBM_DK_CHAN,        (CBM_DK_STATE+80)   /*eleven channels of CBM_DK_CHSZ: the
                                              * ten BASIC can open, and one LOAD
                                              * keeps to itself */
.equ CBM_DK_CHSZ,        16
/* One channel: what kind it is, which buffer it has, and where in the file it
 * has got to. */
.equ CBM_DKC_MODE,       0     /*byte: 0 none, 1 reading a file, 2 the directory,
                                * 3 the command channel, 4 writing a new file,
                                * 5 a buffer of its own, 6 adding to a file */
.equ CBM_DKC_STATUS,     1     /*byte: $40 once the last byte has been given */
.equ CBM_DKC_DRIVE,      2     /*byte: which storage drive it is on */
.equ CBM_DKC_BUF,        3     /*byte: 0 or 1, or $FF for none */
.equ CBM_DKC_TRACK,      4     /*byte: the sector its buffer is for */
.equ CBM_DKC_SECTOR,     5
.equ CBM_DKC_POS,        6     /*byte: how far into that sector, 2..255 */
.equ CBM_DKC_DTRACK,     7     /*byte: the directory sector being listed -- or,
                                * on a channel being written, the first sector of
                                * the chain, which the directory entry wants */
.equ CBM_DKC_DSECTOR,    8
.equ CBM_DKC_DSLOT,      9     /*byte: which of its entries comes next */
.equ CBM_DKC_DSTATE,     10    /*byte: 0 the header line, 1 the entries, 2 the
                                * blocks-free line, 3 the two null bytes, 4 done */
.equ CBM_DKC_LPOS,       11    /*byte: how far into the generated line */
.equ CBM_DKC_LLEN,       12    /*byte: and how long it is */
/* A file being ADDED to already has a directory entry, so those three carry
 * where it is instead -- the block the entry is in, the sector, and the slot.
 * A channel is never listing a directory and appending to a file at once. */
.equ CBM_DKC_EBLK,       10
.equ CBM_DKC_ESEC,       11
.equ CBM_DKC_ESLOT,      12
.equ CBM_DKC_SA,         13    /*byte: the secondary address it was opened on,
                                * which is how a block command names it */
.equ CBM_DKC_WBLK,       14    /*word: how many blocks written so far */

/* The one relative file that can be open, and everything a 1541 keeps about it
 * in its own RAM.  It is HERE, at the top of CBM_SCRATCH, and not beside the
 * rest of the drive's state, because CBM_DK_STATE is exactly full: twenty-four
 * bytes of name, forty of line, sixteen of flags and eleven channels of sixteen
 * come to two hundred and fifty-six with nothing over.  A record has no room
 * for a record length, a record number and an offset into it either.
 *
 * One, because a relative file needs a data block in hand and a side sector to
 * find the next one, and this drive has two buffers.  A second is `70, NO
 * CHANNEL', which is what a 1541 says when its buffers are gone. */
.equ CBM_DK_REL,         (CBM_SCRATCH+928)   /*20 bytes; 96 are free up there */
.equ CBM_RL_CHAN,        (CBM_DK_REL+0)   /*byte: which channel, $FF none */
.equ CBM_RL_LEN,         (CBM_DK_REL+1)   /*byte: the record length, 1..254 */
.equ CBM_RL_REC,         (CBM_DK_REL+2)   /*word: the record it stands on, from 1 */
.equ CBM_RL_OFF,         (CBM_DK_REL+4)   /*byte: how far into that record */
.equ CBM_RL_USED,        (CBM_DK_REL+5)   /*byte: and how much of it is not zero,
                                           * which is where EOI comes */
.equ CBM_RL_SSTR,        (CBM_DK_REL+6)   /*byte: the first side sector */
.equ CBM_RL_SSSC,        (CBM_DK_REL+7)
.equ CBM_RL_BLK,         (CBM_DK_REL+8)   /*word: which data block is in the
                                           * buffer, $FFFF none */
.equ CBM_RL_NBLK,        (CBM_DK_REL+10)  /*word: how many data blocks it has */
.equ CBM_RL_DIRTY,       (CBM_DK_REL+12)  /*byte: the buffer has been written to */
.equ CBM_RL_FILL,        (CBM_DK_REL+13)  /*byte: this record is being written, so
                                           * the rest of it has been zeroed */
.equ CBM_RL_NSS,         (CBM_DK_REL+14)  /*word: how many side sectors */
/* And the drive's own memory, as `M-R' sees it.  A 1541 has two kilobytes of
 * RAM and sixteen of ROM; this drive has the parts of that RAM it really has --
 * the sector buffers, the command buffer, the job queue and the header table --
 * and the ROM verbatim.  Nothing is stored for it: every address is answered
 * from the state that already exists, the way the directory listing is. */
.equ CBM_MEM_BUF0,       0x0300  /*buffer 0, which is CBM_DK_BUF0 */
.equ CBM_MEM_BUF1,       0x0400  /*and buffer 1 */
.equ CBM_MEM_CMD,        0x0200  /*the command buffer: the last command sent */
.equ CBM_MEM_JOBQ,       0x0000  /*six job codes, one per buffer */
.equ CBM_MEM_HDR,        0x0006  /*and the track and sector each one holds */
.equ CBM_MEM_TRACK,      0x0022  /*the track the head is over */
.equ CBM_MEM_LISTEN,     0x0077  /*$20 plus the device number */
.equ CBM_MEM_TALK,       0x0078  /*and $40 plus it */
.equ CBM_MEM_ROM,        0xC000  /*the DOS ROM, all sixteen kilobytes of it */

.equ CBM_RL_SSTAB,       (CBM_DK_REL+16)  /*12: the six side-sector addresses,
                                           * held out of the buffer while the
                                           * chain grows -- every side sector
                                           * carries the whole table, so adding
                                           * one rewrites all of them */
/* Where its directory entry is, and the block count that goes in it, are the
 * channel's own CBM_DKC_EBLK/ESEC/ESLOT and CBM_DKC_WBLK: a relative file has an
 * entry to patch on CLOSE for exactly the reason an appended file has. */
.equ CBM_RL_DATA,        254   /*the bytes of a block a record can be in */
.equ CBM_RL_SSPTR,       120   /*and the data blocks one side sector points at */

/* Canonical TopDesk overlay model, from 64_en Include/DeskMain2.s:998-1006.
 * TopDesk is a VLIR program: its non-resident modules are read into ModStart,
 * which is also DASpace, the desk-accessory load address.  Four per-window
 * file tables of sixteen entries sit directly beneath it. */
.equ MOD_START,          0x6000
.equ DA_SPACE,           MOD_START
.equ MOD_SIZE,           0x2000          /*GetModule ReadRecord r2 = $2000 */
.equ FILE_TAB_ENTRIES,   16
.equ FILE_TAB_SIZE,      (FILE_TAB_ENTRIES*18 + FILE_TAB_ENTRIES*64)  /* 1312 */
.equ FILE_TAB1,          (MOD_START - 4*FILE_TAB_SIZE)   /* $4B80 */
.equ FILE_TAB2,          (FILE_TAB1 + FILE_TAB_SIZE)
.equ FILE_TAB3,          (FILE_TAB2 + FILE_TAB_SIZE)
.equ FILE_TAB4,          (FILE_TAB3 + FILE_TAB_SIZE)
/* Module entry stride.  The 6502 original used a 3-byte JMP abs table; the
 * 68000 equivalent is a 4-byte bra.w, which is fixed-width -- a bare `jmp`
 * assembles as either 4 or 6 bytes depending on the operand, so it cannot be
 * used for a strided table. */
.equ MOD_ENTRY_SIZE,     4
.equ PRINTBASE,          0x7900
.equ OS_VARS,            0x8000
.equ DISK_BASE,          0x9000
/* $9000..$91BF is the C64's disk-driver window and this port's driver is in
 * cartridge ROM, so the 448 bytes are free -- see GEN_M4_SWAP_IN_RAM below,
 * which is why the accessory swap already lives at the top of it.  The XMODEM
 * peer's state is the first sixteen.
 *
 * It used to be at logical $5A10, "beside the UART tap in the QA-only window at
 * $FF5900", with its own comment admitting the hazard: "safe here even though an
 * application's image reaches this far up".  It was not safe.  TopDesk's .bss
 * ends at $5B50 and grew over it, and the failure is not a crash -- the upload
 * runs, 175 packets arrive, and the peer reports every block number out of
 * sequence because its expected-block byte is one of TopDesk's variables.  Any
 * future growth of any application would have done the same.  Nothing that has
 * to survive an application belongs in APP_RAM. */
.equ GEN_XM_STATE,       0x9000      /*byte: the peer's state */
.equ GEN_XM_BLK,         0x9001      /*byte: block number it expects next */
.equ GEN_XM_IDX,         0x9002      /*byte: index into the packet in flight */
.equ GEN_XM_SUM,         0x9003      /*byte: running checksum over its data */
.equ GEN_XM_WAIT,        0x9004      /*word: frames since the last prod */
.equ GEN_XM_PKTS,        0x9006      /*word: packets taken whole */
.equ GEN_XM_TEST,        0x9008      /*word: verdict bits */
.equ GEN_XM_SIDX,        0x900A      /*byte: index into the packet being sent */
.equ GEN_XM_SBLK,        0x900B      /*byte: block number being sent */
.equ GEN_XM_SPKTS,       0x900C      /*word: packets sent and acknowledged */
.equ GEN_XM_SENDER,      0x900E      /*byte: lets the peer take the sending role */
/* The pointer bitmap this machine currently has in VRAM.  ProcessMouse hands
 * msePicPtr to DrawSprite on every pass (mouse2.s), which on a C64 is one store
 * to the VIC's sprite pointer and costs nothing to repeat; here it is a VRAM
 * upload, so the address is remembered and the upload skipped when it has not
 * moved.  The observable contract is the same one: an application changes the
 * pointer by writing msePicPtr and ProcessMouse picks it up. */
.equ GEN_M3_POINTER_PIC_LIVE, 0x9010 /*word: msePicPtr as last uploaded */
.equ GEN_M3_POINTER_UPLOADS,  0x9012 /*word: times the copy was actually made */
/* The storage selftest's own scratch, kept out of APP_RAM for the same reason
 * everything else in this window is: fileSize has to be carried across
 * _WriteRecord and _DeleteRecord, both of which walk the disk in d5/d6/d7. */
.equ GEN_M3S_SIZE_MARK,       0x9014 /*word: fileSize as the case last read it */
.equ SCREEN_BASE,        0xA000

.equ SCREEN_SIZE,        8000
.equ OS_ROM,             0xC000
.equ OS_JUMPTAB,         0xC100

/* Public GEOS variables, exact original byte addresses. */
.equ appMain,            0x849B
.equ intTopVector,       0x849D
.equ intBotVector,       0x849F
.equ mouseVector,        0x84A1
.equ keyVector,          0x84A3
.equ inputVector,        0x84A5
.equ mouseFaultVec,      0x84A7
.equ otherPressVec,      0x84A9
.equ StringFaultVec,     0x84AB
.equ alarmTmtVector,     0x84AD
.equ BRKVector,          0x84AF
.equ RecoverVector,      0x84B1
/* $84B3 and not $04B3.  geossym.inc: `selectionFlash = $84b3 ; menu, icon'.
 * The 8 was missing, so this named a byte 32 KB below the GEOS variable it is,
 * in the C64 low-RAM shadow, and the first routine ever to read it got 117 out
 * of whatever was living there -- a menu item that flashed for four seconds.
 * It is not alone: tools/verify_geos_map.py now looks for the whole class. */
.equ selectionFlash,     0x84B3
.equ alphaFlag,          0x84B4
.equ iconSelFlg,         0x84B5
.equ faultData,          0x84B6
.equ menuNumber,         0x84B7
.equ mouseTop,           0x84B8
.equ mouseBottom,        0x84B9
.equ mouseLeft,          0x84BA
.equ mouseRight,         0x84BC
.equ stringX,            0x84BE
.equ stringY,            0x84C0
.equ mousePicData,       0x84C1
.equ maxMouseSpeed,      0x8501
.equ minMouseSpeed,      0x8502
.equ mouseAccel,         0x8503
.equ keyData,            0x8504
.equ mouseData,          0x8505
.equ inputData,          0x8506
.equ mouseSpeed,         0x8507
.equ random,             0x850A
.equ saveFontTab,        0x850C
.equ dblClickCount,      0x8515
/* Three bytes of the kernal's own boot header at $C000, not of the variable
 * area: header.s declares `dateCopy' between the C128 flag and the reset
 * vector, and DoUpdateTime copies year/month/day into it on every pass of
 * MainLoop.  It is where GEOS publishes the date -- start64.s reads it back at
 * boot -- and the canonical mirror this port installs already carries Berkeley
 * Softworks' own 88/4/20 there.  Computed from header.s and confirmed against
 * assets/geos64-bsw-kernal.bin, where `GEOS BOOT' sits at $C006. */
.equ dateCopy,           0xC018 /*3: year, month, day */
.equ year,               0x8516
.equ month,              0x8517
.equ day,                0x8518
.equ hour,               0x8519
.equ minutes,            0x851A
.equ seconds,            0x851B
.equ alarmSetFlag,       0x851C
.equ sysDBData,          0x851D
.equ screencolors,       0x851E
.equ dlgBoxRamBuf,       0x851F

/* Internal variables from kernal/vars/vars.s at original VARS=$86C0. */
.equ menuOptNumber,      0x86C0
.equ menuTop,            0x86C1
.equ menuBottom,         0x86C2
.equ menuLeft,           0x86C3
.equ menuRight,          0x86C5
.equ menuStackL,         0x86C7
.equ menuStackH,         0x86CB
.equ menuOptionTab,      0x86CF
.equ menuLimitTabL,      0x86D3
.equ menuLimitTabH,      0x86E2
.equ TimersTab,          0x86F1
.equ TimersCMDs,         0x8719
.equ TimersRtns,         0x872D
.equ TimersVals,         0x8755
.equ NumTimers,          0x877D
.equ DelaySP,            0x877E
.equ DelayValL,          0x877F
.equ DelayValH,          0x8793
.equ DelayRtnsL,         0x87A7
.equ DelayRtnsH,         0x87BB
.equ stringLen,          0x87CF
.equ stringMaxLen,       0x87D0
.equ tmpKeyVector,       0x87D1
.equ stringMargCtrl,     0x87D3
.equ GraphPenX,          0x87D4
.equ GraphPenY,          0x87D6
.equ KbdQueHead,         0x87D7
.equ KbdQueTail,         0x87D8
.equ KbdQueFlag,         0x87D9
.equ KbdQueue,           0x87DA
.equ KbdNextKey,         0x87EA
.equ KbdDBncTab,         0x87EB
.equ KbdDMltTab,         0x87F3
.equ PrvCharWidth,       0x8807
.equ clkBoxTemp,         0x8808
.equ clkBoxTemp2,        0x8809
.equ alarmWarnFlag,      0x880A
.equ tempIRQAcc,         0x880B
.equ defIconTab,         0x880C
/* QA-only: the Commodore float selftest.  One byte of pass bits and ten bytes
 * of text for the first failing case -- text, because what FOUT prints is the
 * calculator's display.  The buffer a case is printed into is on the selftest's
 * own stack; it needs no address at all.
 *
 * Both addresses were found the hard way.  $0F60 looked free -- every neighbour
 * here is written as an a6 offset and nothing claimed $0F60..$0FE1 -- but it is
 * the callback-thunk region, declared as $8F60 rather than $0F60, so neither a
 * gap scan nor verify_geos_map_overlap.py saw the collision and the run came
 * back reading JMP opcodes as text.  $080E and $0810..$081F then passed the
 * auditor and were written at run time anyway by something this file does not
 * declare, and $0820..$084F comes back zeroed.  What survives a long run is
 * measured, not declared: $080F holds, and so does the M1 debug buffer.  The
 * auditor sees none of this, which is a real hole in it.
 */

.equ DeskAccPC,          0x8850
.equ DeskAccSP,          0x8852
.equ dlgBoxCallerPC,     0x8853
.equ dlgBoxCallerSP,     0x8855
.equ DBGFilesFound,      0x8856
.equ DBGFOffsLeft,       0x8857
.equ DBGFOffsTop,        0x8858
.equ DBGFNameTable,      0x8859
.equ DBGFTableIndex,     0x885B
.equ DBGFileSelected,    0x885C
.equ A885D,              0x885D
.equ A885E,              0x885E
.equ A885F,              0x885F
.equ RecordDirTS,        0x8861
.equ RecordDirOffs,      0x8863
.equ RecordTableTS,      0x8865
.equ verifyFlag,         0x8867
.equ TempCurDrive,       0x8868
.equ scr_mobx,           0x8869
/* Software clock-alarm target (the CIA TOD alarm registers have no Genesis
 * equivalent, so the armed time lives in RAM).  Armed by geos_set_alarm, which
 * also sets bit 7 of alarmSetFlag; _GeosTickTime fires alarmTmtVector (or the
 * default DoClockAlarm) when the software clock reaches this h:m:s. */
.equ GEN_ALARM_HOUR,     0x8880
.equ GEN_ALARM_MIN,      0x8881
.equ GEN_ALARM_SEC,      0x8882
/* The CIA's alarm bit, which this machine has not got.  time1.s reads it --
 * `MoveB cia1base+13, r1L / ... / and #ALARMMASK' -- to learn that the TOD
 * alarm has just gone off, and everything downstream of that is the same on
 * both machines.  The software clock sets this instead, and _DoUpdateTime
 * consumes it exactly where upstream consumes the register. */
.equ GEN_ALARM_FIRED,    0x8883      /*byte: the tick saw the armed h:m:s */
.equ GEN_ALARM_TEST,     0x8884      /*QA-only: clock-alarm selftest result word */
/* Virtual Composite Drive routing state (the only part that must persist; the
 * header and merged directory are synthesised on demand into the read buffer, so
 * there is no clobber-prone scratch).  MAP[slot] = the backing ROM drive that
 * merged slot came from; CUR latches it for the file currently being opened. */
/* $8886..$888C, and it had three other variables living inside it.  The map is
 * ST_DIR_SLOTS bytes wide and the equates were written one at a time, so
 * GEN_RS232_GEN ($8887), GEN_SERIAL_PEER ($8888..$888B) and GEN_M3_CBM ($888C)
 * were all laid down in the middle of it, and every rebuild of the composite
 * directory wrote seven backing-drive numbers straight over them.
 *
 * GEN_SERIAL_PEER is a routine POINTER the VBlank calls through -- ext_uart_peer
 * (src/genesis/ext_serial.S) is `tst.l / beq / jsr (a0)' -- so the composite
 * directory was arming a jump to whatever slot bytes it happened to write.  The
 * QA boot crashed with ILLG at PC $0D10BCFE, which is `0D 0E 0E 0E': slots 13
 * and 14 of the merged directory, read as an address.  It looked like an
 * address-sensitive fault -- two more bytes anywhere in mouse.S would move the
 * VBlank enough to poll the port after the rebuild instead of before -- and it
 * was a map collision the whole time.
 *
 * tools/verify_geos_map.py now checks every GEN_ equate against every other. */
.equ GEN_COMPOSITE_MAP,  0x8886      /*ST_DIR_SLOTS (7) bytes */
.equ GEN_COMPOSITE_CUR,  0x888D      /*byte: backing drive of the open file (0 = none) */
/* Cached write-protect state of the writable SRAM volume (loaded from disk
 * header offset 40 when the volume is opened; 0 = writable, non-zero = protected).
 * st_block_write_idx rejects SRAM writes with ST_WR_PR_ON while set. */
.equ GEN_SRAM_PROTECTED,  0x886A
.equ GEN_PROT_TEST,       0x886C      /*QA-only: disk-protection selftest result word */
.equ GEN_PRINT_FULL_CNT,  0x886E      /*QA-only: byte count of a full-screen raster frame */
.equ GEN_PRINT_JOBS,      0x8870      /*word: completed screen-print jobs (telemetry) */
.equ GEN_DRIVEC_TEST,     0x0872      /*QA-only: drive C selftest result word */
.equ GEN_DRIVED_TEST,     0x0874      /*QA-only: drive D backing-volume result word */
/* Auto-exec boot: DONE is a re-entry guard (set before launching the startup
 * app, checked on every TopDesk init; it lives in the a6 OS region so it
 * survives the app loading over TopDesk at $1000).  RAN is a telemetry marker
 * the startup app sets to prove it executed. */
.equ GEN_AUTOEXEC_DONE,   0x8876      /*byte: startup app already launched */
.equ GEN_AUTOEXEC_RAN,    0x8877      /*byte: startup app's proof-of-run marker */
.equ GEN_COMPOSITE_TEST,  0x0878      /*QA-only: virtual composite drive selftest word */
.equ GEN_COMPOSITE_RT,    0x087A      /*QA-only: composite file-open routing result word */
.equ GEN_SERIAL_TEST,     0x887C      /*QA-only: EXT-UART receive selftest result word */
.equ GEN_RS232_TEST,      0x887E      /*QA-only: RS-232 driver selftest result word */
/* A peer on the other end of the serial line.  When this vector is non-zero the
 * transport calls it twice: once per byte transmitted (D1 = 1, D0 = the byte)
 * and once per frame from the receive poll (D1 = 0), and the peer answers by
 * pushing into the RX ring.  Zero in the release ROM, where it costs one
 * compare on the transmit path; the emulator has nothing plugged into the EXT
 * port, so without it the modem half of the terminal cannot be exercised at
 * all -- a transfer sends its packet into a void and waits forever for an ACK
 * nobody is there to send.
 *
 * The vector lives here, in audited OS RAM, and not beside the UART tap in the
 * application region: an application is free to have data at that address, and
 * a release boot would then find a non-zero "peer" and jump into it.  The
 * peer's own state can live down there, because nothing reads it unless this
 * vector has been armed, and only a QA boot arms it. */
/* Bumped every time the RS-232 port is opened.  The on-screen keyboard opens
 * the port when nothing else has it and closes it again on the way out, and a
 * boolean "I own it" is not enough to make that safe.  That is no longer how
 * the keyboard works -- it does not touch the port at all now -- but the
 * generation counter stays, because an accessory that opens the port has the
 * same problem an application does. */
.equ GEN_RS232_GEN,       0x8872      /*byte: incremented by every open */
.equ GEN_SERIAL_PEER,     0x8878      /*long: peer routine, 0 = nothing plugged in */
/* The on-screen keyboard's Commodore key: armed by pressing C=, spent on the
 * next key, and a one-shot rather than a toggle like SHIFT -- a sticky C=
 * would make every key after it a command.  It has the byte the keyboard's own
 * serial-port generation used to need, which it does not any more. */
.equ GEN_M3_CBM,          0x8873      /*byte: the next key gets bit 7 */
/* EXT-port UART receive ring (the modem/terminal transport's RX half; the
 * printer path is TX-only).  64-byte power-of-two ring, one slot reserved to
 * distinguish full from empty. */
/* QA-only: one bit per float-selftest case.  Next to GEN_ALARM_TEST because
 * that one is demonstrably readable after a boot; $080F was not -- writing to
 * it and reading it back gave a different value, and the constant it holds was
 * mistaken for a result for a whole session. */
.equ GEN_FP_TEST,        0x888E      /*word */

.equ GEN_UART_RX_HEAD,    0x8890      /*byte: next write index */
.equ GEN_UART_RX_TAIL,    0x8891      /*byte: next read index */
.equ GEN_UART_RX_BUF,     0x8892      /*64 bytes -> 0x08D1 */
/* RS-232 driver open flag: when nonzero the runtime loop drains the UART into
 * the RX ring every pass, so a terminal sees incoming bytes without polling the
 * hardware itself.  Cleared (port closed) by default -> zero mainloop cost. */
.equ GEN_RS232_OPEN,      0x88D2      /*byte: 1 = port open */
/* When the RS-232 port is open, HBlank interrupts drain the UART far faster than
 * once per frame (the single-byte hardware buffer would otherwise overrun at
 * 4800 baud).  HBlank wakes the foreground loop too, so the loop only advances a
 * step when VBlank set the frame tick -- otherwise it would run per scanline. */
.equ GEN_FRAME_TICK,      0x88D3      /*byte: VBlank sets, runtime step clears */
.equ GEN_HINT_COUNT,      0x88D4      /*word: HBlank services (telemetry/proof) */
.equ GEN_CONIO_TEST,      0x88D6      /*QA-only: PutChar control-code selftest result word */
.equ GEN_WIDEFONT_TEST,   0x88D8      /*QA-only: wide-glyph (>8px) render selftest result word */
.equ GEN_CONIO2_TEST,     0x88DA      /*QA-only: margin/fault + ESC_GRAPHICS/RULER selftest word */
.equ GEN_D64_TEST,        0x88DC      /*QA-only: 1541 .d64 CBM mount selftest result word */
.equ GEN_D64_RT,          0x88DE      /*QA-only: .d64 file-load routing result word */
/* CBM directory-chain iteration cursor (drive 13): the linked walk tracks a
 * track/sector pair; the per-sector slot reuses GEN_M3S_ITER_SLOT. */
.equ GEN_D64_ITER_TRACK,  0x88E0      /*byte: current directory sector track */
.equ GEN_D64_ITER_SECTOR, 0x88E1      /*byte: current directory sector */
.equ GEN_D81_TEST,        0x88E2      /*QA-only: 1581 .d81 CBM mount selftest result word */

/* Sega Mega Mouse (controller-port pointing device) driver state. */
.equ GEN_MOUSE_PRESENT,   0x88E4      /*byte: 1 when a Mega Mouse is detected (probed at boot and re-probed live) */
.equ GEN_MOUSE_BTN,       0x88E5      /*byte: last button nibble (b0=L b1=R b2=Mid b3=Start) */
.equ GEN_MOUSE_DX,        0x88E6      /*byte (signed): last X delta, telemetry/tests */
.equ GEN_MOUSE_DY,        0x88E7      /*byte (signed): last Y delta, telemetry/tests */
.equ GEN_MOUSE_SIG,       0x88E8      /*word: captured ID nibbles from the last transfer */
.equ GEN_CBM_COMP_TEST,   0x88EA      /*QA-only: CBM-in-composite browse/open selftest word */
.equ GEN_BOLDFONT_TEST,   0x88EC      /*QA-only: BOLD glyph render selftest result word */
/* $0F60..$0FE1 is the callback-thunk block -- seventeen six-byte JMPs the
 * kernal builds so a logical vector can reach native code (GEN_M3_TRAMP_*,
 * GEN_M2UI_TRAMP_*, below).  A GEN_ variable must never be put in it.  These
 * words were, wedged into the sixteen-byte hole between GEN_MV_TRAMP_MOUSE and
 * GEN_M2UI_TRAMP_MENU_MOUSE, and the moment one more word was added it landed
 * on the menu trampoline's JMP opcode: geoWrite wrote its page number over it
 * at startup and every press in the machine stopped being dispatched.  They
 * live out here now, and verify_geos_map_overlap.py rejects the range. */
.equ GEN_REALFONT_TEST,   0x922A /*word: QA-only: real GEOS font selftest result */
/* geoWrite shell (apps/geowrite.S). */
.equ GEN_GW_ENTRIES,      0x922C /*word: times the shell was entered */
.equ GEN_GW_ACTIONS,      0x922E /*word: menu items picked */
.equ GEN_GW_QUITS,        0x9230 /*word: times it handed the machine back */
.equ GEN_GW_KEYS,         0x9232 /*word: keystrokes the document took */
.equ GEN_GW_CLICKS,       0x9234 /*word: presses that moved the caret */
.equ GEN_GW_PAGE,         0x9236 /*word: the page on screen, one-based */
.equ GEN_GW_LINES,        0x9238 /*word: lines in the line table after a layout */
.equ GEN_GW_WRAPS,        0x923A /*word: of those, ones break_line ended at a
                                  * space rather than at a $0D -- word wrap */
.equ GEN_GW_CARET,        0x923C /*word: the insertion point, as a byte offset
                                  * into the page being edited */
.equ GEN_GW_SEL_LO,       0x923E /*word: textRangeLo, the selection's low end */
.equ GEN_GW_SEL_HI,       0x9240 /*word: textRangeHi -- equal means none */
.equ GEN_GW_DRAGS,        0x9242 /*word: drag steps that moved the selection */
.equ GEN_GW_DBLCLICKS,    0x9244 /*word: second presses that selected a word */
.equ GEN_GW_PANE,         0x9246 /*word: $2FBC, which third of the page is shown */
.equ GEN_GW_CARET_X,      0x9248 /*word: the insertion point's x, in the page */
.equ GEN_GW_SCRAP,        0x924A /*word: $41E4, the text scrap's length */
.equ GEN_GW_CMDS,         0x924C /*word: keys the command table at $3487 took */
.equ GEN_GW_CMD,          0x924E /*byte: the index in it the last one matched --
                                  * 0..33, and the only thing that says WHICH
                                  * command a keypress turned into */
.equ GEN_GW_BURST,        0x924F /*byte: the most characters one kb_collect ever
                                  * gathered.  Two or more is the whole claim of
                                  * buffered typing: that many keys became one
                                  * insertion rather than one edit each */

/* Genesis-only nucleus bookkeeping. Kept outside original used variables. */
.equ GEN_M1_MAGIC,       0x88F0
.equ GEN_M1_LOGICAL_TICKS, 0x88F4
.equ GEN_M1_FRAME_COUNT, 0x88F8
.equ GEN_M1_TICK_ACCUM,  0x88FC
.equ GEN_M1_VIDEO_HZ,    0x88FE
.equ GEN_M1_SUBSECOND,   0x88FF
.equ GEN_M1_TEST_FLAGS,  0x8900 /*long */
.equ GEN_M1_LAST_TRAP,   0x8904 /*long */
.equ GEN_DBG_TEST2,       0x8908 /*word: QA-only: .dbg symbol selftest result */
/* Icon Editor (apps/iconedit.S). */
.equ GEN_IE_ENTRIES,      0x890C /*word: times the editor was entered */
.equ GEN_IE_BITS,         0x890E /*word: pixels currently set in the bitmap */
.equ GEN_IE_SAVEST,       0x8910 /*word: GEOS status the last save gave */
.equ GEN_IE_LOADST,       0x8912 /*word: and the last load */
.equ GEN_IE_QUITS,        0x8914 /*word: times it handed the machine back */
/* geoAssembler's 6502 target (src/kernal/asm/asm6502.S). */
.equ GEN_ASM65_TEST,      0x8916 /*word: QA-only: 6502 table/operand selftest */
.equ GEN_ASM65_TEST2,     0x8918 /*word: QA-only: 6502 encoder selftest */
/* An application launched off authentic CBM media through the Virtual Composite
 * Drive.  apps/cbmmark.S is TEST APP on assets/geos-cbm-disk.d64 -- a real GEOS
 * VLIR application whose record 0 is 68000 code -- and it stamps $C000 with the
 * drive it was loaded from in the low byte, so the marker says both that it ran
 * and where from.  Before it existed the fixture's record 0 was three bytes of
 * `bra.w' to itself followed by ASCII, so a launch through the composite could
 * be shown to ROUTE and never shown to RUN. */
.equ GEN_CBM_APP_RAN,     0x892E /*word: $C000 | curDrive */
.equ GEN_ASM65_TEST3,     0x891C /*word: QA-only: a whole 6502 source assembled */
.equ GEN_ASM65_BYTES,     0x891E /*word: QA-only: bytes that source produced */
/* Ten bytes.  The float selftest writes its failing case here too, by this
 * name rather than an alias of its own -- both are QA-only, neither runs while
 * the other does, and a second .equ on the same address is a collision as far
 * as verify_geos_map_overlap.py is concerned, correctly. */
.equ GEN_M1_DEBUG_BUFFER, 0x8920   /*9 bytes in unused nucleus WRAM */
/* The zero-page allocation counter for .zsect.  It lives here rather than beside
 * GEN_ASM_SECT because that band runs straight into the .include state; the gap
 * above GEN_M1_DEBUG_BUFFER is the nearest free aligned word. */
.equ GEN_ASM_ZLEN,        0x892A /*word: bytes handed out in .zsect (max $100) */
.equ GEN_ASM65_TEST4,     0x892C /*word: QA-only: .zsect selftest result */
.equ GEN_RU_ENTRIES,      0x092E /*word: times ruler1.5 was opened.  The
                                  * desk-accessory counter says one opened; this
                                  * says which one. */

/* Test/application scratch area. */
.equ M1_TEST_AREA,       0x2000
.equ M1_PROCESS_CODE,    0x2400
.equ M1_PROCESS_TABLE,   0x2500
.equ M1_INIT_TABLE,      0x2520

/* Genesis-only Milestone 2 graphics bookkeeping at logical $8930-$89FF. */
.equ GEN_M2_MAGIC,           0x8930 /*long */
.equ GEN_M2_TEST_FLAGS,      0x8934 /*long */
.equ GEN_M2_BITMAP_CRC,      0x8938 /*long */
.equ GEN_M2_PRESENT_COUNT,   0x893C /*long */
.equ GEN_M2_DIRTY_COUNT,     0x8940 /*word */
.equ GEN_M2_DIRTY_CURSOR,    0x8942 /*word */
/* geoDebugger's block sits in the free WRAM above GEN_M2_LAST_TILE and below
 * GEN_M2_DIRTY_BITS at $0980.
 * ---------------------------------------------------------------------------
 * geoDebugger: the exception monitor.
 *
 * The vector table is in cartridge ROM, so the debugger cannot patch a vector;
 * the group 2 handlers in start.S look here first instead and only panic when
 * nothing is armed.  Everything else is in a caller-provided frame, whose
 * address has to survive the exception -- which is why it lives here.
 * ------------------------------------------------------------------------- */
/* The four remaining geoProgrammer disk programs.  Their telemetry is split
 * across the last two free bands below $1000 -- $0946-$094F here and
 * $0B56-$0B5F above GEN_BASIC_VARS -- because no single band is big enough.
 * Both are now full; the next variable that wants a home has to make one. */
.equ GEN_SE_ENTRIES,     0x8946 /*word: times the Sector Editor was entered */
.equ GEN_SE_READST,      0x8948 /*word: GEOS status the last GetBlock gave */
.equ GEN_SE_WRITEST,     0x894A /*word: and the last PutBlock */
.equ GEN_SE_SUM,         0x894C /*word: sum of the block now on screen, so an
                                 * edit is provable without reading the screen */
.equ GEN_HE_ENTRIES,     0x894E /*word: times the Header Editor was entered */
.equ GEN_DBG_FRAME,      0x8950 /*long (abs): the register frame + breakpoints */
.equ GEN_DBG_RESUMESP,   0x8954 /*long: stack to come back on when one fires */
.equ GEN_DBG_RESUMEPC,   0x8958 /*long: where to come back to */
.equ GEN_DBG_ACTIVE,     0x895C /*word: 1 while a program is being watched */
.equ GEN_DBG_STATUS,     0x895E /*word: 0 ran to completion, 1 caught */
.equ GEN_DBG_VECNUM,     0x8960 /*word: the exception that fired */
.equ GEN_DBG_TEST,       0x8962 /*QA-only: debugger selftest result word */
.equ GEN_DBG_STEPS,      0x8964 /*word: QA-only: single steps taken */
/* The symbol table a .dbg file was read into.  The buffers are the caller's --
 * the engine holds no storage of its own -- but the three numbers describing
 * them have to outlive the GEOS calls that read the file. */
.equ GEN_DBG_SYMS,       0x897C /*word: symbols loaded (0 = none) */
.equ GEN_DBG_SYMTAB,     0x0CF8 /*long (abs): base of the loaded symbol table */
.equ GEN_DBG_SYMEND,     0x0CFC /*long (abs): first byte past it */

/* ---------------------------------------------------------------------------
 * geoProgrammer: the IDE shell (apps/geoprog.S).
 *
 * The shell is an ordinary application and keeps its own working storage in its
 * own window; what is here is only what a QA run outside the application has to
 * be able to read back -- whether it was entered, which startup option it took,
 * and how the last assemble ended.
 * ------------------------------------------------------------------------- */
.equ GEN_GP_ENTRIES,     0x8966 /*word: times the shell was entered */
.equ GEN_GP_OPTION,      0x8968 /*word: the startup box's answer */
.equ GEN_GP_LASTERR,     0x896A /*word: the last assembler error, 0 = clean */
.equ GEN_GP_QUITS,       0x896C /*word: times it handed the machine back */
.equ GEN_GP_OBJLEN,      0x896E /*word: bytes the last assemble produced */
.equ GEN_GP_LINKST,      0x8970 /*word: GEOS status the last link returned */
.equ GEN_GP_FOUND,       0x8972 /*word: FindFile on the linked name afterwards */
.equ GEN_GP_SAVEST,      0x8974 /*word: GEOS status the last document save gave */
.equ GEN_GP_LOADST,      0x8976 /*word: and the last document load */
.equ GEN_GP_LINES,       0x8978 /*word: source lines typed into the editor */
/* geoProgrammer's shell is geoAssembler or geoLinker depending on which of the
 * disk's three applications launched it: 0 the assembler, 1 the linker. */
.equ GEN_GP_TOOL,        0x897E /*word: which tool apps/geoprog.S came up as */
/* $097A is the last free word below GEN_M2_DIRTY_BITS, which starts at $0980
 * and runs 128 bytes.  Nothing else fits here. */
.equ GEN_LNK_VLIRST,     0x897A /*word: how the VLIR record write ended --
                                 * high byte the stage (1 open, 2 append,
                                 * 3 write, 4 close), low byte the GEOS status.
                                 * A multi-record write touches four different
                                 * parts of the file system and "it failed" on
                                 * its own says nothing about which. */

/* ---------------------------------------------------------------------------
 * geoDebugger's surface (apps/geodebug.S).  The monitor's own state lives in
 * the GEN_DBG_ block above; these are what a QA run outside the application
 * reads back to know the display showed the right thing.
 *
 * Placed at $0CE2 because that is where there is actually room: $097A looks
 * free but GEN_M2_DIRTY_BITS starts 6 bytes later and runs 128, and this block
 * would have silently overwritten the tile-dirty bitmap.  The band here is
 * bounded by GEN_ASM_TEST3 below and GEN_BASIC_CURREC at $0D00 above.
 * ------------------------------------------------------------------------- */
.equ GEN_GD_ENTRIES,     0x8CE2 /*word: times the debugger was entered */
.equ GEN_GD_CAUGHT,      0x8CE4 /*word: 1 the program was caught, 0 it ended */
.equ GEN_GD_VECTOR,      0x8CE6 /*word: the vector it was caught on */
.equ GEN_GD_QUITS,       0x8CE8 /*word: times it handed the machine back */
.equ GEN_GD_BPST,        0x8CEA /*word: what BP_SET said about the request */
.equ GEN_GD_PC,          0x8CEC /*long: the PC it stopped at */
.equ GEN_GD_BPADDR,      0x8CF0 /*long: the breakpoint the dialog asked for */
.equ GEN_GD_MEMADDR,     0x8CF4 /*long: the address the dump was pointed at */
.equ GEN_GD_SYMHIT,      0x097E /*word: 1 when the last lookup named something */
.equ GEN_GD_SYMS,        0x890A /*word: symbols the debugger has loaded */

/* One entry of a loaded symbol table: the name padded to twelve, then its
 * value.  Fixed stride so a lookup is a walk and not a parse. */
.equ DBG_SYM_SIZE,  16
.equ DBG_SYM_NAME,   0
.equ DBG_SYM_NAMEMAX,12
.equ DBG_SYM_VALUE, 12

/* The frame, all offsets from its base. */
.equ DBG_D0,      0   /*D0-D7 then A0-A6, in movem order */
.equ DBG_A0,     32
.equ DBG_A1,     36
.equ DBG_SP,     60   /*long: the stack the program had */
.equ DBG_SR,     64   /*word */
.equ DBG_PC,     66   /*long */
.equ DBG_VECTOR, 70   /*word: the exception vector number */
.equ DBG_BPTAB,  72   /*8 x [address(4) original(2) used(2)] */
.equ DBG_STKTOP,136   /*long (abs): the watched program's own stack top --
                       * it cannot share the monitor's, because the monitor
                       * pushes frames of its own while the program is stopped */
.equ DBG_BPMAX,   8
.equ DBG_BP_SIZE, 8
.equ DBG_BP_ADDR, 0
.equ DBG_BP_ORIG, 4
.equ DBG_BP_USED, 6
.equ DBG_FRAME_SIZE, 140

.equ GEN_M2_LAST_TILE,       0x8944 /*word */
.equ GEN_M2_DIRTY_BITS,      0x8980  /*128 bytes; 1000 bits used. */

/* Milestone 2 application/test data in ordinary GEOS RAM. */
.equ M2_TEST_AREA,           0x3000
.equ M2_ASSET_AREA,          0x3400

/* Genesis-only Milestone 3 input bookkeeping at logical $8A00-$8AFF. */
.equ GEN_M3_MAGIC,              0x8A00
.equ GEN_M3_TEST_FLAGS,         0x8A04
.equ GEN_M3_PAD_STATE,          0x8A08
.equ GEN_M3_PAD_PREV,           0x8A0A
.equ GEN_M3_PAD_EDGES,          0x8A0C
.equ GEN_M3_KEYBOARD_VISIBLE,   0x8A0E
.equ GEN_M3_KEY_ROW,            0x8A0F
.equ GEN_M3_KEY_COL,            0x8A10
.equ GEN_M3_SHIFT,              0x8A11
.equ GEN_M3_HOLD_COUNT,         0x8A12
.equ GEN_M3_MOUSE_EVENTS,       0x8A14
.equ GEN_M3_KEY_EVENTS,         0x8A18
.equ GEN_M3_INPUT_EVENTS,       0x8A1C
.equ GEN_M3_LAST_KEY,           0x8A20
.equ GEN_M3_LAST_MOUSE_DATA,    0x8A21
.equ GEN_M3_SIXBUTTON,          0x8A22
/* Whether bytes arriving on the EXT port count as keypresses.  It used to mean
 * "the keyboard is the reason the port is open", because raising the on-screen
 * keyboard opened the port with it -- which meant an EXT device nobody was
 * using could put characters into any application at any time, and there was no
 * way to say no.  Now nothing turns it on but the Serial Keyboard accessory. */
.equ GEN_M3_KBD_UART_ON,        0x8A23 /*byte */
.equ GEN_M3_WINDOW_UPDATES,     0x8A24
.equ GEN_M3_SPRITE_UPDATES,     0x8A28
.equ GEN_M3_VECTOR_MOUSE_COUNT, 0x8A2C
.equ GEN_M3_VECTOR_KEY_COUNT,   0x8A30
.equ GEN_M3_VECTOR_INPUT_COUNT, 0x8A34
.equ GEN_M3_LAST_DISPATCH_KEY,  0x8A38
.equ GEN_M3_LAST_DISPATCH_MOUSE,0x8A39
.equ GEN_M3_LAST_DISPATCH_INPUT,0x8A3A
.equ GEN_M3_LAST_DIR,           0x8A3B
.equ GEN_M3_ACCEL_ACC,          0x8A3C
.equ GEN_M3_FRAC_X,             0x8A3D
.equ GEN_M3_FRAC_Y,             0x8A3E
.equ GEN_M3_SPRITE_ENABLED,     0x8A3F
.equ GEN_M3_REPEAT_DELAY,       0x8A40
.equ GEN_M3_REPEAT_RATE,        0x8A41
.equ GEN_M3_LAST_SELECTED,      0x8A42
.equ GEN_M3_POINTER_LIGHT,       0x8A43 /*0 black, 1 white pointer palette. */
.equ GEN_M3_POINTER_STYLE_VEC,   0x8A44 /*App logical callback run before pointer SAT commit. */
.equ GEN_M3_SAVED_POINTER_STYLE_VEC, 0x8A46 /*Desk-accessory/application load preservation. */
/* GEOS application sprites 1-7 (sprite 0 is the pointer).  Parallel arrays
 * indexed by (sprite# - 1), in free Genesis scratch 0x0A48-0x0AFF. */
.equ GEN_M3_SPR_BMP,            0x8A48 /*7 x word: logical 63-byte bitmap ptr */
.equ GEN_M3_SPR_X,              0x8A56 /*7 x word: GEOS x position */
.equ GEN_M3_SPR_Y,              0x8A64 /*7 x byte: GEOS y position */
.equ GEN_M3_SPR_EN,             0x8A6B /*7 x byte: nonzero when enabled */
/* Panic error-dialog scratch (free Genesis scratch above the sprite arrays,
 * 0x0A72-0x0AFF).  Panic runs in a fault context and cannot use application RAM,
 * so its "System error near $XXXX" DoDlgBox table and the formatted message
 * string live here.  A5-relative logical addresses (for r0 / DBTXTSTR string
 * pointers) are the (%a6) offset + 0x8000. */
.equ GEN_PANIC_ADDR,            0x8A72 /*word: durable logical addr of the faulting gate */
.equ GEN_PANIC_TABLE,           0x8A74 /*DoDlgBox command table (~18 bytes) */
/* geoLinker's .rom output length.  A word, not a long, and deliberately: the
 * image is built in the application window, which is under 20 KB, so a length
 * that did not fit a word could not have been built in the first place.  It is
 * here rather than beside the other GEN_LNK_ variables because that band
 * ($0AE0-$0AFF) is full -- as, by now, is nearly everything below $1000.
 * Whoever needs the next variable will have to make room rather than find it. */
.equ GEN_LNK_ROMLEN,            0x8A86 /*word: bytes in the last .rom image */
.equ GEN_PANIC_STRING,          0x8A88 /*"System error near $XXXX" + NUL (24 bytes) */

/* Native callback thunks live in reserved OS RAM, never APP_RAM. */
.equ GEN_M3_NATIVE_TRAMP_BASE,  0x8F60
.equ GEN_M3_TRAMP_CHECK_CLICK,  0x8F60
.equ GEN_M3_TRAMP_MOUSE_FAULT,  0x8F66
.equ GEN_M3_TRAMP_DEMO_MOUSE,   0x8F6C
.equ GEN_M3_TRAMP_DEMO_INPUT,   0x8F72
.equ GEN_M3_TRAMP_DEMO_KEY,     0x8F78
.equ GEN_M3_TRAMP_OTHER_PRESS,  0x8F7E
/* The canonical move-frame pair, from TopDesk's own source: appMain does the
 * per-frame work and mouseVector catches the release.  See
 * src/kernal/drag/moveframe.S. */
.equ GEN_MV_TRAMP_APPMAIN,      0x8F84
.equ GEN_MV_TRAMP_MOUSE,        0x8F8A
/* DeskTopName.  EnterDeskTop puts r6 here and calls GetFile, so the string has
 * to live at a logical (a6-relative) address rather than in ROM.  Sixteen
 * bytes, the width of a GEOS directory name, of which "DESK TOP" uses nine.
 * $8F90..$8F9F, between the move-frame thunks and the M2UI ones. */
.equ M4_DESKTOP_NAME_AREA,      0x8F90
.equ M3_TEXT_AREA,              0x3C00

/* Original GEOS internal zero-page vectors used by menus/dialogs. */
.equ IconDescVec,        0x003F
.equ CallRLo,            0x0041
.equ CallRHi,            0x0042
.equ DBoxDesc,           0x0043
.equ Z45,                0x0045
.equ Z46,                0x0046
.equ Z47,                0x0047

/* Completed Milestone 2 UI bookkeeping, outside original occupied vars. */
.equ GEN_M2UI_MAGIC,              0x8B00
.equ GEN_M2UI_TEST_FLAGS,         0x8B04
.equ GEN_M2UI_ACTIVE_MODE,        0x8B08 /*0 none, 1 menu, 2 dialog, 3 string */
.equ GEN_M2UI_MENU_DESC,          0x0B0A
/* $8B0C held the mouseVector DoMenu displaced, and DoMenu displaces none now --
 * menu1.s:189 sets MENUON and calls StartMouseMode, and CheckClickPos finds the
 * bar by that bit and menuTop/Bottom/Left/Right.  It carries the other thing
 * menu1.s keeps in a processor flag instead: DoMenu1_1 is reached with C set
 * from _DoMenu and from DoMenu0 (menu3.s opening a sub-menu) and with C clear
 * from DoPrvMn1 (_DoPreviousMenu, _ReDoMenu), and @4 places the pointer inside
 * the menu only when it is set.  A 68000 cannot carry a flag across the drawing
 * -- there is a PutString and a Rectangle in between -- so the two entries write
 * this byte and .menu_activate reads it. */
.equ GEN_M2UI_MENU_WARP,          0x8B0C
.equ GEN_M2UI_MENU_SAVED_KEY,     0x8B0E
.equ GEN_M2UI_MENU_SELECTED,      0x8B10
.equ GEN_M2UI_MENU_COUNT,         0x8B11
.equ GEN_M2UI_DIALOG_RESULT,      0x8B12
.equ GEN_M2UI_DIALOG_BUTTONS,     0x0B13
.equ GEN_M2UI_DIALOG_SAVED_MOUSE, 0x8B14
.equ GEN_M2UI_DIALOG_SAVED_KEY,   0x8B16
.equ GEN_M2UI_DIALOG_SAVED_OTHER, 0x8B18
.equ GEN_M2UI_DIALOG_TOP,         0x8B1A
.equ GEN_M2UI_DIALOG_BOTTOM,      0x8B1B
.equ GEN_M2UI_DIALOG_LEFT,        0x8B1C
.equ GEN_M2UI_DIALOG_RIGHT,       0x8B1E
.equ GEN_M2UI_PROMPT_VISIBLE,     0x8B20
.equ GEN_M2UI_PROMPT_HEIGHT,      0x8B21
.equ GEN_M2UI_STRING_START_X,     0x8B22
.equ GEN_M2UI_STRING_COMPLETE,    0x8B24
.equ GEN_M2UI_LAST_ACTION,        0x8B25
.equ GEN_M2UI_GRAPHICS_CMDS,      0x8B26
.equ GEN_M2UI_MENU_ACTIONS,       0x8B28
.equ GEN_M2UI_DIALOG_ACTIONS,     0x8B2A
.equ GEN_M2UI_STRING_ACTIONS,     0x8B2C
.equ GEN_M2UI_ICON_ACTIONS,       0x8B2E
.equ GEN_M2UI_DEMO_STATE,         0x0B30
/* The APPLICATION'S press vector, for as long as a box is up.
 *
 * Upstream DoDlgBox never claims otherPressVec (dlgbox1a.s draws, parses and
 * calls DoIcons, and that is all), so at the moment CheckClickPos reaches its
 * last leg the slot still holds whatever the application put there -- or what
 * DBSYSOPV, DBOPVEC or DBGETFILES put there, which are the only three commands
 * that write it.  DialogSave copies appMain and the 38 bytes after it, vectors
 * included, so the application gets its own back when the box goes.
 *
 * This port's DoMenu does claim the slot, so a box with a menu inside it -- the
 * geoBASIC debugger's panel is one -- cannot read the application's vector out
 * of otherPressVec any more.  It reads it out of here instead, which is the
 * same value upstream would find, and which can never be a kernal handler
 * installed after the box opened. */
.equ GEN_M2UI_DIALOG_OPV,         0x8B30 /*word: the box's other-press vector */
.equ GEN_M2UI_DIALOG_SYSOPV,      0x8B32 /*byte: DBSYSOPV asked for the default */
.equ GEN_M2UI_DIALOG_SAVED_DISP,  0x8B34
/* DBGETFILES chooser state (original kernel vars DBGFOffsLeft/Top,
 * DBGFNameTable, DBGFilesFound, DBGFileSelected, DBGFTableIndex). */
.equ GEN_M2UI_DBGF_PRESENT,     0x8B35
.equ GEN_M2UI_DBGF_OFFS_LEFT,   0x8B36
.equ GEN_M2UI_DBGF_OFFS_TOP,    0x8B37
.equ GEN_M2UI_DBGF_NAME_TABLE,  0x8B38 /*word, little-endian logical */
.equ GEN_M2UI_DBGF_FOUND,       0x8B3A
.equ GEN_M2UI_DBGF_SELECTED,    0x8B3B
.equ GEN_M2UI_DBGF_INDEX,       0x8B3C
/* A DBUSRICON's service routine, one word per user icon in descriptor order --
 * the two bytes at the end of its icon record.  GEOS hands a user icon to
 * DoIcons like any other, so a click RUNS THE ROUTINE and the box stays up;
 * only RstrFrmDialogue ends it.  A zero here is an icon with no routine, which
 * ends the box with its own code, and that is what every application in this
 * tree that predates geoWrite's Search / Replace box relies on. */
/* --- The far side of the storage window ------------------------------------
 *
 * These all used to live at $1000..$1031, and every one of them was inside
 * ST_TMP_IDS -- 128 bytes at the same address that storage_constants.inc gives
 * the block allocator to hold the ids of a chain it is writing
 * (st_chain_write_new).  Nothing caught it: the geos_map overlap audit compares
 * GEN_ entries against each other and the storage window is not one of them.
 *
 * It stayed invisible while no chain was longer than eight blocks, because the
 * first eight ids stop just short of $1010.  geoWrite writing its document to
 * the disk as a real VLIR file is the first thing in this tree to allocate more
 * than that in one go, and the ids walked straight over the counters, the
 * cartridge bitmap base and the font base -- ascending block numbers where a
 * pointer should be.  They are out of that window now, and the audit rejects
 * the whole of it. */
.equ GEN_M2UI_DIALOG_USRVEC,    0x9F70 /*16 bytes: 8 words */
/* And a picture for each of the same eight regions, so the box can draw its
 * icons AFTER the descriptor has been walked instead of as it reaches them.
 *
 * That is when GEOS draws them.  DoDlgBox collects the buttons and the user
 * icons into an icon table as it parses and hands the whole table to DoIcons at
 * the end (dlgbox1a.s @7), so a DB_USR_ROUT that paints over the ground an icon
 * stands on paints UNDER it.  This port drew each icon where the parse met it,
 * and the Icon Editor's `Select File:' box -- $11CE, four buttons and two
 * scroll arrows and then the DB_USR_ROUT at $14FD that fills the list pane --
 * came up with both arrows erased by the pane.
 *
 * A zero is a region with no picture of its own: every system button, which
 * brings its own out of the kernal, and a DBUSRICON whose record carries a null
 * pointer because the application painted the picture itself.  Which of the two
 * it is comes from the region's CODE, not from the zero. */
.equ GEN_M2UI_DIALOG_USRPIC,    0x9016 /*16 bytes: 8 words */
/* geoWrite's Search / Replace counters.  They are out here rather than beside
 * the other GEN_GW_ words at $122C because that run ends at $124F and $1250 is
 * the Sonic accessory's. */
.equ GEN_GW_FOUND,              0x9F80 /*word: matches search_in_page found */
.equ GEN_GW_REPLACED,           0x9F82 /*word: and replacements do_replace made */
.equ GEN_GW_GRABS,              0x9F84 /*word: ruler markers picked up */
.equ GEN_GW_DROPS,              0x9F86 /*word: and put back down somewhere */
.equ GEN_GW_FLIPS,              0x9F88 /*word: tabs turned decimal on the way */
.equ GEN_GW_SCROLLS,            0x9F8A /*word: times the window moved down a page */
.equ GEN_GW_SCROLL_Y,           0x9F8C /*word: and the page y it shows now */
.equ GEN_GW_IND_GRABS,          0x9F8E /*word: the position indicator picked up */
.equ GEN_GW_SAVES,              0x9F90 /*word: pages written back to the document */
.equ GEN_GW_DOC_LIVE,           0x9F92 /*word: the document exists as a file now */
.equ GEN_GW_HEADFOOT,           0x9F94 /*word: the header or the footer opened */
.equ GEN_GW_RENAMES,            0x9FA2 /*word: documents renamed on the disk */
.equ GEN_GW_SHOWPICS,           0x9FA4 /*byte: $023A, all ones while pictures show */
.equ GEN_GW_PRINTS,             0x9FA6 /*word: the Print dialog was answered */
.equ GEN_GW_PR_PAGES,           0x9FA8 /*word: pages sent to the printer */
.equ GEN_GW_PR_QUAL,            0x9FAA /*byte: $4184 as the box left it */
.equ GEN_GW_PR_FEED,            0x9FAB /*byte: $4185 */
.equ GEN_GW_PR_FROM,            0x9FAC /*byte: $4187 */
.equ GEN_GW_PR_TO,              0x9FAD /*byte: $4188 */
.equ GEN_GW_PR_SEND,            0x9FAE /*byte: 1 printed, 0 only previewed */
.equ GEN_GW_PR_ANS,             0x9FAF /*byte: how the box was answered */

/* PutDecimal's digit buffer.  It used to be M2UI_STRING_AREA, $4400 -- an
 * address in the APPLICATION's window, picked from src/tests/selftest_m2ui.S's
 * own layout, where it was the selftest's `GEOS' string and belonged to nobody
 * else.  Every PutDecimal in the machine wrote nine bytes over whatever the
 * running application had at $4400, and what that was depended on where the
 * link happened to put things: geoWrite's $4400 was live code, and the two
 * bytes it lost turned `tst.b gw_pr_send(a5) / beq' into `ori.b' pairs, so the
 * Print dialog laid out one page instead of four and adding six bytes anywhere
 * ahead of it moved the wound. The buffer is the kernal's now, and PutDecimal
 * hands PutString the a5-relative form of it the same way fileTrScTab
 * is handed out -- an A6 offset is $8000 + offset seen from A5. */
.equ GEN_CONIO_DEC_BUF,         0x9FB0 /*9 bytes: five digits, room and a NUL */
.equ GEN_GW_PAGE_W,             0x9FBA /*word: $2EC4/$2EC5, the page's width */
.equ GEN_GW_SELPAGE,            0x9FBC /*word: `select page' adopted a ruler */
.equ GEN_GW_CREATES,            0x9FBE /*word: documents made by `new document' */
.equ GEN_GW_OPENS,              0x9FC0 /*word: documents the file selector opened */

/* The three registers a descriptor command may still need when it is reached,
 * kept for the length of one parse -- see m2ui_dialog_parse. */
.equ GEN_M2UI_DIALOG_R5,        0x9FC2 /*word: DBGETSTRING/DBGETFILES buffer */
.equ GEN_M2UI_DIALOG_R7,        0x9FC4 /*word: DBGETFILES' GEOS file type */
.equ GEN_M2UI_DIALOG_R10,       0x9FC6 /*word: DBGETFILES' class string */
/* Byte: the window-plane keyboard was already up when this box opened, so this
 * box is not the reason it is up and must not take it down.  See
 * .dialog_close in src/kernal/dlgbox/dialog.S. */
.equ GEN_M2UI_DIALOG_KBD,       0x9FC8
/* Byte: which of the application sprites 1-7 were enabled when this box opened.
 *
 * DlgBoxPrep (dlgbox1c.s:34) is `jsr DialogSave / LoadB mobenble, 1' -- the
 * VIC's sprite-enable register set to %00000001, sprite 0 and nothing else --
 * and `mobenble' is one of the twelve regions DialogCopyTab saves into
 * dlgBoxRamBuf, so Dialog_2's DialogRestore puts the old mask back when the box
 * comes down.  A dialog shows the pointer and hides everything the application
 * had up.  One byte here for the same reason the C64 uses one register: the
 * mask IS the state. */
.equ GEN_M2UI_DIALOG_SPR_EN,    0x9FC9
/* Eight bytes of RAM the QA chain assembles a stub into: `jsr (abs).L / rts',
 * which is how a selftest gets a ROM routine into a place appMain can name.
 * appMain is a LOGICAL address -- geos_call_logical is `%a5 + a sixteen-bit
 * offset' -- and the selftest lives in the cartridge, so it cannot be pointed
 * at directly.  It is what answers a modal dialog from inside the dialog's own
 * loop, which calls appMain on every pass exactly as MainLoop does. */
.equ GEN_QA_TRAMP,              0x9FCA
/* geoBASIC's sound: two statements, and one chip this machine has not got.
 *
 * VOICE picks a SID control byte per voice and leaves it in GEN_GB_WAVE; SOUND
 * writes the note into GEN_GB_SID and then that byte, and it is the gate bit in
 * it that makes the note sound at all.  Record 0 loads $0A2A..$0A2D as NOUGHT,
 * so a SOUND with no VOICE before it writes a control register of nought: the
 * gate never rises and nothing is heard.
 *
 * Both are HERE and not in CBM_SCRATCH with the rest of the C64 image, and the
 * reason is worth the line: the scratch page has 25 free bytes at offset 903
 * and this wanted 29, and the 25 run straight into CBM_DK_REL at 928 -- which
 * is spelled `CBM_SCRATCH+928' rather than as a CBM_SC_ offset, so counting the
 * CBM_SC_ names alone says there is room when there is not.  Putting them here
 * is not a compromise either: GB_FI_NAME and GB_FI_NUM are geoBASIC's too and
 * they are already in this band.
 */
.equ GEN_SNDH_OPEN,             0x9FD3 /*byte: geos_snd_open has run */
.equ GEN_SNDH_TIMBRE,           0x9FD4 /*3: SND_TIMBRE_*, one a voice */
.equ GEN_SNDH_NOISE,            0x9FD7 /*byte: the voice that owns the PSG's one
                                        * noise generator, or $FF for none */
.equ GEN_GB_WAVE,               0x9FD8 /*4: $0A2A..$0A2D, indexed by the voice
                                        * number itself 1..3, the way $0A2A,X
                                        * is, so the first byte is never read */
.equ GEN_GB_SID,                0x9FDC /*25: the C64's $D400..$D418 as a
                                        * register file, the way CBM_VIC holds
                                        * the VIC's $D000..$D02E */
.equ GB_SID_VOL,                  24   /*byte: $D418, which SOUND puts $0F in */
.equ GB_SID_VOICE,                 7   /*bytes a voice, and $D400 is voice 1 */
.equ GB_SID_FREQ,                  0   /*word: low byte first, as the chip has */
.equ GB_SID_CTRL,                  4   /*byte: the waveform and the gate */
.equ GB_SID_AD,                    5   /*byte: attack in the high nibble */
.equ GB_SID_SR,                    6   /*byte: sustain in the high nibble */

.equ GEN_QA_COUNT,              0x9FD2 /*byte: and a counter beside it, for a
                                        * stub that has to answer a box more
                                        * than once -- DBFILE's re-opens after
                                        * a drive change */

/* dlgBoxCallerSP, at this machine's width.
 *
 * DoDlgBox does not have a loop: it draws the box, arms the vectors, and then
 * (dlgbox1a.s @8) pops its own caller's return address into dlgBoxCallerPC,
 * writes the stack index into dlgBoxCallerSP, and jumps to MainLoop.  Whatever
 * ends the box calls RstrFrmDialogue, which puts the stack index back, pushes
 * the address, and returns -- so the box's answer arrives at DoDlgBox's caller
 * out of a stack frame that has been thrown away, however deep the routine
 * that answered was.
 *
 * The C64 needs two variables for that because its stack is one fixed page, so
 * one byte names a position in it and the address to resume at is a separate
 * word.  A 68000 needs neither: %sp is the whole answer, and the address to
 * resume at is already on the stack it points to.  One long, therefore, where
 * the original has three bytes at $0853 and $0855 -- which stay declared, and
 * unused, because they are the C64's shape and nothing here can fill them.
 *
 * Zero means no box is open, and RstrFrmDialogue called then does what it does
 * on the C64 with a stale stack index, minus the crash: nothing.
 *
 * $9FF6 and not $9FF5: this is a long and %sp is written to it with a `move.l',
 * which is an address error on an odd address. */
.equ GEN_M2UI_DIALOG_CALLER_SP, 0x9FF6 /*long */

/* $9FFA up to $9FFF is what is left of the last free band below the screen at
 * $FFA000 -- the equates here carry GEOS_BIAS, so $9Fxx is the offset $1Fxx and
 * an address written without it lands 0x6000 BELOW %a6, outside the work area
 * entirely.  Six bytes. */

/* Long: when non-zero, the absolute base BitmapUp reads its source through
 * instead of the application's own.  An application sets it for the length of
 * one call to draw a picture that lives in the cartridge. */
.equ GEN_M2UI_BITMAP_BASE,      0x9F96 /*long */

/* The same idea for a font.  curIndexTable and cardDataPntr are sixteen-bit
 * pointers because that is what GEOS made them, and sixteen bits reach only the
 * application's own window -- so a face has to be copied into the window before
 * it can be drawn from, and geoWrite's largest is 3970 bytes of a $5C00 window.
 *
 * GEN_M2UI_FONT_BASE is what an application sets just before LoadCharSet to say
 * "these offsets are from here, not from A5".  LoadCharSet latches it into
 * GEN_M2UI_FONT_ABS, which is what the four dereference sites in font_basic.S
 * actually read, so a face stays addressable for as long as it is the face in
 * force and no later caller has to remember to set anything.  UseSystemFont
 * clears the request first, so the system font always comes from the kernal. */
.equ GEN_M2UI_FONT_BASE,        0x9F9A /*long: the application's request */
.equ GEN_M2UI_FONT_ABS,         0x9F9E /*long: and the face in force */

.equ GEN_M2UI_TRAMP_MENU_MOUSE,   0x8FA0
.equ GEN_M2UI_TRAMP_MENU_KEY,     0x8FA6
.equ GEN_M2UI_TRAMP_DIALOG_MOUSE, 0x8FAC
.equ GEN_M2UI_TRAMP_DIALOG_KEY,   0x8FB2
.equ GEN_M2UI_TRAMP_STRING_KEY,   0x8FB8
.equ GEN_M2UI_TRAMP_STRING_DONE,  0x8FBE
.equ GEN_M2UI_TRAMP_ICON_MOUSE,   0x8FC4
.equ GEN_M2UI_TRAMP_DEMO_MENU,    0x8FCA
.equ GEN_M2UI_TRAMP_DEMO_DONE,    0x8FD0
.equ GEN_M3B_TRAMP_MOUSE,         0x8FD6
.equ GEN_M3B_TRAMP_KEY,           0x8FDC
/* $4000, $4400 and $4500 -- the M2 UI selftest's own data, string and
 * descriptor areas -- used to be published here as M2UI_DATA_AREA,
 * M2UI_STRING_AREA and M2UI_DESC_AREA.  They are addresses in whatever
 * application is running, so they mean nothing outside the selftest that lays
 * itself out around them, and src/tests/selftest_m2ui.S declares the ones it
 * uses itself.  The names are gone because a kernal routine took one of them
 * for scratch (see GEN_CONIO_DEC_BUF) and the next one would have too. */

/* Original GEOS disk/file workspace (logical $8000-$849A, A6-relative). */
.equ diskBlkBuf,          0x8000
.equ fileHeader,         0x8100
.equ curDirHead,         0x8200
.equ fileTrScTab,        0x8300
.equ dirEntryBuf,        0x8400
/* There used to be a second set of these five -- `diskBlkBufLogical' and its
 * four siblings -- "absolute 16-bit GEOS logical addresses for public
 * pointers", and by the time they were deleted every one held the same number
 * as the plain name above it.  They date from when this file kept A6
 * DISPLACEMENTS: `fileTrScTab' was $0300 then, and code that had to publish a
 * pointer needed $8300 from somewhere.  The conversion to logical addresses
 * (this file's own header, and tools/verify_geos_map.py since) made them
 * duplicates, and duplicates that hid state: `_ReadFile' fills the track/sector
 * table and named it `fileTrScTabLogical', so tools/kernal_compare.py -- which
 * measures a routine by the shared names it touches -- could not see it, and
 * scored the routine 0 of 2 against a body that does the work. */
/* $841E, $8430 and $8453, not $041E, $0430 and $0453.  APP_RAM is $0400, so
 * the three disk names were being kept eighteen and seventeen bytes at a time
 * INSIDE the running application's own RAM, which every application is entitled
 * to write.  They survived only because nothing had put anything there yet.
 * tools/verify_geos_map.py compares every shared name against geossym.inc now. */
.equ DrACurDkNm,         0x841E
.equ DrBCurDkNm,         0x8430
.equ dataFileName,       0x8442
.equ dataDiskName,       0x8453
.equ curDevice,          0x00BA  /*original logical $00BA */
.equ curDrive,           0x8489
.equ diskOpenFlg,        0x848A
.equ isGEOS,             0x848B
.equ interleave,         0x848C
.equ NUMDRV,             0x848D
.equ driveType,          0x848E
.equ curRecord,          0x8496
.equ usedRecords,        0x8497
.equ fileWritten,        0x8498
.equ fileSize,           0x8499


/* M9 complete public-API compatibility telemetry in unused OS RAM. */
.equ GEN_M9_MAGIC,          0x8B40 /*long: API9 */
.equ GEN_M9_TEST_FLAGS,     0x8B44 /*native long */
.equ GEN_M9_RESULT,         0x8B48 /*byte: 1 pass, $ff fail */
.equ GEN_M9_FAILURE,        0x8B49 /*first failed test number */
.equ GEN_M9_INPUT_CALLS,    0x8B4A /*native word */
.equ GEN_M9_SYNC_CALLS,     0x8B4C /*native word */
.equ GEN_M9_ENTRIES,        0x8B4E /*native word */
.equ GEN_M9_PANIC_MAGIC,    0x8B50 /*long: PAN9 */
.equ GEN_M9_PANIC_CALLS,    0x8B54 /*native word */

/* geoBASIC integer interpreter working state (engine lives in the kernal; the
 * program buffer is caller-provided, the 26 variables live here). */
/* The rest of the four disk programs' telemetry; see GEN_SE_ENTRIES. */
.equ GEN_HE_READST,       0x8B56 /*word: GEOS status reading the file header */
.equ GEN_HE_WRITEST,      0x8B58 /*word: and writing it back */
.equ GEN_FC_ENTRIES,      0x8B5A /*word: times the File Copier was entered */
.equ GEN_FC_STATUS,       0x8B5C /*word: how the last copy ended (0 = copied) */
.equ GEN_FC_BYTES,        0x8B5E /*word: payload bytes the last copy moved */
.equ GEN_BASIC_VARS,      0x8B60 /*A..Z: 26 signed 32-bit longs (104 bytes) */
.equ GEN_BASIC_OUTVEC,    0x8BC8 /*word: logical PRINT char-output callback (D0.b=char) */
.equ GEN_BASIC_ERRLINE,   0x8BCA /*word: line number where a run error occurred (0 none) */
.equ GEN_BASIC_ERRCODE,   0x8BCC /*word: last run error code (0 = ok) */
.equ GEN_BASIC_CAPTURE,   0x8BCE /*long: value of the most recent PRINTed number (tests) */
.equ GEN_BASIC_CURLINE,   0x8BD2 /*word: line number currently executing */
.equ GEN_BASIC_TEST,      0x8BD4 /*QA-only: geoBASIC interpreter selftest result word */
.equ GEN_BASIC_FLAGS,     0x8BD6 /*byte: bit0 = STOP (END/STOP executed), bit1 = jumped */
.equ GEN_BASIC_PROG,      0x8BD8 /*long: absolute base of the program buffer (line lookup) */
.equ GEN_BASIC_TARGET,    0x0BDC /*word: GOTO/GOSUB target line number */
.equ GEN_BASIC_TEST2,     0x8BDE /*QA-only: geoBASIC intrinsic-function selftest word */
.equ GEN_BASIC_TEST3,     0x8BE0 /*QA-only: geoBASIC string-handling selftest word */
.equ GEN_BASIC_TEST4,     0x8BE2 /*QA-only: geoBASIC substring-function selftest word */
.equ GEN_BASIC_HEAP,      0x8BE4 /*long: base of the string-concat heap (0 = none) */
.equ GEN_BASIC_HEAPEND,   0x8BE8 /*long: first byte past the heap */
.equ GEN_BASIC_HEAPPTR,   0x8BEC /*long: bump pointer, reset to HEAP each RUN */
/* geoProgrammer assembler status (the pointer block lives at $0EA0). */
.equ GEN_ASM_ERR,         0x8BF0 /*word: assembler error number (0 = clean) */
.equ GEN_ASM_ERRLINE,     0x8BF2 /*word: source line the error was found on */
.equ GEN_ASM_SYMS,        0x8BF4 /*word: symbols defined in pass 1 */
.equ GEN_ASM_LINE,        0x8BF6 /*word: source line being assembled */
.equ GEN_ASM_PASS,        0x8BF8 /*word: 1 or 2 */
.equ GEN_ASM_TEST,        0x8BFA /*QA-only: assembler engine selftest result word */
.equ GEN_ASM_TEST2,       0x8BFC /*QA-only: addressing-mode selftest result word */
.equ GEN_ASM_TEST3,       0x8CE0 /*QA-only: macro selftest result word */
.equ GEN_ASM_RAN,         0x0BFE /*word: QA-only: a self-assembled application writes
                                  * its own marker here when the desktop runs it */
/* Control-flow runtime (stage 2): current line, jump target, GOSUB/FOR stacks. */
/* The assembler's error LIST, which is what makes a count possible: the engine
 * used to stop at the first bad line and report one number, and geoAssembler's
 * report box says `There were N errors:' because the original assembles the
 * whole file and counts.  GEN_ASM_ERRBUF is the caller's buffer -- zero if it
 * does not want the list -- and holds GEN_ASM_ERRMAX entries of
 * [line(word) code(word)].  GEN_ASM_ERRCOUNT keeps counting past the buffer's
 * end: the count is the count, the list is what would fit. */
.equ GEN_ASM_ERRBUF,      0x8CF8 /*long: absolute base of the error list (0 none) */
.equ GEN_ASM_ERRCOUNT,    0x8CFC /*word: lines this assembly rejected */
/* The Icon Editor's $196C, placed here because its own block at $890C is full:
 * 0 the file already had a GEOS type and nothing was asked, 1 the box went up
 * and was answered NO, 2 the file is REL and was refused, 3 it was converted. */
.equ GEN_IE_CONVST,       0x8CFE /*word: what `Convert to GEOS file type?' did */
.equ GEN_ASM_ERRMAX,      64
.equ GEN_ASM_ERRSIZE,     4

.equ GEN_BASIC_CURREC,    0x8D00 /*long: record ptr of the line now executing */
.equ GEN_BASIC_JLINE,     0x8D04 /*long: jump target line record */
.equ GEN_BASIC_RESUME,    0x8D08 /*long: absolute cursor to resume at (0 = line start) */
.equ GEN_BASIC_GSP,       0x8D0C /*byte: GOSUB stack depth */
.equ GEN_BASIC_FSP,       0x8D0D /*byte: FOR stack depth */
.equ GEN_ASM65_TEST5,     0x88EE /*QA-only: the 6502 program written out for a
                                  * host emulator to run; another orphan word,
                                  * see GEN_LNK_ROMLEN */
.equ GEN_LNK_TEST4,       0x8D0E /*QA-only: .rom cartridge output selftest;
                                  * squeezed in above GEN_BASIC_FSP for the
                                  * reason given at GEN_LNK_ROMLEN */
.equ GEN_BASIC_GSTACK,    0x8D10 /*8 x [record(4) cursor(4)] = 64 bytes */
.equ GEN_BASIC_FSTACK,    0x8D50 /*8 x [var(1) pad(3) limit(4) step(4) record(4)
                                  * cursor(4)] = 160 bytes.  The pad is real: the
                                  * stride is 20 and limit sits at +4, so an
                                  * annotation that omits it under-sizes the
                                  * array by 24 bytes and reports them free. */
.equ GEN_BASIC_DATAREC,   0x8DF0 /*long: record ptr of the DATA line being READ (0 = none yet) */
.equ GEN_BASIC_DATAPTR,   0x8DF4 /*long: absolute cursor at the next unread DATA item */
.equ GEN_BASIC_STRVARS,   0x8DF8 /*A$..Z$: 26 descriptors, each [ptr:long][len:word] = 156 bytes */
.equ GEN_BASIC_ARRPOOL,   0x8E94 /*long: base of the numeric-array pool (0 = none) */
.equ GEN_BASIC_ARREND,    0x8E98 /*long: first byte past the array pool */
.equ GEN_BASIC_ARRPTR,    0x8E9C /*long: bump pointer, reset to ARRPOOL each RUN */
.equ BASIC_GOSUB_MAX,     8
.equ BASIC_FOR_MAX,       8

/* Genesis M3 storage bookkeeping at logical $8C00. */
.equ GEN_M3S_MAGIC,       0x8C00 /*long */
.equ GEN_M3S_TEST_FLAGS,  0x8C04 /*long */
.equ GEN_M3S_RET_A,       0x8C08 /*byte */
.equ GEN_M3S_RET_X,       0x8C09 /*byte */
.equ GEN_M3S_RET_Y,       0x8C0A /*byte */
.equ GEN_M3S_ROM_FILES,   0x8C0C /*word */
.equ GEN_M3S_SRAM_FILES,  0x8C0E /*word */
.equ GEN_M3S_FREE_BLOCKS, 0x8C10 /*word */
.equ GEN_M3S_ITER_BLOCK,  0x8C12 /*byte */
.equ GEN_M3S_ITER_SLOT,   0x8C13 /*byte */
.equ GEN_M3S_LAST_BYTES,  0x8C14 /*word */
.equ GEN_M3S_INIT_FORMATTED, 0x8C16 /*byte */
/* SaveFile normally copies the range the header declares, out of the caller's
 * own window.  A tool that produces a program loading at the address the tool
 * itself occupies -- geoAssembler and geoLinker both do, at $1000 -- cannot put
 * the payload there without overwriting itself, so it names the buffer instead.
 * One-shot: SaveFile takes it and clears it, so a stale value can never reach
 * the next save. */
.equ GEN_SAVE_SRC,        0x8C18 /*long (abs): payload buffer, 0 = the window */
.equ GEN_SAVE_SRC_LIVE,   0x8C1C /*long (abs): the value this save is using */

/* Interactive pure-assembly M3 directory browser at logical $8C20. */
.equ GEN_M3B_MAGIC,        0x8C20
.equ GEN_M3B_ROW_COUNT,    0x8C24
.equ GEN_M3B_SELECTED,     0x8C25
.equ GEN_M3B_LAST_ACTION,  0x8C26
.equ GEN_M3B_LAST_ERROR,   0x8C27
.equ GEN_M3B_RECORD,       0x8C28
.equ GEN_M3B_REDRAWS,      0x8C2A /*word */
.equ GEN_M3B_CLICKS,       0x8C2C /*word */
/* The non-GEOS takeover probe.  One word of bit flags rather than a counter
 * per stage, because by this point every free band below $1000 is down to
 * scattered pairs of bytes -- this is the gap between GEN_M3B_CLICKS and
 * GEN_M3B_ROWS, and it is two bytes wide. */
.equ GEN_NG_TEST,          0x8C2E /*word: bit0 entered, bit1 masked interrupts,
                                   * bit2 took the VDP, bit3 held it, bit4 gave
                                   * it back, bit5 drew through GEOS again */
.equ GEN_M3B_ROWS,         0x8C30 /*8 records x 4: drive, structure, type, baseline y */

/* geoProgrammer assembler section state (GEN_M3B_ROWS ends at $0C50). */
.equ GEN_ASM_RAMPC,       0x8C50 /*long: the .ramsect location counter */
.equ GEN_ASM_RAMLEN,      0x8C54 /*long: total .ramsect size, for the caller */
.equ GEN_ASM_SECT,        0x8C58 /*word: 0 = .psect (code), 1 = .ramsect,
                                 * 2 = .zsect (page zero); see GEN_ASM_ZLEN */
/* .include state: a bump region for included text and a stack of return
 * cursors, so an included file may itself include another. */
.equ GEN_ASM_INCBUF,      0x8C5A /*long: base of the include text region */
.equ GEN_ASM_INCEND,      0x8C5E /*long: first byte past it */
.equ GEN_ASM_INCPTR,      0x8C62 /*long: bump cursor within the region */
.equ GEN_ASM_INCPEND,     0x8C66 /*long: text an .include just loaded (0 none) */
.equ GEN_ASM_INCSP,       0x8C6A /*word: nesting depth */
.equ GEN_ASM_INCSTK,      0x8C6C /*4 x [cursor(4) bumptop(4) textbase(4)] */
.equ GEN_ASM_INCMAX,      4
.equ GEN_ASM_TEXT,        0x8C9C /*long: base of the text now being walked --
                                  * a label is "column one" relative to THIS,
                                  * not to the outermost source file */
/* Conditional assembly.  Each level records whether it is assembling, whether
 * any branch of it has been taken yet, whether .else has been seen, and
 * whether the level enclosing it was assembling at all. */
.equ GEN_ASM_ACTIVE,      0x8CA0 /*word: 1 while lines are being assembled */
.equ GEN_ASM_IFSP,        0x8CA2 /*word: conditional nesting depth */
.equ GEN_ASM_IFSTK,       0x8CA4 /*8 x [active(1) taken(1) elseseen(1) parent(1)] */
.equ GEN_ASM_IFMAX,       8
/* Macros.  A definition is a 16-byte table entry (name, parameter count, body
 * pointer); the body is copied to a text bump region with every parameter
 * reference already reduced to a marker byte plus index, so expansion is a
 * copy with substitution rather than a name lookup. */
.equ GEN_ASM_MACTAB,      0x8CC4 /*long: base of the macro table */
.equ GEN_ASM_MACEND,      0x8CC8 /*long: first byte past it */
.equ GEN_ASM_MACPTR,      0x8CCC /*long: next free entry */
.equ GEN_ASM_MACTXT,      0x8CD0 /*long: base of the macro body text */
.equ GEN_ASM_MACTXTEND,   0x8CD4 /*long: first byte past it */
.equ GEN_ASM_MACTXTPTR,   0x8CD8 /*long: bump cursor */
.equ GEN_ASM_MACDEF,      0x8CDC /*long: entry being defined (0 = not) */
/* $0AA0-$0AFF is unclaimed: GEN_PANIC_STRING ends at $0A9F and the M2UI block
 * starts at $0B00.  The macro parameter names used to sit at $0CE2, which left
 * only 30 of the 32 bytes they need before GEN_BASIC_CURREC at $0D00. */
.equ GEN_ASM_MACPARM,     0x8AA0 /*4 x 8 parameter names, definition-time */
.equ GEN_ASM_MACPMAX,     4
.equ GEN_ASM_MACPLEN,     8

/* Relocatable object output (.rel).  A relocation site is an offset into the
 * .psect image holding a value that moves with the section; the linker adds the
 * module's load bias to whatever it finds there.  The width is recorded because
 * a 68000 site may be a 32-bit absolute address or a 16-bit displacement -- the
 * 6502 product had only the one 16-bit width and so needed no width field. */
.equ GEN_ASM_RELBUF,      0x8AC0 /*long: base of the relocation buffer */
.equ GEN_ASM_RELEND,      0x8AC4 /*long: first byte past it */
.equ GEN_ASM_RELPTR,      0x8AC8 /*long: next free entry */
.equ GEN_ASM_RELCUR,      0x8ACC /*word: the expression just read touched a
                                  * .psect symbol */
.equ GEN_ASM_RELQ,        0x8ACE /*2 words: one flag per operand of the
                                  * instruction being encoded, in parse order */
.equ GEN_ASM_RELQW,       0x8AD2 /*word: queue write index */
.equ GEN_ASM_RELQR,       0x8AD4 /*word: queue read index */
.equ GEN_ASM_GLBL,        0x8AD6 /*word: 1 while .glbl is in effect */
.equ GEN_ASM_RELOCS,      0x8AD8 /*word: relocation entries recorded */
.equ GEN_ASM_GLOBALS,     0x8ADA /*word: symbols marked global */
.equ GEN_ASM_TEST4,       0x8ADC /*QA-only: .rel output selftest result word */
.equ GEN_LNK_TEST3,       0x8ADE /*QA-only: VLIR overlay selftest result word */

/* ---------------------------------------------------------------------------
 * geoLinker: combine .rel modules into one launchable GEOS application.
 *
 * Like the assembler the engine holds no state of its own -- everything lives
 * in a caller-provided context block, whose address is the one thing that has
 * to survive a GEOS call, and so the one thing kept here.
 * ------------------------------------------------------------------------- */
.equ GEN_LNK_CTX,         0x8AE0 /*long: the context block being linked */
.equ GEN_LNK_ERR,         0x8AE4 /*word: LNKE_* */
.equ GEN_LNK_MODS,        0x8AE6 /*word: modules linked */
.equ GEN_LNK_SYMS,        0x8AE8 /*word: resident symbols resolved */
.equ GEN_LNK_RELOCS,      0x8AEA /*word: relocation sites applied */
.equ GEN_LNK_PSLEN,       0x8AEC /*long: total .psect length */
.equ GEN_LNK_RSLEN,       0x8AF0 /*long: total .ramsect length */
.equ GEN_LNK_TEST,        0x8AF4 /*QA-only: linker selftest result word */
.equ GEN_LNK_RAN,         0x0AF6 /*word: QA-only: stamped by the linked program */
.equ GEN_LNK_ERRLINE,     0x8AF8 /*word: the .lnk line an error was found on */
.equ GEN_LNK_TEST2,       0x8AFA /*QA-only: command-file selftest result word */
.equ GEN_LNK_GROUP,       0x8AFC /*word: the record group being walked now */
.equ GEN_LNK_RECS,        0x8AFE /*word: VLIR records the output carries */

/* The context block, all offsets from its base.  Pointers marked (abs) are
 * physical; the module names and the output name must live in a5-relative RAM
 * because r0 reaches them as a 16-bit logical pointer. */
.equ LNK_OUT,        0   /*long (abs): the combined image under construction */
.equ LNK_OUTEND,     4   /*long (abs) */
.equ LNK_SYMTAB,     8   /*long (abs): resident symbols, ASM_SYM_SIZE each */
.equ LNK_SYMEND,    12   /*long (abs) */
.equ LNK_SYMPTR,    16   /*long (abs): next free entry */
.equ LNK_RECBUF,    20   /*long (abs): staging for one record */
.equ LNK_RECEND,    24   /*long (abs) */
.equ LNK_NAMES,     28   /*long (abs): NUL-terminated names, empty name ends */
.equ LNK_HDR,       32   /*long (abs): 256-byte header buffer */
.equ LNK_OUTNAME,   36   /*long (abs): name for the linked application */
.equ LNK_PSECT,     40   /*long: .psect base, the address it will load to */
.equ LNK_RAMSECT,   44   /*long: .ramsect base, computed from the psect total */
.equ LNK_MODTAB,    48   /*8 x LNK_MOD_SIZE */
.equ LNK_VALBUF,   240   /*long (abs): one resolved value per symbol of the
                          * module being relocated, indexed as the .rel numbers
                          * them -- what an external relocation looks up */
.equ LNK_VALEND,   244   /*long (abs) */
.equ LNK_NAMEEND,  248   /*long (abs): first byte past the name list, so the
                          * command-file parser knows where it may write */
.equ LNK_FLAGS,    252   /*word: what the .lnk file asked for */
/* One entry per VLIR record the output will carry: where that record's image
 * starts in the buffer and how long it is.  Record 0 is the resident program;
 * record N is the overlay a .mod N group produced. */
.equ LNK_HDRNAME,  320   /*long (abs): the .header file's name, 0 = none */
.equ LNK_RECTAB,   256   /*8 x [bufoff(4) len(4)] */
.equ LNK_REC_OFF,    0
.equ LNK_REC_LEN,    4
.equ LNK_REC_SIZE,   8
.equ LNK_CTX_SIZE, 328
.equ LNK_F_VLIR,  0x0001 /*.vlir */
.equ LNK_F_DBG,   0x0002 /*.dbg  */
.equ LNK_F_ERR,   0x0004 /*.err  */
.equ LNK_F_MOD,   0x0008 /*a .mod named an overlay record */
.equ LNK_F_HDR,   0x0010 /*.header named a file to take the header from */
.equ LNK_F_ROM,   0x0020 /*.rom: a standalone Genesis cartridge image rather
                          * than a GEOS application -- absolute origin 0, a
                          * 64-entry vector table, a 256-byte cartridge header
                          * and a checksum, and no GEOS file header at all */
/* Where a .rom image's pieces are, as byte offsets that are also ROM
 * addresses: the linker builds the cartridge in place, so buffer offset and
 * cartridge address are the same number the whole way through. */
.equ LNK_ROM_VECTORS,   0x0000  /*64 longs: initial SP, initial PC, 62 handlers */
.equ LNK_ROM_HEADER,    0x0100  /*256 bytes */
.equ LNK_ROM_CODE,      0x0200  /*the linked program, and its entry point */
.equ LNK_ROM_STACK,  0x00FFFE00 /*initial SP: top of work RAM, less a margin */
.equ LNK_ROM_RAMBASE,0x00FF0000 /*where .ramsect goes in a cartridge */
.equ LNK_MODMAX,     8
.equ LNK_MOD_SIZE,  24
.equ LNK_MOD_PSBASE, 0
.equ LNK_MOD_RAMBASE,4
.equ LNK_MOD_PSLEN,  8
.equ LNK_MOD_RSLEN, 12
.equ LNK_MOD_BUFOFF,16   /*long: where this module's code sits in LNK_OUT --
                          * no longer just (psbase - LNK_PSECT), because an
                          * overlay's psbase is MOD_START while its image sits
                          * after the resident one in the buffer */
.equ LNK_MOD_GROUP, 20   /*word: the VLIR record it belongs to, 0 = resident */
/* A relocation entry: the offset of the site within this module's .psect image,
 * the section whose base has to be added, the width of the site in bytes, and
 * the symbol it names ($FFFF when the value is already in the site). */
.equ GEN_ASM_RELSIZE,     8
.equ GEN_ASM_REL_OFF,     0      /*long */
.equ GEN_ASM_REL_SECT,    4      /*byte: 0 .psect, 1 .ramsect */
.equ GEN_ASM_REL_WIDTH,   5      /*byte: 2 or 4 */
.equ GEN_ASM_REL_SYM,     6      /*word: symbol index, $FFFF = none */
.equ GEN_ASM_RELSECT_P,   0
.equ GEN_ASM_RELSECT_R,   1
.equ GEN_ASM_RELSECT_X,   2      /*external: the site holds nothing yet */

.equ M3B_STRING_AREA,      0x5600
.equ M3B_NAME_AREA,        0x5800 /*8 x 17-byte NUL strings */
.equ M3B_STATUS_AREA,      0x5900
.equ M3B_HEADER_AREA,      0x5A00
.equ M3B_PAYLOAD_AREA,     0x5B00
/* Genesis M4 application lifecycle bookkeeping. */
/* ---------------------------------------------------------------------------
 * geoProgrammer: kernal-resident 68000 assembler engine.
 *
 * The engine lives in the kernal so it executes in place from cartridge ROM and
 * costs nothing in the ~20 KB application window ($1000-$5F1B) that the IDE
 * shell has to fit in -- the same split geoBASIC uses for its interpreter.
 * Every buffer is caller-provided so the engine holds no state of its own.
 * ------------------------------------------------------------------------- */
.equ GEN_ASM_SRC,         0x8EA0 /*long: absolute base of the source text */
.equ GEN_ASM_PC,          0x8EA4 /*long: assembly program counter */
/* Which processor is being assembled for: 0 the 68000, 1 the 6502.  The front
 * end is shared, so this only chooses the operand parser and the encoder -- and
 * whether a .b/.w/.l suffix means anything, which on a 6502 it does not. */
.equ GEN_ASM_CPU,         0x891A /*word: 0 = 68000, 1 = 6502 -- placed here
                                 * because the assembler's own band is full;
                                 * $0EA8 looked free and is GEN_ASM_OUT */
.equ GEN_ASM_OUT,         0x8EA8 /*long: absolute base of the object buffer */
.equ GEN_ASM_OUTEND,      0x8EAC /*long: first byte past the object buffer */
.equ GEN_ASM_OUTPTR,      0x8EB0 /*long: emit cursor, reset at the start of pass 2 */
.equ GEN_ASM_SYMTAB,      0x8EB4 /*long: absolute base of the symbol table */
.equ GEN_ASM_SYMEND,      0x8EB8 /*long: first byte past the symbol table */
.equ GEN_ASM_ORG,         0x8EBC /*long: address the object is being assembled for */

.equ GEN_M4_MAGIC,            0x8EC0
.equ GEN_M4_TEST_FLAGS,       0x8EC4
.equ GEN_M4_STATE,            0x8EC8
.equ GEN_M4_APP_ACTIVE,       0x8EC9
.equ GEN_M4_DACC_ACTIVE,      0x8ECA
.equ GEN_M4_SAVED_APP_ACTIVE, 0x8ECB
.equ GEN_M4_APP_START,        0x8ECC /*native big-endian word */
.equ GEN_M4_APP_END,          0x8ECE /*native big-endian word */
.equ GEN_M4_APP_VECTOR,       0x8ED0 /*native big-endian word */
.equ GEN_M4_DACC_VECTOR,      0x8ED2 /*native big-endian word */
.equ GEN_M4_DACC_SAVED_SP,    0x8ED4 /*native physical long */
.equ GEN_M4_SAVED_DRIVE,      0x8ED8
.equ GEN_M4_PENDING_DRIVE,    0x8ED9
.equ GEN_M4_PENDING_NAME,     0x8EDA /*native big-endian word */
.equ GEN_M4_APP_ENTRIES,      0x8EDC /*native word */
.equ GEN_M4_DACC_ENTRIES,     0x8EDE /*native word */
.equ GEN_M4_RESTORE_COUNT,    0x8EE0 /*native word */
.equ GEN_M4_DESKTOP_COUNT,    0x8EE2 /*native word */
.equ GEN_M4_APP_TICKS,        0x8EE4 /*native long */
.equ GEN_M4_SWAP_SAVES,       0x8EE8 /*native word */
.equ GEN_M4_SWAP_LOADS,       0x8EEA /*native word */
/* Why the last desk-accessory launch did not happen.  It used to fail in
 * silence -- restore the environment, return, and the user sees a box flash and
 * the desktop come back with no idea which of the two steps refused. */

.equ GEN_M4_SAVED_MODE,       0x8EEC
.equ GEN_M4_SAVED_MOUSE_ON,   0x8EED
.equ GEN_M4_SAVED_DISP,       0x8EEE
.equ GEN_M4_SAVED_VECTORS,    0x8EF0 /*24 bytes, appMain through RecoverVector */
/* The pointer fences an accessory may narrow.  A6-RELATIVE like everything else
 * below $1000 here -- except that this one is above it, in the disk-driver
 * window at logical $9000, because the bands around GEN_M4_SAVED_VECTORS are
 * full.  include/storage_constants.inc owns the first $1C0 bytes of that
 * window (ST_TMP_IDS .. ST_UNDEL_VALID); this starts above them.
 *
 * The saved-vector block covers appMain..RecoverVector, which stops at $04B2
 * and does not reach mouseTop/mouseBottom/mouseLeft/mouseRight at $04B8..$04BD.
 * On the C64 an accessory does not restore those either -- the Calculator's
 * $52A0 narrows them to its own 80x80 window and its close at $52D6 never puts
 * them back -- because GEOS re-establishes them when the application resumes.
 * This port's StartMouseMode does not, so LdDeskAcc saves and RstrAppl restores
 * them; otherwise the desktop comes back with the pointer still locked inside
 * the box the accessory drew. */
.equ GEN_M4_SAVED_MOUSE_BOX,  0x91C0 /*6 bytes: mouseTop..mouseRight */
/* Why the last desk-accessory launch did not happen.  It used to fail in
 * silence: restore the environment, return, and all the user sees is a box
 * flash and the desktop come back, with no way to tell which of the two steps
 * refused or what it said.  In this window rather than beside the other M4
 * variables because the M4 band is full and this one has room. */
.equ GEN_M4_DACC_ERROR,       0x91C6 /*byte: the storage error that aborted it */
.equ GEN_M4_DACC_STEP,        0x91C7 /*byte: 1 = the swap save, 2 = the load */

/* The desk-accessory swap, in memory.
 *
 * GEOS writes it to disk because a C64 has nowhere else to put it -- the
 * accessory lands on top of the running application and 64 KB is all there is.
 * This machine has the same 64 KB of work RAM, but it does not have to spend
 * any of it on a disk driver: DISK_BASE at logical $9000 is the window the C64
 * keeps one in, and this port's driver is in cartridge ROM.  So the swap goes
 * here, and an accessory opening stops depending on there being room on drive B
 * -- which is 32 KB of cartridge SRAM and can genuinely fill up.
 *
 * Big enough for every accessory on the volume with room over: ruler1.5 is the
 * largest at 2408 bytes.  Anything that will not fit still goes to disk, so the
 * contract does not narrow, it just stops being the common case. */
.equ GEN_M4_SWAP_IN_RAM,      0x91C8 /*byte: the last swap was kept in memory */
.equ GEN_M4_SWAP_ADDR,        0x91CA /*word: logical address it came from */
.equ GEN_M4_SWAP_BYTES,       0x91CC /*word: how many */
/* Move-frame state, the port's translation of TopDesk's moveFlag/moveKoords.
 * A6-relative, in the disk-driver window with the other M4 state. */
.equ GEN_MV_FLAG,             0x91CE /*byte: $FF while a frame is being moved */
.equ GEN_MV_TOP,              0x91CF /*byte */
.equ GEN_MV_BOTTOM,           0x91D0 /*byte */
.equ GEN_MV_LEFT,             0x91D2 /*word */
.equ GEN_MV_RIGHT,            0x91D4 /*word */
.equ GEN_MV_LASTX,            0x91D6 /*word: pointer at the last redraw */
.equ GEN_MV_LASTY,            0x91D8 /*byte */
.equ GEN_MV_GRABX,            0x91DA /*word: pointer minus left, at the grab */
.equ GEN_MV_GRABY,            0x91DC /*byte: pointer minus top */
.equ GEN_MV_APPMAIN,          0x91DE /*word: the appMain this displaced */
.equ GEN_MV_MOUSEVEC,         0x91E0 /*word: and the mouseVector */
.equ GEN_MV_DONEVEC,          0x91E2 /*word: called with the final rectangle */
.equ GEN_MV_W,                0x91EA /*word: right - left */
.equ GEN_MV_H,                0x91EC /*byte: bottom - top */
.equ GEN_MV_DRAWN,            0x11ED /*byte: an outline is on the screen */

/* The mouse button's edges, waiting to be dispatched.
 *
 * GEOS's shape is the C64's: the interrupt notices the button changed, sets
 * pressFlag's MOUSE bit, and MainLoop dispatches mouseVector once for it.  That
 * works when MainLoop comes round many times a frame, which on a 1MHz 6502 with
 * a 320x200 bitmap it does.  Here a single foreground pass can take several
 * frames -- one directory read, one window redraw -- and every edge that lands
 * inside it collapses into the one flag.  Press, release, press, release: four
 * changes, one dispatch, and the three that were lost are the ones a click
 * is made of.  Measured: six button-state changes, two dispatches.
 *
 * So the flag keeps its meaning ("something is waiting") and the states queue
 * up behind it.  The interrupt pushes, DoCheckButtons pops one per pass and
 * leaves the flag set while more are waiting, and mouseData is what the edge
 * being dispatched says it is.  Single producer, single consumer, and the pop
 * runs with interrupts masked -- which is the php/sei the original puts around
 * exactly these reads. */
.equ GEN_M3_MSE_QUEUE,        0x91EE /*8 bytes: mouseData, oldest first */
.equ GEN_M3_MSE_QHEAD,        0x91F6 /*byte: next to dispatch */
.equ GEN_M3_MSE_QTAIL,        0x91F7 /*byte: next to fill */
.equ GEN_M3_MSE_LAST,         0x91F8 /*byte: last state the driver queued */
/* A and B, as bits 4 and 5, while they are still down from the on-screen
 * keyboard.  The keyboard forces both buttons to `released' while it is up --
 * on this machine A is both the mouse button and the key the keyboard presses,
 * and one hold cannot be both -- but the hold that TAKES the keyboard down
 * outlives it: RETURN answers a dialog, .dialog_close drops the keyboard, and
 * the very next frame reads the same A that is still physically held and
 * queues it as a press.  It used to land wherever the pointer happened to be
 * parked and do nothing; once DoMenu started placing the pointer on the menu
 * bar (docs/KERNAL_GAP.md item 18) it landed on `geos' and opened it, and
 * geoWrite's document had a menu over it for the rest of the session.  So each
 * button stays locked out until it is physically released. */
.equ GEN_M3_OSK_BTN_LOCK,     0x91F9 /*byte: A/B held over from the keyboard */
/* SetTextWin's one level of saved clip -- the original's :OldClip, six bytes:
 * windowTop, windowBottom, leftMargin.w, rightMargin.w. */
.equ GEN_TW_OLD,              0x91FA /*6 bytes */

.equ GEN_M4_SWAP_BUF,         0x9260 /*3072 bytes, .$9E5F */
/* The application's process table and delay stack, TimersCMDs through
 * DelayRtnsH, held while a desk accessory is up.  An accessory installs its
 * own -- ruler1.5 calls InitProcesses with a four-entry table of its own -- and
 * those entries are addresses inside the accessory's image, which stops
 * existing the moment it closes. */
.equ GEN_M4_SAVED_PROCS,      0x9E60 /*182 bytes: $0719..$07CE */

/* Sonic's sound effects: which tick of the score is playing, one past the end
 * of the saved process table.  See tools/sonic/build_sfx.py. */
.equ GEN_SONIC_SFX_TICK,      0x9F16 /*word: 0 idle, else the tick + 1 */
.equ GEN_SONIC_SFX_WHICH,     0x9F18 /*byte: which score is playing */
.equ GEN_SONIC_DUCK,          0x9F19 /*byte: Sonic 1's Sonic_Duck */
.equ GEN_SONIC_SPIN_FLAG,     0x9F1A /*byte: spindash_flag(a0) */
.equ GEN_SONIC_SPIN_CTR,      0x9F1C /*word: spindash_counter(a0), 0..$800 */
/* The camera, and the plane columns behind it. */
.equ GEN_SONIC_CAMX,          0x9F1E /*word: the level pixel at the left edge */
.equ GEN_SONIC_CAMCOL,        0x9F20 /*word: the level tile column it was at */
.equ GEN_SONIC_OFFS,          0x9F22 /*8 bytes: a shift per desktop repeat */
/* Which animation script he is on, and which he was on last frame -- `anim` and
 * `next_anim` in the object, and they are two bytes rather than one for the
 * same reason the cartridge keeps both: `Sonic_Animate` opens by comparing them
 * and restarting the cel and its timer when they differ. */
.equ GEN_SONIC_ANIM,          0x9F2D /*byte: an index into Ani_Sonic */
.equ GEN_SONIC_ANIMPREV,      0x9F2E /*byte: what it was last frame */
/* Two FM tracks, four bytes each: which score, the volume in its channel's TL
 * registers, and the tick (0 idle, else the tick + 1).  Two, because Sonic 1's
 * signpost is two channels playing the same loop four ticks apart. */
.equ GEN_SONIC_FM_TRACKS,     0x9F32 /*8 bytes: 2 x {which, vol, tick.w} */
/* Object 08, Sonic 2's dust.  One of it follows him while he charges, and the
 * skid throws off a short-lived puff every four frames -- the cartridge
 * allocates a fresh object for each of those, so this keeps a small pool.
 * Charging and skidding cannot overlap (one needs inertia zero and the other
 * needs $400 of it), which is why they can share one strip of VRAM. */
.equ GEN_DUST_ANIM,           0x9F3A /*byte: 0 none, 2 dash, 3 skid */
.equ GEN_DUST_STEP,           0x9F3B /*byte: how far into the script */
.equ GEN_DUST_TIME,           0x9F3C /*byte: anim_frame_duration */
.equ GEN_DUST_FRAME,          0x9F3D /*byte: its mapping frame */
.equ GEN_DUST_SHOWN,          0x9F3E /*byte: the frame whose art is in VRAM */
.equ GEN_DUST_WANT,           0x9F3F /*byte: the frame whose art is wanted */
.equ GEN_DUST_TIMER,          0x9F40 /*byte: obj08_dust_timer, the skid spawn */
.equ GEN_DUST_PIECES,         0x9F41 /*byte: SAT entries the dust will use */
.equ GEN_DUST_PUFFS,          0x9F42 /*24 bytes: 4 x {x.w, y.w, step.b, time.b} */
/* Whether a Z80 driver has been loaded and started.  It decides what giving
 * the sound chips back means -- see geos_fm_close in src/genesis/sound.S. */
.equ GEN_SND_Z80_LIVE,        0x9F2F /*byte: nonzero once geos_sound_boot ran */
.equ GEN_SND_FM_PARKED,       0x9F30 /*byte: this side woke the Z80 and owes it
                                      * a reset when it is finished */
.equ GEN_SND_S2_LIVE,         0x9F31 /*byte: Sonic 2's own Z80 driver is loaded
                                      * and running, so sounds go to its queue */
/* The 68000 side of that queue.  The Z80's own slot holds one sound and the
 * driver empties it once a frame, so the cartridge does not write it blind: it
 * keeps the request on this side and hands it over on the frame the slot reads
 * $80 again ("cmpi.b #$80,8(a1) / bne" in Sonic 2's own PlaySound).  $80 here
 * likewise means nothing is waiting. */
/* Colour.  The C64 keeps a thousand-byte matrix at a fixed address because the
 * VIC-II reads it out of RAM itself and cannot be told otherwise.  This machine
 * builds its tiles in software, so the matrix only has to exist when something
 * has colours to say -- and it has to be that way, because there is no
 * thousand-byte hole left to put one in.  The 64 KB is spoken for: $0400 up is
 * the application image and its RAM, $6000 the back screen, $8000 and $9000 the
 * OS variables and the disk window, $A000 the bitmap, and $BF40..$FE7F the
 * canonical GEOS image the pattern table and the BSW font are dereferenced out
 * of (`a5 + curPattern`, in _Rectangle).  That last one is why $BF40 looked
 * free to a symbol scan and was not.
 *
 * So the kernal publishes a pointer.  Null -- which is every case but one --
 * means every card takes `screencolors`, which is one byte read and exactly
 * what the machine did before there was any colour at all.  An application with
 * per-card colour allocates the thousand bytes in its own space and points this
 * at them. */
/* An application has claimed the Z80's 8 KB as scratch and owes it back.  Set
 * by geos_z80_scratch_claim, cleared by _release; while it is set the Z80 is
 * held in reset and both SND_*_LIVE flags are false, because nothing valid is
 * resident.  $1F5A is the first free byte after GEN_DUST_PUFFS' 24. */
.equ GEN_Z80_SCRATCH_HELD,    0x9F5A /*byte */
.equ GEN_COLOR_PTR,           0x9F62 /*long: a 1000-byte (ink<<4)|paper matrix,
                                      * or 0 for "every card is screencolors" */
/* And the invalidation that goes with it.  On a C64 the colour matrix is the
 * hardware and a store to it is on screen the same field; here the renderer
 * only re-reads a card's colour when that card's tile is dirty, and changing a
 * colour byte changes no pixel, so nothing would ever mark it.  An application
 * that writes the matrix widens this span -- the lowest and highest card index
 * it touched -- and geos_vdp_present_dirty turns it into dirty tiles and
 * closes it again.  LO > HI means nothing is pending, which is what a fresh
 * span is set to; the zeroed WRAM a cold boot starts from spends one card 0
 * upload saying so, once. */
.equ GEN_COLOR_FIRST,         0x9F6A /*word: the card index the matrix starts
                                      * at.  geoPaint's is 80, card row 2,
                                      * where its canvas begins -- nothing
                                      * above that is ever coloured, and its
                                      * own shadow of the matrix is short for
                                      * the same reason ($2142 moves 720 into
                                      * COLOR_MATRIX+80). */
.equ GEN_COLOR_COUNT,         0x9F6C /*word: how many cards it covers.  A card
                                      * outside FIRST..FIRST+COUNT is
                                      * screencolors like any uncoloured one. */
.equ GEN_COLOR_DIRTY_LO,      0x9F66 /*word: lowest card index whose colour
                                      * changed since the last present */
.equ GEN_COLOR_DIRTY_HI,      0x9F68
 /* word: and the highest */
.equ GEN_SND_S2_PEND,         0x9F2C /*byte: a sound ID waiting for the slot */

/* Sonic, the desk accessory.  A6-RELATIVE, in the disk-driver window above the
 * storage scratch and the saved mouse box -- see include/storage_constants.inc
 * for the rest of that window's tenants.  Positions are 16.16 and speeds 8.8,
 * which is what the cartridge uses (reference/sonic/SONIC_REFERENCE.md). */
.equ GEN_SONIC_X,         0x9200 /*long: 16.16 position */
.equ GEN_SONIC_Y,         0x9204 /*long: 16.16 position */
.equ GEN_SONIC_XSP,       0x9208 /*word: 8.8 horizontal speed */
.equ GEN_SONIC_YSP,       0x920A /*word: 8.8 vertical speed */
.equ GEN_SONIC_INERTIA,   0x920C /*word: 8.8 ground speed */
.equ GEN_SONIC_AIR,       0x920E /*byte: nonzero when airborne */
.equ GEN_SONIC_FACING,    0x920F /*byte: 0 right, 1 left */
.equ GEN_SONIC_FRAME,     0x9210 /*byte: mappings frame index */
.equ GEN_SONIC_SHOWN,     0x9211 /*byte: frame whose tiles are in VRAM */
.equ GEN_SONIC_ANIMTIME,  0x9212 /*byte: frames left on this animation cel */
.equ GEN_SONIC_ANIMSTEP,  0x9213 /*byte: position in the animation script */
.equ GEN_SONIC_ACTIVE,    0x9214 /*byte: the accessory owns the sprites */
.equ GEN_SONIC_JUMPING,   0x9215 /*byte: airborne from a jump, not a fall */
.equ GEN_SONIC_DONE,      0x9216 /*byte: the sign post was reached */
.equ GEN_SONIC_PIECES,    0x9217 /*byte: SAT entries the current frame uses */
.equ GEN_SONIC_PAD,       0x9218 /*word: this frame's pad state */
.equ GEN_SONIC_PADEDGE,   0x921A /*word: buttons pressed this frame */
.equ GEN_SONIC_POSTX,     0x921C /*word: sign post x */
.equ GEN_SONIC_POSTY,     0x921E /*word: sign post y */
.equ GEN_SONIC_TICKS,     0x9220 /*long: frames the accessory has run */
.equ GEN_SONIC_ENTRIES,   0x9224 /*word: times the accessory was opened */
.equ GEN_SONIC_SCRX,      0x9226 /*word: his x in pixels, for the sprite writer */
.equ GEN_SONIC_SCRY,      0x9228 /*word: and his y */

/* The sign post at the end of the act, which is how the accessory closes.
 * Object 0D's own routine numbers and its own animation state. */
.equ GEN_SIGN_ROUTINE,    0x9250 /*byte: 0 waiting, 2 spinning, 4 run out, 6 done */
.equ GEN_SIGN_ANIM,       0x9251 /*byte: index into Ani_Sign */
.equ GEN_SIGN_PREVANIM,   0x9252 /*byte: what AnimateSprite last saw */
.equ GEN_SIGN_ANIMSTEP,   0x9253 /*byte: position in the animation script */
.equ GEN_SIGN_ANIMTIME,   0x9254 /*byte: frames left on this cel */
.equ GEN_SIGN_FRAME,      0x9255 /*byte: Map_Sign frame index */
.equ GEN_SIGN_PIECES,     0x9256 /*byte: SAT entries the sign uses */
.equ GEN_SIGN_UPLOAD,     0x9257 /*byte: tiles of its art still to go to VRAM */
.equ GEN_SIGN_SPINTIME,   0x9258 /*word: frames left in this spin cycle */
.equ GEN_SONIC_LOCK,      0x925A /*byte: controls locked, he runs right */
.equ GEN_SONIC_EMIT_FLIP, 0x925B /*byte: mirror the pieces being written */
.equ GEN_SONIC_EMIT_TILE, 0x925C /*word: VRAM tile the pieces index from */
.equ GEN_SONIC_HEIGHT,    0x925E /*byte: obHeight -- 19 standing, 14 in a ball */
.equ GEN_SONIC_WIDTH,     0x925F /*byte: obWidth  --  9 standing,  7 in a ball */

.equ M4_TEST_LOAD_AREA,       0x4E00
.equ M4_APP_LOAD_START,       0x0400
.equ M4_APP_LOAD_LIMIT,       0x4000

.equ M3S_TEST_AREA,       0x5000
.equ M3S_HEADER_AREA,     0x5200
.equ M3S_NAME_AREA,       0x5400

/* Genesis M8 TopDesk telemetry in the unused gap after M4 bookkeeping
 * ($0F08-$0F1F) and before M5 telemetry. It must never overlap M4. */
.equ GEN_M8_MAGIC,          0x8F08 /*long: WIN8 */
.equ GEN_M8_OPEN_MASK,      0x8F0C /*byte */
.equ GEN_M8_ACTIVE_WINDOW,  0x8F0D /*byte */
.equ GEN_M8_SELECTION_MASK, 0x8F0E /*native word */
.equ GEN_M8_DRAGS,          0x8F10 /*native word */
.equ GEN_M8_COPIES,         0x8F12 /*native word */
.equ GEN_M8_TRASHES,        0x8F14 /*native word */
.equ GEN_M8_LAST_DROP,      0x8F16 /*byte: 0 none, 1 copy, 2 trash, 3 readonly */
.equ GEN_M8_RESULT,         0x8F17 /*GEOS error byte */
.equ GEN_M8_WINDOW_ACTIONS, 0x8F18 /*native word */

/* M10 final release startup/close telemetry in the remaining M8 gap. */
.equ GEN_M10_DISPLAY_LIVE,    0x8F1A /*byte: VDP display has been enabled */
.equ GEN_M10_PRESENT_READY,    0x8F1B /*byte: finished desktop plane is visible */
.equ GEN_M10_MODE_CLOSES,     0x8F1C /*native word: visible-window Mode closes */

/* Fidelity: canonical Show-menu display choice and size unit. */
.equ GEN_M5_SHOW_MODE,        0x8F1E /*byte: 0 icons,1 name,2 date,3 size,4 type */
.equ GEN_M5_UNIT_MODE,        0x8F1F /*byte: 0 blocks, 1 KBytes */


/* Genesis M5 TopDesk application telemetry (logical $8F20-$8F5F). */
.equ GEN_M5_MAGIC,          0x8F20
.equ GEN_M5_TEST_FLAGS,     0x8F24
.equ GEN_M5_STATE,          0x8F28
.equ GEN_M5_WINDOW_OPEN,    0x8F29
.equ GEN_M5_DRIVE,          0x8F2A
.equ GEN_M5_VIEW_MODE,      0x8F2B
.equ GEN_M5_FILE_COUNT,     0x8F2C
.equ GEN_M5_SELECTED,       0x8F2D
.equ GEN_M5_REDRAWS,        0x8F2E /*native word */
.equ GEN_M5_SELECTIONS,     0x8F30 /*native word */
.equ GEN_M5_OPENS,          0x8F32 /*native word */

.equ GEN_M5_MENU_ACTIONS,   0x8F34 /*native word */
.equ GEN_M5_SPLASH_TICKS,   0x8F36 /*native word */
/* M5 input-routing telemetry. */
.equ GEN_M5_INPUT_PRESS,    0x8F38
.equ GEN_M5_CHECK_PRESS,    0x8F39
.equ GEN_M5_DISPATCH_MOUSE, 0x8F3A /*native word */
.equ GEN_M5_MOUSE_VEC_SEEN, 0x8F3C /*little-endian logical pointer */
.equ GEN_M5_OTHER_VEC_SEEN, 0x8F3E /*little-endian logical pointer */
.equ GEN_M5_MOUSE_CALLBACKS,0x8F40 /*native word */
.equ GEN_M5_INPUT_MOUSE_DATA,0x8F42
.equ GEN_M5_CHECK_MOUSE_DATA,0x8F43
.equ GEN_M5_BACKSYNC,       0x8F44 /*native word */
.equ GEN_M5_DUPLICATES,     0x8F46 /*native word */
.equ GEN_M5_RENAMES,        0x8F48 /*native word */
.equ GEN_M5_DELETES,        0x8F4A /*native word */
.equ GEN_M5_SORTS,          0x8F4C /*native word */
.equ GEN_M5_LAST_ACTION,    0x8F4E /*byte: TopDesk operation code */
.equ GEN_M5_AUTO_SWAP,      0x8F4F /*byte: Special -> automatic swap flag */

/* 0x8F50..0x8F57 was M5_TOPDESK_NAME_AREA, eight bytes holding "TopDesk".
 * DeskTopName is "DESK TOP" and needs nine, so the buffer moved to $8F90 --
 * see M4_DESKTOP_NAME_AREA below.  Two of the eight are reused here. */
.equ GEN_M5_DOC_OPENS,      0x8F50 /*native word: documents opened through the
                                     application that owns them (XDocument) */

/* M7 authentic disk-menu telemetry; ends before native callback thunks at $0F60. */
.equ GEN_M7_MAGIC,          0x8F58 /*long: DSK7 */
.equ GEN_M7_LAST_OP,        0x8F5C /*byte */
.equ GEN_M7_RESULT,         0x8F5D /*GEOS error byte */
.equ GEN_M7_ACTIONS,        0x8F5E /*native word */
/* geoBASIC string heap (caller-provided bump allocator for concatenation). */


/* TopDesk VLIR module overlay telemetry.  Placed after the reserved callback
 * thunk block ($0F60-$0FE1), which is the region the M7 comment above refers
 * to; $0FE2-$0FFF is genuinely unclaimed. */
.equ GEN_M5_CUR_MODULE,     0x8FE2 /*byte: resident module number, $FF none */
.equ GEN_M5_MODULE_LOADS,   0x8FE4 /*native word: GetModule reads */
.equ GEN_M5_VISIBLE,        0x8FE6 /*byte: cached entries the window shows */
.equ GEN_M5_DIR_TOTAL,      0x8FE7 /*byte: entries in the whole directory */
.equ GEN_M5_PAGE_FIRST,     0x8FE8 /*byte: FindDirFiles skip count */
.equ GEN_M5_FOLDER,         0x8FE9 /*byte: folder being listed, 0 = root */
.equ GEN_M5_FOLDER_ENTERS,  0x8FEA /*native word */
.equ GEN_M5_FOLDER_LEAVES,  0x8FEC /*native word */
.equ GEN_M5_FOLDERS_MADE,   0x8FEE /*native word */
.equ GEN_M5_FOLDER_DELETES, 0x8FF0 /*native word */
.equ GEN_M5_FOLDER_COPIES,  0x8FF2 /*native word */

/* geoPaint band codec telemetry.  $0FF4-$0FFB is the last free space below
 * DISK_BASE at logical $9000: the callback thunks end at $0FE1 and the M5
 * overlay block above runs to $0FF3. */
.equ GEN_PAINT_TEST,        0x8FF4 /*word: QA-only: band codec selftest result */
/* $0FF6 and $0FF8 held GEN_PAINT_LEN and GEN_PAINT_PACKLEN, which reported
 * what the band codec's selftest already asserts on the console -- the lengths
 * are compared there, and a wrong one fails a bit.  Reclaimed when the dither
 * needed two words and this space was full; that is what "consolidate the map"
 * looks like in practice. */
.equ GEN_DITHER_TEST,       0x8FF6 /*word: QA-only: error-diffusion selftest */
.equ GEN_DITHER_DIFF,       0x8FF8 /*word: band bytes differing from the disk */
.equ GEN_PAINT_SUM,         0x8FFA /*word: checksum of the last band decoded */

/* GIF reader telemetry -- and the last four bytes of A6-relative space.  The
 * next variable added here has nowhere to go: $1000 is DISK_BASE at logical
 * $9000.  The decoded-pixel checksum is deliberately not exported for that
 * reason; the selftest compares it on the console instead, which is a stronger
 * check anyway. */
.equ GEN_GIF_TEST,          0x8FFC /*word: QA-only: GIF reader selftest result */
/* $0FFE held GEN_GIF_ROWS, which reported a row count the GIF selftest already
 * asserts on the console -- bit 1 of GEN_GIF_TEST is exactly "the decode
 * delivered GV_HEIGHT rows", set by comparing the same number.  Reclaimed for
 * geoPaint the application, on the same reasoning that reclaimed GEN_PAINT_LEN
 * and GEN_PAINT_PACKLEN: a word that only echoes an assertion is not worth the
 * last free word below DISK_BASE.
 *
 * $FFFF until geoPaint runs, then the icon index of the live tool -- 7, the
 * pencil, the moment its screen is up.  One word carries both facts a QA run
 * needs: that the application was entered, and that the tool box answered. */
.equ GEN_PT_TOOL,           0x8FFE
/* QA-only: the C64 screen editor's twelve cases, in the last free byte of OS
 * RAM.  0 = the selftest never ran, $5A = all twelve passed, anything else is
 * n+1 for the first case that failed -- the cases are listed in the header of
 * src/tests/selftest_cbmscreen.S.
 *
 * One byte because there is exactly one: $0400..$0F4F is GEOS's own variables
 * and the GEN block, $0F50 is TopDesk's drive name, $0F60..$0FE1 the callback
 * thunks, and $0FE2..$0FFE the desktop's and the QA selftests' results.  Above
 * $1000 is the disk window, where a block read lands on top of anything a boot
 * selftest wrote -- which is where two earlier homes for this went. */
.equ GEN_CBMSCR_TEST,       0x8FFF /*byte */
