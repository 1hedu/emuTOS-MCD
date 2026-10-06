#!/usr/bin/env python3
"""Cut one version of Dungeon Master out of ReDMCSB's source tree.

ReDMCSB keeps every version of DM and CSB in one tree, told apart by
#ifdef MEDIAnnn_<versions> blocks and #if EXETYPE == ... tests. This
resolves those for one executable and leaves everything else -- the
code, its comments, its own macros -- exactly as written, so the result
is still source a person can read and patch, and still compiles under
the original Megamax C. ReDMCSB's own build does the same reduction
(Copy-ReDMCSBCustomSource in its PowerShell) because Megamax runs out of
room for that many #defines.

How: COMPILE.H is preprocessed by the host's gcc for the chosen EXEID,
which says which version symbols are defined and what the EXETYPE
constants are worth. unifdef then removes exactly those conditionals.
Only the version symbols are resolved; an include guard or a feature
macro is left for the compiler, as it was.

Usage: dm-reduce.py <ReDMCSB Common/Source dir> <out dir> [--exeid 115]
Writes the game's files, every file they include, and EXEID.H.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

# The game executable's compile list, from MKSSGAME.BAT: the same for
# every Atari ST version of DM 1.x.
GAME = ("DATA DUNVIEW BLTSHRNK FLIPHORI FLIPVERT BLIT BLITMASK BLITFILL "
        "DUNGEON GAMELOOP BASE OBJECT TEXT PROJEXPL TIMELINE GROUP MOVESENS "
        "CHAMPION PANEL COMMAND MENU TOS SAVEPATH READWRIT DIALOG SAVEHEAD "
        "LOADSAVE PALETTE TITLE ENTRANCE ENDGAME STARTUP1 FLOPPYST FLOPPY "
        "DECOMPDU STARTUP2 SOUND IO COPYPRO1 COPYPRO2 COPYPRO3 COPYPRO4 "
        "COPYPRO5 COPYPRO6 COPYPRO7 COPYPRO8 COPYPRO9 EXPAND MEMORY LZW").split()

VERSION_SYM = re.compile(r'^(MEDIA\w*|EXETYPE|EXEID|C\d\d_\w+|NOCOPYPROTECTION)$')


def read(path):
    return open(path, encoding='latin-1').read()


def defines(src, exeid, stub):
    """Every macro COMPILE.H defines for this EXEID, as gcc sees them."""
    out = subprocess.run(
        ['gcc', '-E', '-dM', '-nostdinc', '-DEXEID=%d' % exeid,
         '-DNOCOPYPROTECTION=1', '-I', src, '-I', stub, '-x', 'c', '-'],
        input='#include "COMPILE.H"\n', capture_output=True, text=True,
        check=True).stdout
    d = {}
    for line in out.splitlines():
        m = re.match(r'#define (\w+)(?:\s+(.*))?$', line)
        if m:
            d[m.group(1)] = (m.group(2) or '').strip()
    return d


def unifdef_args(src, d):
    """-D/-U for every version symbol a conditional in the tree tests."""
    syms = set()
    for name in os.listdir(src):
        if not name.upper().endswith(('.C', '.H')):
            continue
        for line in read(os.path.join(src, name)).splitlines():
            if re.match(r'\s*#\s*(if|ifdef|ifndef|elif)\b', line):
                line = re.sub(r'/\*.*?\*/', '', line)
                syms.update(re.findall(r'\b[A-Za-z_]\w*\b', line))
    args = []
    for s in sorted(syms):
        if not VERSION_SYM.match(s):
            continue
        if s not in d:
            args.append('-U' + s)
            continue
        v = d[s]
        while v in d and d[v] != v:         # EXETYPE -> C03_GAME -> 3
            v = d[v]
        args.append('-D%s=%s' % (s, v) if v else '-D' + s)
    return args


def includes(path):
    found = []
    for line in read(path).splitlines():
        m = re.match(r'\s*#\s*include\s*"([^"]+)"', line)
        if m:
            found.append(m.group(1).upper())
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('out')
    ap.add_argument('--exeid', type=int, default=115)   # S12E\START.PAK
    a = ap.parse_args()

    with tempfile.TemporaryDirectory() as stub:
        # Megamax's own headers are not on the host; empty ones stand in,
        # since nothing in them decides a version.
        for name in os.listdir(a.src):
            if name.upper().endswith(('.C', '.H')):
                for m in re.finditer(r'#\s*include\s*<([^>]+)>',
                                     read(os.path.join(a.src, name))):
                    p = os.path.join(stub, m.group(1))
                    os.makedirs(os.path.dirname(p), exist_ok=True)
                    open(p, 'a').close()
        d = defines(a.src, a.exeid, stub)
    args = unifdef_args(a.src, d)

    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, 'EXEID.H'), 'wb') as f:
        f.write(b'#define EXEID %d\r\n' % a.exeid)
    done, todo = {'EXEID.H'}, [n + '.C' for n in GAME]
    while todo:
        name = todo.pop()
        if name in done:
            continue
        done.add(name)
        src = os.path.join(a.src, name)
        # unifdef exits 1 when it changed something: that is success here.
        r = subprocess.run(['unifdef', '-x2'] + args + [src],
                           capture_output=True)
        if r.returncode not in (0, 1):
            sys.exit('unifdef failed on %s: %s' % (name, r.stderr.decode()))
        dst = os.path.join(a.out, name)
        open(dst, 'wb').write(r.stdout)
        todo.extend(includes(dst))
    print('%d files for EXEID %d' % (len(done), a.exeid))


if __name__ == '__main__':
    main()
