/* EDIT.PRG -- write a file on a console.
 *
 * Until now nothing on this machine could author a byte. It could
 * format drives, copy the internal memory out, read a disc and run
 * somebody else's software, but a note, a batch file for EmuCON or a
 * changed EMUDESK.INF had to be made on another computer and carried
 * in. That is the gap this closes.
 *
 * A full-screen editor over the VT52 console EmuTOS already provides:
 * 40 columns, 25 rows, a status line at the top and a key line at the
 * bottom, 23 rows of text between them. The buffer is one flat array
 * and an insertion point, which for a file this size is faster than the
 * machinery that would avoid the memmove.
 *
 * On input. The on-screen keyboard is the only keyboard most of these
 * consoles will have, and until this program was written it sent
 * letters, digits and little else -- no cursor keys, and nothing that
 * was not text, so a program had no way to hear a command. Both were
 * added to the servant for this: the four arrows, and one Escape key.
 * Escape opens the command line at the bottom, which is where saving
 * and quitting live. Nothing else is stolen from the text.
 *
 * A serial keyboard works too, and is much the nicer way to use this --
 * the keyboard checkbox on the on-screen keyboard turns it on.
 */

typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;

void con_ws(const char *s);
long con_in(void);
long dos_cconis(void);
long dos_fsetdta(void *dta);
long dos_fsfirst(const char *spec, long attr);
long dos_fsnext(void);
long dos_fopen(const char *name, long mode);
long dos_fcreate(const char *name, long attr);
long dos_fread(long handle, long count, void *buf);
long dos_fwrite(long handle, long count, const void *buf);
long dos_fclose(long handle);

#define ROWS      23            /* text rows: 1..23, status 0, keys 24 */
#define COLS      40
#define BUFMAX    16000

struct dta {
    char    reserved[21];
    UBYTE   attr;
    UWORD   time, date;
    ULONG   size;
    char    name[14];
};

static struct dta the_dta;
static char  names[20][14];
static int   nfiles;

static char  buf[BUFMAX];
static long  len;               /* bytes in use */
static long  cur;               /* insertion point, 0..len */
static long  top;               /* buffer offset of the first shown row */
static int   dirty;
static char  fname[16];

/* ---- small helpers, because there is no libc here ---------------- */

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void scopy(char *d, const char *s) { while ((*d++ = *s++)) ; }

/* Decimal by repeated subtraction. These programs are linked without
 * libgcc -- it is not built -mpcrel and could not survive being loaded
 * -- so a long divide is an undefined reference, not a slow one. */
static void putn(long v)
{
    static const long p10[6] = { 100000L, 10000L, 1000L, 100L, 10L, 1L };
    int i, started = 0;

    for (i = 0; i < 6; i++) {
        int d = 0;
        while (v >= p10[i]) { v -= p10[i]; d++; }
        if (d || started || i == 5) {
            char s[2];
            s[0] = (char)('0' + d); s[1] = 0;
            con_ws(s);
            started = 1;
        }
    }
}

/* VT52: position the cursor. EmuTOS's console speaks this and the
 * servant converts whatever it draws, so this is all the screen
 * handling the program needs. */
static void at(int row, int col)
{
    char s[5];
    s[0] = 27; s[1] = 'Y';
    s[2] = (char)(' ' + row);
    s[3] = (char)(' ' + col);
    s[4] = 0;
    con_ws(s);
}
static void clreol(void) { con_ws("\033K"); }
static void clrscr(void) { con_ws("\033E"); }   /* shadow_forget() with it */
static void curs(int on) { con_ws(on ? "\033e" : "\033f"); }

/* ---- the buffer -------------------------------------------------- */

static void insert(char c)
{
    long i;
    if (len >= BUFMAX - 1) return;
    for (i = len; i > cur; i--) buf[i] = buf[i-1];
    buf[cur++] = c;
    len++;
    dirty = 1;
}

static void backspace(void)
{
    long i;
    if (!cur) return;
    cur--;
    for (i = cur; i < len - 1; i++) buf[i] = buf[i+1];
    len--;
    dirty = 1;
}

/* Where the row containing `p` starts. A row ends at a newline or at
 * COLS characters, whichever comes first, which is what the screen
 * does -- so the cursor keys have to agree with the wrap or they walk
 * somewhere the caret is not. */
static long row_start(long p)
{
    long s = 0, n;
    for (;;) {
        n = s;
        while (n < len && buf[n] != '\n' && n - s < COLS) n++;
        if (n >= p || n >= len)
            return s;
        s = (n < len && buf[n] == '\n') ? n + 1 : n;
    }
}

static long row_next(long s)
{
    long n = s;
    while (n < len && buf[n] != '\n' && n - s < COLS) n++;
    return (n < len && buf[n] == '\n') ? n + 1 : n;
}

/* ---- the screen -------------------------------------------------- */

/* Drawing is split three ways, and that is the whole reason this
 * program is usable.
 *
 * The first version redrew all twenty-five rows on every keystroke,
 * cursor keys included. That is about a thousand characters through
 * EmuTOS's VT52 console and then through the servant's converter, which
 * costs the better part of a second for a full screen -- so every
 * letter appeared late and every arrow press did the same work as
 * typing. Moving the caret changes no text at all, and inserting a
 * character cannot change anything above the line it is on.
 *
 * So: the status line when the length or the modified flag moves, the
 * text from one row downwards when the buffer changes, and otherwise
 * nothing but a cursor reposition. */

static void draw_status(void)
{
    curs(0);
    at(0, 0);
    con_ws(fname[0] ? fname : "(unnamed)");
    con_ws(dirty ? " *  " : "    ");
    putn(len);
    con_ws(" bytes");
    clreol();
}

/* Which screen row holds the caret, and where along it. -1 if the caret
 * is not on the screen, which follow() exists to prevent. */
static int caret_row, caret_col;

static void find_caret(void)
{
    long p = top;
    int  r;

    caret_row = -1;
    for (r = 0; r < ROWS; r++) {
        long e = row_next(p);
        long last = (e > p && buf[e-1] == '\n') ? e - 1 : e;
        if (cur >= p && cur <= last) { caret_row = r; caret_col = (int)(cur - p); return; }
        if (e == p) return;
        p = e;
    }
}

static void place_caret(void)
{
    find_caret();
    if (caret_row >= 0) { at(caret_row + 1, caret_col); curs(1); }
}

/* What is on the screen now, so that a redraw can emit only the rows
 * that actually changed.
 *
 * Typing a character at the top of a file re-lays every row below it,
 * but almost all of them come out identical -- the text only shifts
 * when a wrap moves. Without this, all twenty-three rows went through
 * the VT52 console on every keystroke and the letter arrived late; with
 * it, an ordinary keystroke costs one row. It is 920 bytes to stop
 * doing a screen's worth of work to change one character. */
static char shadow[ROWS][COLS + 1];

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void shadow_forget(void)
{
    int r;
    for (r = 0; r < ROWS; r++) shadow[r][0] = 1, shadow[r][1] = 0;
}

/* Rows `from` to the bottom, from the buffer. */
static void draw_text(int from)
{
    long p = top;
    int  r;

    curs(0);
    for (r = 0; r < from; r++) {
        long e = row_next(p);
        if (e == p) break;
        p = e;
    }
    for (; r < ROWS; r++) {
        long e = p;
        char line[COLS + 1];
        int  n = 0;

        while (e < len && buf[e] != '\n' && e - p < COLS)
            line[n++] = buf[e++];
        line[n] = 0;
        if (!same(shadow[r], line)) {
            at(r + 1, 0);
            con_ws(line);
            clreol();
            scopy(shadow[r], line);
        }
        p = (e < len && buf[e] == '\n') ? e + 1 : e;
        if (p >= len && e >= len) {
            for (r++; r < ROWS; r++)
                if (shadow[r][0]) {
                    at(r + 1, 0); clreol();
                    shadow[r][0] = 0;
                }
            break;
        }
    }
}

static void draw_keys(void)
{
    curs(0);
    at(24, 0);
    con_ws("arrows move   ESC = save/quit");
    clreol();
}

static void draw(void)
{
    draw_status();
    draw_text(0);
    draw_keys();
    place_caret();
}

/* Keep the caret on the screen: scroll a row at a time, which on a
 * console is cheaper than it sounds because draw() rewrites everything
 * anyway. */
static void follow(void)
{
    long p, s;
    int  r;

    if (cur < top) { top = row_start(cur); return; }
    p = top;
    for (r = 0; r < ROWS; r++) {
        s = row_next(p);
        if (cur <= (s > p ? s - 1 : s) || (r == ROWS - 1 && cur < s)) return;
        if (s == p) return;
        p = s;
    }
    top = row_next(top);
    follow();
}

/* ---- files ------------------------------------------------------- */

static int load(const char *name)
{
    long fh, n;
    fh = dos_fopen(name, 0);
    if (fh < 0) return 0;
    n = dos_fread(fh, (long)BUFMAX - 1, buf);
    dos_fclose(fh);
    if (n < 0) return 0;
    len = n; cur = 0; top = 0; dirty = 0;
    scopy(fname, name);
    return 1;
}

static int save(void)
{
    long fh, n;
    if (!fname[0]) return 0;
    fh = dos_fcreate(fname, 0);
    if (fh < 0) return 0;
    n = dos_fwrite(fh, len, buf);
    dos_fclose(fh);
    if (n != len) return 0;
    dirty = 0;
    return 1;
}

/* Read a line at the bottom of the screen. The only place this program
 * asks for text rather than taking it into the buffer. */
static void prompt(const char *ask, char *out, int max)
{
    int n = 0;
    at(24, 0); clreol();
    con_ws(ask);
    curs(1);
    for (;;) {
        long k = con_in();
        int  c = (int)(k & 0xFF);
        if (c == 13) break;
        if (c == 27) { n = 0; break; }
        if (c == 8) {
            if (n) { n--; con_ws("\010 \010"); }
            continue;
        }
        if (c >= 32 && c < 127 && n < max - 1) {
            char s[2];
            out[n++] = (char)c;
            s[0] = (char)c; s[1] = 0; con_ws(s);
        }
    }
    out[n] = 0;
}

/* Escape's menu. Save, quit, or go back to the text. */
static int command(void)
{
    for (;;) {
        long k;
        int  c;

        at(24, 0); clreol();
        con_ws("S)ave  N)ame  Q)uit  ESC back");
        k = con_in();
        c = (int)(k & 0xFF);
        if (c >= 'a' && c <= 'z') c -= 32;

        if (c == 27) return 0;
        if (c == 'S') {
            if (!fname[0]) prompt("Save as: ", fname, sizeof fname);
            if (!fname[0]) continue;
            at(24, 0); clreol();
            con_ws(save() ? "Saved.  Press a key." : "SAVE FAILED.  Press a key.");
            con_in();
            return 0;
        }
        if (c == 'N') { prompt("Name: ", fname, sizeof fname); return 0; }
        if (c == 'Q') {
            if (!dirty) return 1;
            at(24, 0); clreol();
            con_ws("Not saved.  Q again to lose it.");
            k = con_in();
            c = (int)(k & 0xFF);
            if (c == 'q' || c == 'Q') return 1;
            return 0;
        }
    }
}

/* ---- choosing a file --------------------------------------------- */

static void collect(const char *spec)
{
    long r;
    dos_fsetdta(&the_dta);
    r = dos_fsfirst(spec, 0);
    while (r == 0 && nfiles < 20) {
        int i;
        for (i = 0; i < nfiles; i++)             /* .TXT twice, once */
            if (names[i][0] == the_dta.name[0] && slen(names[i]) == slen(the_dta.name))
                break;
        scopy(names[nfiles], the_dta.name);
        nfiles++;
        r = dos_fsnext();
    }
}

static int pick(void)
{
    int i;

    nfiles = 0;
    collect("*.TXT");
    collect("*.INF");
    collect("*.BAT");

    clrscr();
    con_ws("EDIT -- a text editor\r\n\r\n");
    for (i = 0; i < nfiles; i++) {
        char line[24];
        int p = 0, j = 0;
        line[p++] = (char)('A' + i);
        line[p++] = ' ';
        while (names[i][j] && p < 20) line[p++] = names[i][j++];
        line[p++] = '\r'; line[p++] = '\n'; line[p] = 0;
        con_ws(line);
    }
    con_ws(nfiles ? "\r\nA letter opens one.\r\n" : "No text files here.\r\n");
    con_ws("N starts an empty file.  Q quits.\r\n");

    for (;;) {
        long k = con_in();
        int  c = (int)(k & 0xFF);
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c == 'Q') return 0;
        if (c == 'N') { len = cur = top = 0; dirty = 0; fname[0] = 0; return 1; }
        if (c >= 'A' && c < 'A' + nfiles) {
            if (load(names[c - 'A'])) return 1;
            con_ws("Will not open.\r\n");
        }
    }
}

/* ---- the loop ---------------------------------------------------- */

int pmain(void)
{
#ifdef EDIT_AUTO
    /* Emulator only, and never on a cartridge: no menu and no keys.
     * Load a file that is certainly on C:, draw it, and hold, so a
     * framebuffer dump can be decoded and compared with the file. That
     * exercises the load, the line walker and the whole screen draw --
     * everything except the keystrokes. */
    load("README.TXT");
    cur = 0; top = 0;
    clrscr();
    shadow_forget();
    draw();
    for (;;) ;
#endif
    if (!pick()) { curs(0); clrscr(); return 0; }

    clrscr();
    shadow_forget();
    draw();

    for (;;) {
        long k;
        int  c, sc;
        long oldtop;
        int  row_before;

        k  = con_in();
        c  = (int)(k & 0xFF);
        sc = (int)((k >> 16) & 0xFF);

        /* Where the caret is now, so an edit knows the first row it
         * can possibly have changed. Taken before the key is acted on. */
        find_caret();
        row_before = caret_row;
        oldtop = top;

        switch (sc) {
        case 0x4B:                                      /* left */
            if (cur) cur--;
            goto moved;
        case 0x4D:                                      /* right */
            if (cur < len) cur++;
            goto moved;
        case 0x48: {                                    /* up */
            long s = row_start(cur), col = cur - s;
            if (s) {
                long ps = row_start(s - 1), pe = row_next(ps);
                if (pe > ps && buf[pe-1] == '\n') pe--;
                cur = (ps + col < pe) ? ps + col : pe;
            }
            goto moved;
        }
        case 0x50: {                                    /* down */
            long s = row_start(cur), col = cur - s;
            long ns = row_next(s);
            if (ns < len || (ns == len && ns > s)) {
                long ne = row_next(ns);
                if (ne > ns && ne <= len && buf[ne-1] == '\n') ne--;
                if (ne > len) ne = len;
                cur = (ns + col < ne) ? ns + col : ne;
            }
            goto moved;
        }
        }

        if (c == 27) {
            if (command()) break;
            clrscr(); shadow_forget(); draw();
            continue;
        }

        /* An edit. Backspace can pull text up into the row above, so it
         * redraws from one row earlier than an insertion does. */
        if (c == 8) {
            backspace();
            follow();
            if (top != oldtop) { draw_status(); draw_text(0); }
            else { draw_status();
                   draw_text(row_before > 0 ? row_before - 1 : 0); }
            place_caret();
            continue;
        }
        if (c == 13 || (c >= 32 && c < 127)) {
            insert(c == 13 ? '\n' : (char)c);
            follow();
            if (top != oldtop) { draw_status(); draw_text(0); }
            else { draw_status();
                   draw_text(row_before >= 0 ? row_before : 0); }
            place_caret();
            continue;
        }
        continue;

        /* A cursor key changes no text. If it did not scroll, the only
         * thing on the screen that moves is the caret, and repainting a
         * screen to move a caret is what made this program feel slow. */
    moved:
        follow();
        if (top != oldtop) { draw_status(); draw_text(0); }
        place_caret();
    }

    /* Cursor off on the way out, not on.
     *
     * The VT52 cursor is drawn by EmuTOS's console into the
     * framebuffer, and the desktop neither uses it nor turns it off --
     * so leaving it enabled left a box blinking in the corner of the
     * desktop for the rest of the session. This program is the only
     * thing that ever switched it on, so it is the thing that has to
     * switch it off. Off first, then clear, so the cell it occupied is
     * cleared after it stops being redrawn. */
    curs(0);
    clrscr();
    return 0;
}
