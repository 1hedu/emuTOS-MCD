/* ROMDTEST.PRG -- does R: read the romdisk?
 *
 * On a cartridge boot R: is a FAT image in the ROM at 512 KB, read a
 * sector at a time by the servant (cart op 11). This lists R:, then
 * types the first .TXT it finds, which is everything a program run
 * from R: will ask of it: a directory, a file opened by name, and its
 * bytes read back.
 *
 * Built as ROMDAUTO.PRG for AUTO with -DROMD_AUTO, emulator-only: it
 * holds the screen afterwards, so a frame dump can read the result.
 */
typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;

void con_ws(const char *s);
long dos_fsetdta(void *dta);
long dos_fsfirst(const char *spec, long attr);
long dos_fsnext(void);
long dos_fopen(const char *name, long mode);
long dos_fread(long handle, long count, void *buf);
long dos_fclose(long handle);

struct dta {
    char    reserved[21];
    UBYTE   attr;
    UWORD   time, date;
    ULONG   size;
    char    name[14];
};

/* Anything handed to the OS is word-aligned, or tools/mkprg.py refuses
 * the program: a 68000 takes an address error on an odd word access. */
#define OSBUF __attribute__((aligned(4)))

static struct dta the_dta OSBUF;
static char txt[14] OSBUF;
static char buf[257] OSBUF;

static void putu(ULONG v)
{
    char t[11];
    int i = 10;

    t[i] = 0;
    do {
        ULONG q = 0, r = 0;
        int b;
        for (b = 31; b >= 0; b--) {             /* v / 10, no libgcc */
            r = (r << 1) | ((v >> b) & 1);
            if (r >= 10) { r -= 10; q |= 1UL << b; }
        }
        t[--i] = (char)('0' + r);
        v = q;
    } while (v);
    con_ws(t + i);
}

static int is_txt(const char *n)
{
    while (*n && *n != '.') n++;
    return n[0] == '.' && n[1] == 'T' && n[2] == 'X' && n[3] == 'T';
}

int pmain(void)
{
    long r, fh, n;
    int count = 0;

    con_ws("\033E" "ROMDISK TEST: R:\\*.*\r\n\r\n");
    dos_fsetdta(&the_dta);
    r = dos_fsfirst("R:\\*.*", 0);
    if (r < 0) {
        con_ws("Fsfirst failed: -");
        putu((ULONG)-r);
        con_ws("\r\n");
    }
    while (r == 0) {
        int i;
        con_ws("  ");
        con_ws(the_dta.name);
        con_ws("  ");
        putu(the_dta.size);
        con_ws("\r\n");
        if (!txt[0] && is_txt(the_dta.name))
            for (i = 0; i < 14; i++) txt[i] = the_dta.name[i];
        count++;
        r = dos_fsnext();
    }
    con_ws("\r\n");
    putu((ULONG)count);
    con_ws(" file(s)\r\n");

    if (txt[0]) {
        char path[20] OSBUF;
        int i;
        path[0] = 'R'; path[1] = ':'; path[2] = '\\';
        for (i = 0; txt[i]; i++) path[3 + i] = txt[i];
        path[3 + i] = 0;
        con_ws("\r\n");
        con_ws(path);
        con_ws(":\r\n");
        fh = dos_fopen(path, 0);
        if (fh < 0) {
            con_ws("Fopen failed\r\n");
        } else {
            n = dos_fread(fh, 256, buf);
            if (n > 0) {
                buf[n] = 0;
                con_ws(buf);
            }
            dos_fclose(fh);
        }
    }
#ifdef ROMD_AUTO
    con_ws("\r\nROMDISK TEST DONE\r\n");
    for (;;)
        ;
#endif
    return 0;
}
