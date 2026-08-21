.equ SET_RUNABLE,        0x80
.equ SET_BLOCKED,        0x40
.equ SET_FROZEN,         0x20
.equ SET_NOTIMER,        0x10
.equ MAX_PROCESSES,      20
.equ MAX_DELAYS,         20

/* M68k mapping of the original 6502 caller-visible registers. */
/* 6502 A -> D0.b, X -> D1.b, Y -> D2.b. A5 is GEOS_BASE; A6 is the permanent $8000-biased GEOS anchor. */

.equ M1_MAGIC_VALUE,     0x47454F53      /* "GEOS" */
.equ M1_TEST_MEMORY,     0x0001
.equ M1_TEST_MATH,       0x0002
.equ M1_TEST_PROCESS,    0x0004
.equ M1_TEST_DELAY,      0x0008
.equ M1_TEST_TIME,       0x0010
.equ M1_TEST_MAP,        0x0020
.equ M1_TEST_JUMPTAB,    0x0040
.equ M1_TEST_ALL,        0x007F

/* GEOS graphics mode / screen-selection constants. */
.equ ST_WR_BACK,          0x40
.equ ST_WR_FORE,          0x80
/* What iconSelFlg asks ProcessClick for when an icon is clicked (const.inc).
 * ST_FLASH inverts, holds selectionFlash and inverts back; ST_INVERT inverts
 * once and leaves it; neither is silence, which is what the Calculator wants. */
.equ ST_FLASH,            0x80
.equ ST_INVERT,           0x40
.equ SCREEN_WIDTH,        320
.equ SCREEN_HEIGHT,       200
.equ SCREEN_TILES_X,      40
.equ SCREEN_TILES_Y,      25
.equ SCREEN_TILE_COUNT,   1000
.equ GEOS_DISPLAY_Y,       12
/* The screen's colours, measured off VICE captures of the originals rather
 * than chosen.  A GEOS screen has exactly two, set by _FirstInit as the colour
 * matrix (DKGREY << 4) + LTGREY, plus a black border and a blue pointer:
 *
 *      LoadB screencolors, (DKGREY << 4)+LTGREY
 *      LoadB mob0clr, BLUE ; sta mob1clr
 *      LoadB extclr, BLACK
 *
 * The whole of build/vice-ruler-screen.png is two colours, (213,213,213) and
 * (119,119,119), and build/vice-topdesk.png adds (0,0,0) for the border and
 * (62,81,240) for the pointer -- four colours in the entire frame.  The
 * Genesis has three bits a channel, so the levels available are multiples of
 * 34 -- but the VDP's output is not linear in the level, so the values below
 * were settled by rendering them and measuring: $C reads 205 against the
 * original's 213, $6 reads 115 against 119, $E reads 238 against 240 and $4
 * reads 82 against 81.  ($6 for the ink was the halfway case between two
 * levels on paper and the right one on screen.) */
/* The C64 colour names, which are GEOS's own.  They are palette indices
 * here, because palette line 0 is in this order. */
.equ BLACK,     0
.equ WHITE,     1
.equ RED,       2
.equ CYAN,      3
.equ PURPLE,    4
.equ GREEN,     5
.equ BLUE,      6
.equ YELLOW,    7
.equ ORANGE,    8
.equ BROWN,     9
.equ LTRED,    10
.equ DKGREY,   11
.equ MEDGREY,  12
.equ LTGREEN,  13
.equ LTBLUE,   14
.equ LTGREY,   15

.equ VDP_BORDER,          0x0000    /* extclr BLACK */
.equ VDP_PAPER,           0x0CCC    /* LTGREY, as measured */
.equ VDP_INK,             0x0666    /* DKGREY, as measured */

/* Palette line 0 is the C64's sixteen in the C64's own order, so a colour
 * nibble out of the matrix is a palette index.
 *
 * All sixteen are measured, off the same VICE this tree's other captures came
 * from.  Every capture in build/ is monochrome -- GEOS uses four colours and
 * no more -- so the other twelve were rendered on purpose: a monitor
 * breakpoint at the BASIC main loop fills the first sixteen screen cells with
 * reverse spaces and colour RAM with 0..15, and the screenshot is sampled cell
 * by cell.  tools/measure_c64_palette.sh does it.
 *
 * The proof that it is the right source: black, (119,119,119), (213,213,213)
 * and (62,81,240) come out of that render byte for byte identical to the four
 * already measured here off GEOS screens.  Same emulator, same palette, so the
 * twelve new ones sit beside the four old ones without a seam.
 *
 * Pepto's were tried first and were wrong for this machine, for a reason worth
 * recording.  In RGB his greys are about as far apart as these ((68,149)
 * against (119,213)), but the VDP has three bits a channel and quantising sent
 * his pair to levels 2 and 4 where this pair lands on 3 and 6.  Eight levels
 * is not many, and a palette can be faithful in RGB and still lose half the
 * contrast of the only two colours GEOS actually draws with.  Measure on the
 * hardware's own terms.
 *
 * Four keep hand-calibrated words rather than the arithmetic, because the
 * VDP's output is not linear in the level and those four were settled by
 * rendering and measuring -- see the note above.  Only BLUE differs from what
 * rounding gives ($2 against $4 in red); the other three agree.
 *
 * Index 0 is transparent on this machine and C64 colour 0 is black, over a
 * backdrop that is also black, so the one index the VDP cannot draw is the one
 * colour it does not need to. */
.equ VDP_C64_00,          0x0000    /* BLACK    (  0,  0,  0) measured, calibrated */
.equ VDP_C64_01,          0x0EEE    /* WHITE    (255,255,255) */
.equ VDP_C64_02,          0x064A    /* RED      (189, 81,108) */
.equ VDP_C64_03,          0x0CE8    /* CYAN     (145,245,220) */
.equ VDP_C64_04,          0x0E4A    /* PURPLE   (184, 85,246) */
.equ VDP_C64_05,          0x04C6    /* GREEN    (119,220, 70) */
.equ VDP_C64_06,          0x0E42    /* BLUE     ( 62, 81,240) measured, calibrated */
.equ VDP_C64_07,          0x06EE    /* YELLOW   (255,255, 92) */
.equ VDP_C64_08,          0x026A    /* ORANGE   (196,121, 45) */
.equ VDP_C64_09,          0x0068    /* BROWN    (138,103,  0) */
.equ VDP_C64_10,          0x0A8E    /* LTRED    (240,143,166) */
.equ VDP_C64_11,          0x0666    /* DKGREY   (119,119,119) measured, calibrated */
.equ VDP_C64_12,          0x0AAA    /* MEDGREY  (165,165,165) */
.equ VDP_C64_13,          0x08EA    /* LTGREEN  (195,255,153) */
.equ VDP_C64_14,          0x0E88    /* LTBLUE   (136,152,255) */
.equ VDP_C64_15,          0x0CCC    /* LTGREY   (213,213,213) measured, calibrated */
/* The pointer is one solid colour and stays that colour.  TopDesk reads it out
 * of the Preferences file and hands it to the hardware once --
 * DeskTop.main.s:146, "MoveB $8000+7,$d027" -- alongside the background at +5,
 * the border at +71 and the 63-byte pointer bitmap at +8, and the only other
 * thing it ever does with $d027 is copy it to $d028 so the drag ghost matches.
 * There is no light variant and nothing switches it.
 *
 * This tree has no Preferences file, so the blue is the port's stand-in for
 * byte 7 rather than a value read off a disk. */
.equ VDP_POINTER,         0x0E42    /* BLUE   (62,81,240) */
.equ VDP_POINTER_LIGHT,   0x0EEE    /* palette 1 index 3; unused since the
                                     * invented contrast switch went */

.equ PATTERN_TAB_LOGICAL, 0xD000
.equ BSW_FONT_LOGICAL,    0xD210

.equ M2_MAGIC_VALUE,      0x47525832  /* "GRX2" */
.equ M2_TEST_SCANLINE,    0x0001
.equ M2_TEST_POINT,       0x0002
.equ M2_TEST_LINES,       0x0004
.equ M2_TEST_RECTS,       0x0008
.equ M2_TEST_BITMAPUP,    0x0010
.equ M2_TEST_VDP,         0x0020
.equ M2_TEST_FONT,        0x0040
.equ M2_TEST_ALL,         0x007F

/* Milestone 3 controller/input constants. */
.equ M3_MAGIC_VALUE,      0x494E5033  /* "INP3" */
.equ M3_TEST_PAD,         0x0001
.equ M3_TEST_QUEUE,       0x0002
.equ M3_TEST_MOUSE_API,   0x0004
.equ M3_TEST_REGION,      0x0008
.equ M3_TEST_VECTORS,     0x0010
.equ M3_TEST_WINDOW,      0x0020
.equ M3_TEST_SPRITE,      0x0040
.equ M3_TEST_FIRSTINIT,   0x0080
.equ M3_TEST_ALL,         0x00FF
.equ SET_KEYPRESS,        0x80
.equ SET_INPUTCHG,        0x40
.equ SET_MOUSE,           0x20
.equ SET_MSE_ON,          0x80
.equ SET_MENUON,          0x40
.equ SET_ICONSON,         0x20
.equ PAD_UP,              0x0001
.equ PAD_DOWN,            0x0002
.equ PAD_LEFT,            0x0004
.equ PAD_RIGHT,           0x0008
.equ PAD_A,               0x0010
.equ PAD_B,               0x0020
.equ PAD_C,               0x0040
.equ PAD_START,           0x0080
.equ PAD_X,               0x0100
.equ PAD_Y,               0x0200
.equ PAD_Z,               0x0400
.equ PAD_MODE,            0x0800

/* Completed Milestone 2: original graphics/UI descriptor constants. */
.equ M2UI_MAGIC_VALUE,    0x55493243  /* "UI2C" */
.equ M2UI_TEST_GRAPHSTR,  0x0001
.equ M2UI_TEST_ICONS,     0x0002
.equ M2UI_TEST_MENU,      0x0004
.equ M2UI_TEST_PROMPT,    0x0008
.equ M2UI_TEST_DIALOG,    0x0010
.equ M2UI_TEST_DECIMAL,   0x0020
.equ M2UI_TEST_VECTORS,   0x0040
.equ M2UI_TEST_ALL,       0x007F

.equ MOVEPENTO,            1
.equ LINETO,               2
.equ RECTANGLETO,          3
.equ NEWPATTERN,           5
.equ ESC_PUTSTRING,        6
.equ FRAME_RECTO,          7
.equ PEN_X_DELTA,          8
.equ PEN_Y_DELTA,          9
.equ PEN_XY_DELTA,        10
.equ MENU_ACTION,       0x00
.equ DYN_SUB_MENU,      0x40
.equ SUB_MENU,          0x80
.equ HORIZONTAL,        0x00
.equ VERTICAL,          0x80

/* PutString/PutChar style codes, const.inc's own numbers. */
.equ ULINEON,             14
.equ ULINEOFF,            15
.equ BOLDON,              24
.equ ITALICON,            25
.equ OUTLINEON,           26
.equ PLAINTEXT,           27
.equ OK,                   1
.equ CANCEL,               2
.equ YES,                  3
.equ NO,                   4
.equ OPEN,                 5
.equ DISK,                 6
.equ DBTXTSTR,            11
.equ DBVARSTR,            12
.equ DBGETSTRING,         13
.equ DBSYSOPV,            14
.equ DBGRPHSTR,           15
.equ DBGETFILES,          16
.equ DBOPVEC,             17
.equ DBUSRICON,           18
.equ DB_USR_ROUT,         19
/* DBUSRICON regions answer with a code of their own, numbered in descriptor
 * order from here.  The six system buttons are 1..6 and the descriptor commands
 * 11..19, so $20 up is free and cannot be mistaken for either. */
.equ DBUSRICON_FIRST,   0x20
.equ DEF_DB_POS,        0x80
.equ DEF_DB_TOP,          32
.equ DEF_DB_BOT,         127
.equ DEF_DB_LEFT,         64
.equ DEF_DB_RIGHT,       255
/* The four cursor keys and the two deletes, as GEOS sends them and as geoWrite
 * reads them.  geoWrite settles both halves from the disk: kb_collect ($3417)
 * takes $1C and $1D as the delete and nothing else, and its command table at
 * $3487 has $08, $1E, $10 and $11 as left, right, up and down.  So $08 is a
 * CURSOR KEY, not a backspace -- KEY_BACKSPACE below is the port's own older
 * name for the same code and the on-screen keyboard no longer sends it. */
.equ KEY_LEFT,             8
.equ KEY_BACKSPACE,        8    /* the port's older name for KEY_LEFT's code */
.equ KEY_TAB,              9
.equ KEY_RETURN,          13
.equ KEY_UP,              16
.equ KEY_DOWN,            17
.equ KEY_ESCAPE,          27
.equ KEY_INSERT,          28
.equ KEY_DELETE,          29
.equ KEY_RIGHT,           30
/* The Commodore key sets bit 7, which is the whole of it: every one of the
 * thirty combinations in geoWrite's command table is `character | $80', and
 * kb_collect's first test is BMI -- a byte with bit 7 set is a command and is
 * never typed. */
.equ KEY_CBM_BIT,       0x80

/* True Milestone 3 storage constants. */
.equ M3S_MAGIC_VALUE,       0x46533347  /* "FS3G" */
.equ M3S_TEST_BLOCKS,       0x0001
.equ M3S_TEST_DIRECTORY,    0x0002
.equ M3S_TEST_SEQUENTIAL,   0x0004
.equ M3S_TEST_SAVE_LOAD,    0x0008
.equ M3S_TEST_RENAME_DELETE,0x0010
.equ M3S_TEST_VLIR,         0x0020
.equ M3S_TEST_SRAM,         0x0040
.equ M3S_TEST_UNDELETE,     0x0080
.equ M3S_TEST_ALL,          0x00FF
.equ NO_BLOCKS,             1
.equ INSUFF_SPACE,          3
.equ FULL_DIRECTORY,       4
.equ FILE_NOT_FOUND,        5
.equ BAD_BAM,               6
.equ UNOPENED_VLIR,         7
.equ INV_RECORD,            8
.equ OUT_OF_RECORDS,        9
.equ STRUCT_MISMAT,        10
.equ BFR_OVERFLOW,         11
.equ DEV_NOT_FOUND,        13
.equ WR_PR_ON,           0x26
.equ NOT_GEOS,              0
.equ DATA,                  3
.equ DESK_ACC,              5
.equ APPLICATION,           6
.equ APPL_DATA,             7
.equ TEMPORARY,             13
.equ SEQUENTIAL,            0
.equ VLIR,                  1
.equ USR,                   3
.equ OFF_CFILE_TYPE,        0
.equ OFF_DE_TR_SC,          1
.equ OFF_FNAME,             3
.equ OFF_GHDR_PTR,         19
.equ OFF_GSTRUC_TYPE,      21
.equ OFF_GFILE_TYPE,       22
.equ OFF_YEAR,             23
.equ OFF_SIZE,             28
.equ O_GHIC_WIDTH,          2
.equ O_GHIC_HEIGHT,         3
.equ O_GHIC_PIC,            4
.equ O_GHCMDR_TYPE,        68
.equ O_GHGEOS_TYPE,        69
.equ O_GHSTR_TYPE,         70
.equ O_GHST_ADDR,          71
.equ O_GHEND_ADDR,         73
.equ O_GHST_VEC,           75
.equ O_GHFNAME,            77
.equ O_GH_AUTHOR,          97
/* The parent application's CLASS, thirteen bytes of it, and the field a
 * deskTop opens a document through: SubDir.src.s's XDocument takes
 * diskBlkBuf+$75 -- 117 -- and hands it to FindFTypes as r10. */
.equ O_GHP_FNAME,         117
.equ O_GHINFO_TXT,       0xA0

/* Milestone 4 native application execution. */
.equ M4_MAGIC_VALUE,       0x41505034  /* "APP4" */
.equ M4_STATE_DESKTOP,     0
.equ M4_STATE_APPLICATION, 1
.equ M4_STATE_DESKACC,     2
.equ M4_TEST_APP_HEADER,   0x0001
.equ M4_TEST_APP_LOAD,     0x0002
.equ M4_TEST_DACC_HEADER,  0x0004
.equ M4_TEST_DACC_LOAD,    0x0008
.equ M4_TEST_ENTRY_VECTOR, 0x0010
.equ M4_TEST_SWAP_STORAGE, 0x0020
.equ M4_TEST_LIFECYCLE,    0x0040
/* GetFile's r0L: bit 0 loads without running, bits 6/7 carry the document
 * name into dataDiskName/dataFileName (load2.s UNK_5, load4b.s). */
.equ M4_TEST_DOC_LAUNCH,   0x0080
/* And the deskTop's half: O_GHP_FNAME -> FindFTypes -> open the owner. */
.equ M4_TEST_DOC_OWNER,    0x0100
.equ M4_TEST_SWAP_FILE,    0x0200
.equ M4_TEST_ALL,          0x03FF
.equ AUTO_EXEC,            14
