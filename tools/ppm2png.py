#!/usr/bin/env python3
"""Turn the harness's frame.ppm into a PNG, so a frame can be looked at
rather than reduced to a colour count. Pure stdlib: the container has
no PIL, and a minimal encoder is shorter than arguing with that."""
import sys, zlib, struct

def chunk(tag, data):
    c = tag + data
    return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c))

src, dst = sys.argv[1], sys.argv[2]
d = open(src, 'rb').read()
if not d.startswith(b'P6'):
    sys.exit("not a binary PPM")
tok, i = [], 2
while len(tok) < 3:                     # width, height, maxval
    while d[i:i+1].isspace(): i += 1
    if d[i:i+1] == b'#':
        while d[i:i+1] not in (b'\n', b''): i += 1
        continue
    j = i
    while not d[j:j+1].isspace(): j += 1
    tok.append(int(d[i:j])); i = j
i += 1
w, h, _ = tok
px = d[i:i + w * h * 3]
raw = b''.join(b'\x00' + px[y*w*3:(y+1)*w*3] for y in range(h))
open(dst, 'wb').write(
    b'\x89PNG\r\n\x1a\n'
    + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    + chunk(b'IDAT', zlib.compress(raw, 9))
    + chunk(b'IEND', b''))
print("%s  %dx%d" % (dst, w, h))
