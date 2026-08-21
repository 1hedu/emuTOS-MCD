# Decompress the Mega CD sub-CPU BIOS out of a boot ROM image.
#
# The algorithm is transcribed from Pier Solar's own unpacker at ROM
# 0xF830 (rev C), which is a reimplementation of Sega's format. The blob
# and the console's own unpacker are found the way boot/m1emu.S finds
# them: `lea 0x20000,a1` occurs exactly once, the longword before it is
# the blob offset.
import sys, struct

def find_blob(rom):
    for a in range(0, len(rom) - 12, 2):
        if rom[a:a+6] == b'\x43\xf9\x00\x02\x00\x00' \
           and rom[a-6:a-4] == b'\x41\xf9' \
           and rom[a+6:a+8] == b'\x61\x00':
            return struct.unpack('>I', rom[a-4:a])[0], a
    return None, None

def unpack(src, at):
    out = bytearray()
    p = at
    def rd():
        nonlocal p
        v = src[p]; p += 1; return v
    bits = src[p] | (src[p+1] << 8); p += 2
    n = 16
    def getbit():
        nonlocal bits, n, p
        b = bits & 1
        bits >>= 1
        n -= 1
        if n <= 0:
            bits = src[p] | (src[p+1] << 8); p += 2
            n = 16
        return b
    while True:
        if getbit():
            out.append(rd())                    # literal
            continue
        if getbit():                            # long match
            lo = rd(); d1 = rd()
            off = lo + (((d1 & 0xF8) - 256) << 5)
            if d1 & 7:
                cnt = (d1 & 7) + 1
            else:
                d1 = rd()
                if d1 == 0:
                    break
                if d1 == 1:
                    continue
                cnt = d1
        else:                                   # short match
            b1 = getbit(); b2 = getbit()
            cnt = 2 * b1 + 1 + b2
            off = rd() - 256
        for _ in range(cnt + 1):
            out.append(out[len(out) + off])
    return bytes(out)

rom = open(sys.argv[1], 'rb').read()
blob, hit = find_blob(rom)
print(f"signature at 0x{hit:X}, blob at 0x{blob:X}", file=sys.stderr)
d = unpack(rom, blob)
print(f"unpacked {len(d)} bytes", file=sys.stderr)
open(sys.argv[2], 'wb').write(d)
