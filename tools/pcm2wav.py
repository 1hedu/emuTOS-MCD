#!/usr/bin/env python3
"""Put a WAV header on the raw stereo S16LE the trace core writes.

    GPGX_WAV=out.pcm .emu/libretro-harness ...
    tools/pcm2wav.py out.pcm out.wav

44100 Hz because that is SOUND_FREQUENCY in the core's libretro.c.
Also prints where the sound is, which is usually the only question:
peak amplitude per tenth of a second, so a silent run is obvious
without opening the file.
"""
import struct, sys

RATE = 44100
src, dst = sys.argv[1], sys.argv[2]
pcm = open(src, 'rb').read()
n = len(pcm) // 4
with open(dst, 'wb') as f:
    f.write(b'RIFF' + struct.pack('<I', 36 + n * 4) + b'WAVEfmt ')
    f.write(struct.pack('<IHHIIHH', 16, 1, 2, RATE, RATE * 4, 4, 16))
    f.write(b'data' + struct.pack('<I', n * 4) + pcm[:n * 4])

step = RATE // 10
loud = []
for i in range(0, n, step):
    chunk = pcm[i * 4:(i + step) * 4]
    if not chunk:
        break
    peak = max(abs(v) for v in struct.unpack('<%dh' % (len(chunk) // 2), chunk))
    if peak > 200:
        loud.append((i / RATE, peak))
print('%s: %.2f s, %d loud tenths' % (dst, n / RATE, len(loud)))
for t, p in loud[:60]:
    print('   %6.2f s  peak %5d' % (t, p))
