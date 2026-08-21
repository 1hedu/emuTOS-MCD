/* SHOW.PRG -- look at Atari ST pictures on a television.
 *
 * ST low resolution is 320x200 in 16 colours, four bitplanes interleaved
 * a word at a time, 32000 bytes. That is not merely a format this
 * machine can read: it is exactly what the servant already converts to
 * VDP tiles sixty times a second. A Degas or NEOchrome file is a palette
 * and precisely that buffer. So showing one is a palette and a copy,
 * with no decoding at all -- which is why this is the first program
 * worth porting to a console whose only input is a d-pad. Nothing here
 * needs typing beyond choosing a file, and the result is the machine
 * doing what it is actually good at.
 *
 * Formats:
 *   .PI1  Degas       word resolution, 16 words palette, 32000 bytes
 *   .NEO  NEOchrome   word flag, word resolution, 16 words palette,
 *                     90 bytes of title and steering, 32000 bytes
 * Both uncompressed and both low resolution. The compressed variants
 * (.PC1, .TN1) are a different job and are not read here; a file that
 * is not the right length is refused rather than shown as noise.
 *
 * The palette is the part that needed hardware. There is no ST palette
 * on a Mega CD for Setcolor to write to -- the servant uploads sixteen
 * colours to the VDP once at startup and nothing could change them
 * afterwards, so any picture would have been drawn in the desktop's
 * colours. It now watches a small block just past the framebuffer, in
 * the 32K EmuTOS reserves for the screen and does not use: a magic, a
 * generation, and sixteen 0x0RGB words. Bump the generation and the
 * colours are in CRAM on the next frame.
 */

typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;

void con_ws(const char *s);
long con_in(void);
long dos_fsetdta(void *dta);
long dos_fsfirst(const char *spec, long attr);
long dos_fsnext(void);
long dos_fopen(const char *name, long mode);
long dos_fread(long handle, long count, void *buf);
long dos_fclose(long handle);
/* v_bas_ad out of the ST sysvar rather than Physbase(): this is the
 * same longword EmuTOS publishes to the servant every VBL, so the
 * picture and the palette block land where the pump is looking. */
#define V_BAS_AD  (*(UBYTE * volatile *)0x44EL)

#define SCREEN_BYTES 32000L

/* The GEMDOS disk transfer address. Only the name matters here, but the
 * whole 44 bytes must exist or Fsfirst writes past it. */
struct dta {
    char    reserved[21];
    UBYTE   attr;
    UWORD   time, date;
    ULONG   size;
    char    name[14];
};

/* The palette block the servant reads, at the end of the screen's own
 * reserved memory. Sixteen colours, an ST-order 0x0RGB word each. */
struct palblock {
    UWORD   magic0, magic1;     /* "PA" "L!" */
    UWORD   gen;
    UWORD   colour[16];
};

/* EmuTOS's own ST-low palette, so the desktop looks like itself again
 * when we are finished with the screen. Same sixteen the servant loads
 * at startup; a picture leaves its own behind otherwise, and the
 * desktop that comes back is unreadable. */
static const UWORD deskpal[16] = {
    0x777, 0x700, 0x070, 0x770, 0x007, 0x707, 0x077, 0x555,
    0x333, 0x733, 0x373, 0x773, 0x337, 0x737, 0x377, 0x000
};

static struct dta the_dta;
static char names[24][14];
static int  nfiles;

static UBYTE header[128];

static int slen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void scopy(char *d, const char *s)
{
    while ((*d++ = *s++)) ;
}

/* The two magic words and the generation, written last, so the servant
 * cannot see a half-written palette: it only acts when the generation
 * changes, and by then the sixteen colours are already there. */
static void setpal(const UWORD *pal)
{
    struct palblock *pb =
        (struct palblock *)(V_BAS_AD + SCREEN_BYTES);
    int i;

    for (i = 0; i < 16; i++) pb->colour[i] = pal[i];
    pb->magic0 = 0x5041;                /* "PA" */
    pb->magic1 = 0x4C21;                /* "L!" */
    pb->gen++;
}

static void collect(const char *spec)
{
    long r;

    dos_fsetdta(&the_dta);
    r = dos_fsfirst(spec, 0);
    while (r == 0 && nfiles < 24) {
        scopy(names[nfiles], the_dta.name);
        nfiles++;
        r = dos_fsnext();
    }
}

/* One line per file, lettered, because a letter is one keypress on the
 * on-screen keyboard and a filename is a dozen. */
static void menu(void)
{
    int i;
    char line[24];

    con_ws("\033E");                    /* VT52: clear screen, home */
    con_ws("SHOW -- Atari ST pictures\r\n\r\n");
    if (!nfiles) {
        con_ws("No .PI1 or .NEO files in this folder.\r\n\r\n");
        con_ws("Put some on a drive, open that folder,\r\n");
        con_ws("and run this from there.\r\n");
        return;
    }
    for (i = 0; i < nfiles; i++) {
        int p = 0, j = 0;
        line[p++] = (char)('A' + i);
        line[p++] = ' ';
        while (names[i][j] && p < 20) line[p++] = names[i][j++];
        line[p++] = '\r'; line[p++] = '\n'; line[p] = 0;
        con_ws(line);
    }
    con_ws("\r\nA letter shows one.  Q quits.\r\n");
}

/* Read one picture straight into the screen. The bitmap is the last
 * 32000 bytes of the file and the framebuffer is 32000 bytes of exactly
 * that layout, so it goes there with no pass over it in between. */
static int show(const char *name)
{
    long fh, n, hdr;
    const UWORD *pal;
    int e = slen(name);

    /* Which header, by extension. Length is checked after opening: a
     * .PI1 that is not 32034 bytes is not a Degas picture whatever it
     * is called, and painting 32000 bytes of something else onto the
     * screen is how a viewer becomes a way to see noise. */
    if (e >= 4 && name[e-3] == 'N' && name[e-2] == 'E' && name[e-1] == 'O') {
        hdr = 128; pal = (const UWORD *)(header + 4);
    } else {
        hdr = 34;  pal = (const UWORD *)(header + 2);
    }

    fh = dos_fopen(name, 0);
    if (fh < 0) return 0;

    n = dos_fread(fh, hdr, header);
    if (n != hdr) { dos_fclose(fh); return 0; }

    /* Low resolution only: the resolution word is 0 for ST low, and the
     * other two modes are a different number of bytes per line. */
    {
        UWORD rez = (hdr == 128) ? *(UWORD *)(header + 2)
                                 : *(UWORD *)(header + 0);
        if (rez != 0) { dos_fclose(fh); return -1; }
    }

    n = dos_fread(fh, SCREEN_BYTES, V_BAS_AD);
    dos_fclose(fh);
    if (n != SCREEN_BYTES) return 0;

    setpal(pal);
    return 1;
}

int pmain(void)
{
#ifdef SHOW_AUTO
    /* Emulator only, and never on a cartridge: no menu, no keys, show
     * the first picture in the folder and hold it so a frame dump can
     * be compared against the file it came from. This is how the
     * viewer is tested without a hand on the pad. */
    nfiles = 0;
    collect("*.PI1");
    collect("*.NEO");
    if (nfiles) show(names[0]);
    for (;;) ;
#endif
    for (;;) {
        long k;
        int c;

        nfiles = 0;
        collect("*.PI1");
        collect("*.NEO");
        menu();

        k = con_in();
        c = (int)(k & 0xFF);
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c == 'Q' || !nfiles) break;

        if (c >= 'A' && c < 'A' + nfiles) {
            int r = show(names[c - 'A']);
            if (r == 1) {
                con_in();               /* the picture, until a key */
                setpal(deskpal);
            } else {
                con_ws("\033E");
                con_ws(r == -1
                       ? "That one is not low resolution.\r\n"
                       : "That file will not read as a picture.\r\n");
                con_ws("\r\nPress any key.\r\n");
                con_in();
            }
        }
    }

    /* However we leave, the desktop gets its own colours back -- and
     * the console cursor is put out, because nothing else will. */
    setpal(deskpal);
    con_ws("\033f");
    con_ws("\033E");
    return 0;
}
