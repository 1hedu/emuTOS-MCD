#!/usr/bin/env python3
"""Convert a binary P6 PPM to RGB PNG using only the Python standard library."""
from pathlib import Path
import argparse
import struct
import zlib

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("destination", type=Path)
args = parser.parse_args()

data = args.source.read_bytes()
pos = 0

def token() -> bytes:
    global pos
    while pos < len(data):
        if data[pos:pos+1] == b"#":
            pos = data.find(b"\n", pos)
            if pos < 0:
                raise ValueError("unterminated PPM comment")
        elif data[pos] in b" \t\r\n":
            pos += 1
        else:
            break
    start = pos
    while pos < len(data) and data[pos] not in b" \t\r\n":
        pos += 1
    return data[start:pos]

if token() != b"P6":
    raise SystemExit("only binary P6 PPM is supported")
width = int(token())
height = int(token())
maximum = int(token())
if maximum != 255:
    raise SystemExit("only 8-bit PPM is supported")
while pos < len(data) and data[pos] in b" \t\r\n":
    pos += 1
pixels = data[pos:]
if len(pixels) != width * height * 3:
    raise SystemExit(f"pixel length mismatch: {len(pixels)}")
scanlines = b"".join(b"\0" + pixels[y*width*3:(y+1)*width*3] for y in range(height))

def chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(scanlines, 9))
png += chunk(b"IEND", b"")
args.destination.parent.mkdir(parents=True, exist_ok=True)
args.destination.write_bytes(png)
print(f"{args.destination}: {width}x{height}")
