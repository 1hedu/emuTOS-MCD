/* Included ahead of every Cyber Paint source file (-include): what
 * Aztec C and its libraries had that GCC, libcmini and GEMlib spell
 * differently. */
#include <osbind.h>
#include <ctype.h>

#define Setpallete(p)   Setpalette(p)       /* Aztec's spelling */
#define _tolower(c)     tolower(c)
/* Only ever _xbios(64, -1): Blitmode, to ask for a blitter. There is
 * none here, so Cyber Paint uses its own blit code. */
#define _xbios(op, arg) 0L
