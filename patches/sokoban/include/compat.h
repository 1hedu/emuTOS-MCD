/* AHCC / Pure C GEM bindings on top of GEMlib, for Peter Lane's Sokoban. */
#ifndef SOKO_COMPAT_H
#define SOKO_COMPAT_H
#include <gem.h>
#include <stdarg.h>
#include <osbind.h>
/* Pure C's wind_set is variadic: a string for the name and info lines,
 * up to four words otherwise. GEMlib's is a fixed six-argument call, so a
 * string goes to wind_set_str and anything else passes four words.
 * Sokoban sometimes passes fewer (WF_TOP); the words read past them are
 * stack, and those fields ignore them. */
static short compat_wind_set(short h, short f, ...)
{
    va_list ap;
    short w[4] = {0, 0, 0, 0}, i;
    va_start(ap, f);
    if (f == WF_NAME || f == WF_INFO) {
        char *s = va_arg(ap, char *);
        va_end(ap);
        return wind_set_str(h, f, s);
    }
    for (i = 0; i < 4; i++) w[i] = (short)va_arg(ap, int);
    va_end(ap);
    return wind_set(h, f, w[0], w[1], w[2], w[3]);
}
#undef wind_set
#define wind_set compat_wind_set

/* Pure C names for things GEMlib spells differently. */
typedef MFDB FDB;
#define WHITE    G_WHITE
#define BLACK    G_BLACK
#define RED      G_RED
#define GREEN    G_GREEN
#define BLUE     G_BLUE
#define CYAN     G_CYAN
#define YELLOW   G_YELLOW
#define MAGENTA  G_MAGENTA
#define LWHITE   G_LWHITE
#define LBLACK   G_LBLACK
#define LRED     G_LRED
#define LGREEN   G_LGREEN
#define LBLUE    G_LBLUE
#define LCYAN    G_LCYAN
#define LYELLOW  G_LYELLOW
#define LMAGENTA G_LMAGENTA

/* Pure C passes the timer as two words, low then high; GEMlib as a long. */
#undef evnt_multi
#define evnt_multi(fl, cl, ma, st, m1f, m1x, m1y, m1w, m1h, m2f, m2x, m2y, m2w, m2h, \
                   buf, lo, hi, mx, my, bs, ks, kr, br) \
    mt_evnt_multi(fl, cl, ma, st, m1f, m1x, m1y, m1w, m1h, m2f, m2x, m2y, m2w, m2h, \
                  buf, ((long)(unsigned short)(hi) << 16) | (unsigned short)(lo), \
                  mx, my, bs, ks, kr, br, aes_global)
#endif
