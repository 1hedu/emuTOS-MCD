#!/usr/bin/env bash
# Build Dungeon Master for this machine, from ReDMCSB, with the original
# compiler.
#
#   tools/build-dm.sh [S11E|S12E]      -> vendor/stsoft/DM.PRG
#
# The version has to be the one your data files came from: DM 1.1 or 1.2,
# English. ReDMCSB lists every disk by MD5 in Documentation/ReDMCSB.xlsx,
# sheet Files, so a DUNGEON.DAT can be checked against it. Then put your
# own DUNGEON.DAT and GRAPHICS.DAT in vendor/stsoft/ beside it. Either build of the system carries them on D:
# -- the disc's filesystem, or the cartridge's romdisk -- and DM reads
# them from the drive it was started from.
#
# What it does:
#   1. fetches ReDMCSB (Christophe Fontanel's reverse-engineered DM/CSB
#      source) into .cache/, checked against a pinned SHA-256
#   2. cuts that version's game executable out of it, without the copy
#      protection: tools/dm-reduce.py
#   3. applies patches/dm/megacd.patch, which is the port, and the one
#      change that patch cannot carry for every version (below)
#   4. builds it with ReDMCSB's own Atari ST toolchain -- Megamax C 1.1,
#      its linker, a command shell -- under Hatari, headless, with the
#      EmuTOS image ReDMCSB ships as its TOS. About two minutes.
#
# Megamax rather than gcc because about 3000 lines of DM are inline
# assembly in Megamax's own syntax, written against its register
# allocation and calling convention. Run on its own compiler, all of it
# stays exactly as reverse-engineered, and the port is a short patch.
#
# Needs: python3 with py7zr (pip install py7zr) or 7z, unifdef, gcc (to
# read COMPILE.H's version symbols), hatari. Nothing of FTL's ends up in
# the repository: ReDMCSB is fetched, and the data files are yours.
set -euo pipefail
VER="${1:-S12E}"
# ReDMCSB's executable ID and link file for each game, from MKSS.BAT.
case "$VER" in
  S11E) EXEID=114; LNK=S11.LNK ;;
  S12E) EXEID=115; LNK=S12S13.LNK ;;
  *) echo "build-dm.sh: S11E (DM 1.1) or S12E (DM 1.2)" >&2; exit 1 ;;
esac
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$ROOT/.cache/dm"
W="$ROOT/build/dm"
URL=http://dmweb.free.fr/Stuff/ReDMCSB_WIP20210206.7z
SHA=58de16a34476a1ac2349ca01a4768cdecbadc9929fed0b69e8b9698f996159d1
ARCHIVE="$CACHE/$(basename "$URL")"

for t in unifdef gcc hatari; do
  command -v "$t" >/dev/null || { echo "build-dm.sh needs $t" >&2; exit 1; }
done

mkdir -p "$CACHE"
if [[ ! -f "$ARCHIVE" ]]; then
  curl -fsSL -o "$ARCHIVE.part" "$URL"
  mv "$ARCHIVE.part" "$ARCHIVE"
fi
echo "$SHA  $ARCHIVE" | sha256sum -c --quiet - || {
  echo "$ARCHIVE does not match the pinned checksum" >&2; exit 1; }

RD="$CACHE/ReDMCSB"
if [[ ! -d "$RD/Toolchains" ]]; then
  rm -rf "$RD.part"; mkdir -p "$RD.part"
  if command -v 7z >/dev/null; then
    7z x -bd -o"$RD.part" "$ARCHIVE" >/dev/null
  else
    python3 -I -c 'import sys, py7zr
with py7zr.SevenZipFile(sys.argv[1]) as z: z.extractall(sys.argv[2])' \
      "$ARCHIVE" "$RD.part"
  fi
  mv "$RD.part" "$RD"
fi
ST="$RD/Toolchains/Atari ST"

# The toolchain's hard disk, as ReDMCSB's own PowerShell lays it out:
# Megamax in \MEGAMAX, the sources and build scripts in \SOURCE, and
# \OBJECT and \BUILD for the results.
rm -rf "$W"; mkdir -p "$W"
cp -r "$ST/Base/HARDDISK" "$W/HD"
cp "$ST/Base/Hatari/tos.img" "$W/tos.img"
mkdir -p "$W/HD/SOURCE" "$W/HD/BUILD/$VER" "$W/HD/OBJECT/$VER/START.PAK" \
         "$W/HD/OBJECT/SU1E/START.PRG"
cp "$ST/Source/"*.BAT "$ST/Source/"*.LNK "$W/HD/SOURCE/"
python3 "$ROOT/tools/dm-reduce.py" "$RD/Toolchains/Common/Source" "$W/src" \
        --exeid "$EXEID"
patch -s -d "$W/src" -p1 < "$ROOT/patches/dm/megacd.patch"
# The save dialog's instruction. DIALOG.C orders its strings differently
# from one version to the next, so a patch hunk would not apply to all of
# them; the string itself is the same in each.
sed -i 's/"PUT GAME SAVE DISK IN DRIVE A:"/"SAVED GAMES ARE ON DRIVE S:"/' \
    "$W/src/DIALOG.C"
grep -q 'SAVED GAMES ARE ON DRIVE S:' "$W/src/DIALOG.C"
cp "$W/src/"* "$W/HD/SOURCE/"

# MKSS.BAT builds everything ReDMCSB knows; this is the two lines of it
# that matter here. The first puts the unimproved C startup code in the
# Megamax library, which is what ReDMCSB links every game with.
printf '%s\r\n' 'ECHO ON' 'PATH \MEGAMAX' 'CD \SOURCE' \
  'MKSSINIT.BAT \MEGAMAX \OBJECT\SU1E\START.PRG INIT' \
  "MKSSGAME.BAT $VER START $LNK -DEXEID=$EXEID" \
  'CD \' 'SHUTDOWN.PRG' > "$W/HD/SOURCE/REDMCSB.BAT"

# SHUTDOWN.PRG powers Hatari off through NatFeats when the batch file is
# done. The console goes to stdout (--conout 2), so a compile or link
# error is in the log rather than on a screen nobody can see.
( cd "$W" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  timeout 1200 hatari --confirm-quit false --tos tos.img --machine st \
    --memsize 1024 --harddrive HD --gemdos-case off --fast-forward on \
    --fast-boot on --sound off --natfeats on --cpuclock 32 \
    --compatible off --timer-d on --log-level warn --statusbar off \
    --conout 2 HD/PCOMMAND.PRG ) > "$W/hatari.log" 2>&1 || true

PAK="$W/HD/BUILD/$VER/START.PAK"
if [[ ! -s "$PAK" ]]; then
  echo "no START.PAK: the build failed. The console is in $W/hatari.log;" >&2
  echo "per-file compiler messages are in $W/HD/OBJECT/$VER/START.PAK/*.ERR" >&2
  exit 1
fi
mkdir -p "$ROOT/vendor/stsoft"
cp "$PAK" "$ROOT/vendor/stsoft/DM.PRG"
echo "built: vendor/stsoft/DM.PRG, $VER ($(stat -c%s "$PAK") bytes)"
for f in DUNGEON.DAT GRAPHICS.DAT; do
  [[ -f "$ROOT/vendor/stsoft/$f" ]] ||
    echo "missing: vendor/stsoft/$f -- copy it from your own $VER disk"
done
