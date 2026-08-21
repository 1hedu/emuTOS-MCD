#!/usr/bin/env bash
# Build the Mode 1 cart ROM that lifts the Mega CD's internal backup RAM
# into cart SRAM.  Usage: tools/build-rom.sh [out.bin]
#
# Cart in the slot, Sega CD attached, tray empty.
#
# boot/m1hello.S builds through here too: same header, same padding, a
# program that only cycles the backdrop colour. It answers the question
# the tool cannot -- whether this cart launches a ROM from this
# toolchain at all.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$ROOT/boot/m1tool.S}"
case "$(basename "$SRC")" in
  m1tool.S) DEF="$ROOT/build/mcdbram.bin" ;;          # the name it has always had
  *)        DEF="$ROOT/build/$(basename "${SRC%.S}").bin" ;;
esac
OUT="${2:-$DEF}"

PATH=/opt/m68k-elf/bin:$PATH
B="$ROOT/build"; mkdir -p "$B"

# The vector table, the header and the console bring-up come from
# megadev; only `main` is ours. Four builds of a hand-rolled header and
# init got four "no change"es off the console, and every fault in them
# was boilerplate this devkit has had right all along.
MD="$ROOT/vendor/megadev"
GCC="m68k-elf-gcc -x assembler-with-cpp -c -m68000 -Wa,--register-prefix-optional"
$GCC -I "$MD/lib" -I "$MD/lib/main" \
  -DHEADER_HARDWARE_ID="SEGA MEGA DRIVE" \
  -DHEADER_COPYRIGHT="(C)EMUTOS 26.AUG" \
  -DPROJECT_NAME="MCD BRAM DUMP" \
  -DPROJECT_NAME_DOMESTIC="MCD BRAM DUMP" \
  -DHEADER_SOFTWARE_ID="GM MCDBRAM-00" \
  -DHEADER_REGION="JUE" \
  -o "$B/boot.o" "$ROOT/boot/m1boot.s"
# -I so a source can .incbin the payloads it carries. The Mode 1 EmuTOS
# loader is mostly payload: an EmuTOS image and a ramdisk that used to
# come off the disc and now ride in the ROM.
# A single dbra copies at most 65536 longwords, which is 256 KB and also
# the size of Word RAM, so a payload larger than that would be copied
# short with no complaint. gas cannot check this itself -- a label
# difference spanning an .incbin and an .align is not an assembly-time
# constant -- so it is checked here, where the files can simply be
# measured.
for inc in $(grep -o '\.incbin[[:space:]]*"[^"]*"' "$SRC" 2>/dev/null \
             | sed 's/.*"\(.*\)"/\1/'); do
  for d in "$ROOT" "$ROOT/build" "$ROOT/build/fs" "$ROOT/emutos"; do
    [[ -f "$d/$inc" ]] || continue
    n=$(stat -c%s "$d/$inc")
    (( n <= 262144 )) || { echo "$inc is $n bytes: past one dbra of longwords" >&2; exit 1; }
    echo "payload $inc: $n bytes"
    break
  done
done

# The emulator-only AUTO diagnostic must never reach a cartridge. It
# runs before the desktop, waits half a minute for a cartridge swap and
# then holds forever -- on a television that is a machine that "does
# some test instead of booting", which is exactly how it was reported
# the one time a NOASK build slipped through to hardware.
for inc in $(grep -o '\.incbin[[:space:]]*"[^"]*"' "$SRC" 2>/dev/null \
             | sed 's/.*"\(.*\)"/\1/'); do
  for d in "$ROOT" "$ROOT/build" "$ROOT/build/fs" "$ROOT/emutos"; do
    [[ -f "$d/$inc" ]] || continue
    # SONICAUT.ACC is here for the same reason as the AUTO programs and
    # not for a different one: ACCAUTO builds an accessory that opens
    # itself at startup, which on a television is a console that plays
    # Sonic instead of booting. It only has a name of its own so that
    # this line can see it -- the AES loads any *.ACC.
    # -a, and it is not decoration: these are filesystem images, and
    # without it grep prints "binary file matches" instead of the match
    # and the name comes back empty. The version of this line that only
    # asked -q was right by accident; this one has to be told.
    hit=$(grep -aoE "SDIAGAUT|PALTEST|SHOWAUT|EDITAUT|NATAUT|PRNTAUT|SONICAUT|DDISKAUT|BRAMAUT|BRAMRWAU|FMTIAUTO" \
            "$d/$inc" | head -1 || true)
    if [[ -n "$hit" ]]; then
      echo "$inc carries $hit, which is emulator-only and must not reach" >&2
      echo "a cartridge. Rebuild the ISO without the flag that adds it." >&2
      exit 1
    fi
    break
  done
done

m68k-elf-as -m68000 --register-prefix-optional \
  -I "$ROOT" -I "$ROOT/build" -I "$ROOT/build/fs" -I "$ROOT/emutos" \
  -o "$B/rom.o" "$SRC"
m68k-elf-ld -Ttext 0x0 --oformat binary -o "$OUT" "$B/boot.o" "$B/rom.o"
rm -f "$B/rom.o" "$B/boot.o"

python3 - "$OUT" <<'PY'
import sys, os
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
if len(d) < 0x200:
    raise SystemExit("ROM is %d bytes: the header did not assemble" % len(d))
if d[0x100:0x104] != b'SEGA':
    raise SystemExit("no SEGA signature at 0x100")
# megadev's header leaves the extra-memory field blank -- its own TODO
# says so -- and without it the flash cart has no reason to create a
# save file at all, so the dump would go into memory nobody reads back.
# Written here rather than in the header source because it belongs to
# this ROM and not to the devkit.
#
#   D7 always 1, D6 battery-backed, D5 always 1, D4:D3 = 11 odd-byte
#   data, D2:D0 = 0.  That is 0xF8.
d[0x1B0:0x1BC] = (b'RA' + bytes([0xF8, 0x20])
                  + (0x00200001).to_bytes(4, 'big')
                  + (0x0020FFFF).to_bytes(4, 'big'))
if d[0x1B0:0x1B2] != b'RA':
    raise SystemExit("no RA save declaration at 0x1B0")
# Pad up to a power of two, minimum 32 KB.
#
# The first attempt at this added two paddings together and produced
# 32878 bytes -- neither a power of two nor even a whole number of
# blocks. The console would not launch it at all: the flasher's own
# screen stayed up, and pressing start from the menu froze on the menu.
# A 1536-byte ROM had run fine, so it was never smallness that mattered;
# it was a size no loader has any reason to expect.
size = 0x8000
while size < len(d):
    size *= 2
d += b'\xFF' * (size - len(d))
if len(d) & (len(d) - 1):
    raise SystemExit("ROM is %d bytes, which is not a power of two" % len(d))
# The ROM-end field in the header has to match what we actually shipped.
n = len(d) - 1
d[0x1A4:0x1A8] = n.to_bytes(4, 'big')
open(p, 'wb').write(d)
print("rom %d bytes (0x%X), SRAM declared %08X..%08X"
      % (len(d), len(d),
         int.from_bytes(d[0x1B4:0x1B8], 'big'),
         int.from_bytes(d[0x1B8:0x1BC], 'big')))
PY
echo "built: $OUT"
