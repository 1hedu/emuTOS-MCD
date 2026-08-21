#!/usr/bin/env python3
"""Wrap a linked ELF as a GEMDOS .PRG.

The program is built with -mpcrel so every reference is PC-relative and
survives being loaded anywhere; this checks that, because a stray
absolute reference would need a relocation table we do not emit and
would fail silently at some later address.
"""
import struct, subprocess, sys

elf, out = sys.argv[1], sys.argv[2]
objdump = sys.argv[3] if len(sys.argv) > 3 else "m68k-elf-objdump"

sizes = {}
for line in subprocess.run([objdump, "-h", elf], capture_output=True,
                           text=True, check=True).stdout.splitlines():
    f = line.split()
    if len(f) > 3 and f[0].isdigit():
        sizes[f[1]] = int(f[2], 16)

rel = subprocess.run([objdump, "-r", elf], capture_output=True,
                     text=True, check=True).stdout
for line in rel.splitlines():
    if line.startswith("RELOCATION RECORDS") and "empty" not in line:
        sys.exit("mkprg: %s still has relocations; build it with -mpcrel" % elf)

# Absolute jsr/jmp would jump to a link-time address once TOS loads the
# program somewhere else, and there is no relocation table to fix it.
# This is exactly how linking libgcc breaks a -mpcrel program: its
# __umodsi3 reaches __udivsi3 with "4eb9 <abs.l>".
dis = subprocess.run([objdump, "-d", elf], capture_output=True,
                     text=True, check=True).stdout
for line in dis.splitlines():
    for op, name in (("4eb9", "jsr"), ("4ef9", "jmp"),
                     ("4eb8", "jsr.w"), ("4ef8", "jmp.w")):
        if ("\t%s " % op) in line:
            sys.exit("mkprg: %s: absolute %s, which will not survive being "
                     "loaded:\n  %s" % (elf, name, line.strip()))

subprocess.run([objdump.replace("objdump", "objcopy"), "-O", "binary",
                elf, out + ".bin"], check=True)
body = open(out + ".bin", "rb").read()

text = sizes.get(".text", 0)
data = sizes.get(".data", 0)
bss = sizes.get(".bss", 0)
if len(body) != text + data:
    sys.exit("mkprg: binary is %d bytes, sections say %d" % (len(body), text + data))
# A 68000 takes an address error on a word or long access to an odd
# address, and neither emulator this project uses implements that -- so
# a misaligned buffer runs perfectly in testing and kills the console.
#
# Two checks, because the first one was not the bug. Section lengths
# keep .data and .bss starting even; that was worth doing and did not
# help, because what actually crashed CDTEST.PRG was a one-byte `char`
# at the front of .bss putting every array after it on an odd address.
# GEMDOS wrote the DTA and faulted on the caller's behalf.
#
# So: every symbol in .bss and .data has to be even too. Anything the
# operating system writes into is the real hazard and a char array is
# where it hides, since its natural alignment is 1.
for name, n in (("text", text), ("data", data)):
    if n % 4:
        sys.exit("mkprg: %s is %d bytes, which leaves the next section "
                 "misaligned -- see tools/prg.ld" % (name, n))

odd = []
for line in subprocess.run([objdump.replace("objdump", "nm"), "-n", elf],
                           capture_output=True, text=True,
                           check=True).stdout.splitlines():
    f = line.split()
    if len(f) == 3 and f[1].lower() in ("b", "d") and int(f[0], 16) % 2:
        odd.append("%s at 0x%s" % (f[2], f[0]))
if odd:
    sys.exit("mkprg: these live at odd addresses and a 68000 will take "
             "an address error on any word access to them:\n  "
             + "\n  ".join(odd)
             + "\nMark buffers handed to the OS with OSBUF.")

with open(out, "wb") as f:
    f.write(struct.pack(">HLLLLLLH", 0x601A, text, data, bss, 0, 0, 0, 0))
    f.write(body)
    f.write(b"\0\0\0\0")            # empty relocation table
print("%s: text %d data %d bss %d" % (out, text, data, bss))
