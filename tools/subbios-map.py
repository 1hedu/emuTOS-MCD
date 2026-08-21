#!/usr/bin/env python3
"""Map the Mega CD sub-CPU BIOS: every entry point, named.

Everything this project has learned about the CDBIOS was learned one
routine at a time, chasing one symptom at a time -- the CTRL ladder, the
coroutine, the three blobs. Each of those was visible from the dispatch
tables all along. This prints them.

Usage: tools/subbios-map.py vendor/bios/bios_CD_U.bin
"""
import sys, struct, subprocess, os, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

# Sega's published function numbers. Only the >= 0x80 half is a jump
# table; see the note this script prints about the rest.
CDC = {
    0x80: 'CDBCHK',   0x81: 'CDBSTAT', 0x82: 'CDBTOCWRITE', 0x83: 'CDBTOCREAD',
    0x84: 'CDBPAUSE', 0x85: 'FDRSET',  0x86: 'FDRCHG',      0x87: 'CDCSTART',
    0x88: 'CDCSTARTP',0x89: 'CDCSTOP', 0x8A: 'CDCSTAT',     0x8B: 'CDCREAD',
    0x8C: 'CDCTRN',   0x8D: 'CDCACK',  0x8E: 'SCDINIT',     0x8F: 'SCDSTART',
    0x90: 'SCDSTOP',  0x91: 'SCDSTAT', 0x92: 'SCDREAD',     0x93: 'SCDPQ',
    0x94: 'SCDPQL',   0x95: 'LEDSET',  0x96: 'CDCSETMODE',  0x97: 'WONDERREQ',
    0x98: 'WONDERCHK',
}
DRIVE = {
    0x00: 'MSCSTOP', 0x02: 'MSCSTOP', 0x03: 'MSCPAUSEON', 0x04: 'MSCPAUSEOFF',
    0x05: 'MSCSCANFF', 0x06: 'MSCSCANFR', 0x07: 'MSCSCANOFF', 0x08: 'ROMPAUSEON',
    0x09: 'ROMPAUSEOFF', 0x0A: 'DRVOPEN', 0x0B: 'DRVINIT',
    0x10: 'DRVINIT', 0x11: 'DRVOPEN', 0x20: 'ROMREAD', 0x21: 'ROMSEEK',
    0x22: 'MSCSEEK', 0x23: 'MSCSEEK1', 0x24: 'ROMREADN', 0x25: 'ROMREADE',
}

def unpack_bios(path):
    out = tempfile.NamedTemporaryFile(suffix='.bin', delete=False)
    out.close()
    subprocess.run([sys.executable, os.path.join(HERE, 'subbios.py'), path,
                    out.name], check=True, stderr=subprocess.DEVNULL)
    d = open(out.name, 'rb').read()
    os.unlink(out.name)
    return d

def find_dispatcher(d):
    """blob 1 installs its own entry vector: `lea 0x5f22,a0 / lea <disp>,a1`."""
    i = d.find(b'\x41\xf8\x5f\x22')
    if i < 0 or d[i+4:i+6] != b'\x43\xf9':
        return None
    return struct.unpack('>I', d[i+6:i+10])[0]

def main():
    d = unpack_bios(sys.argv[1])
    disp = find_dispatcher(d)
    print("%s: %d bytes unpacked, dispatcher at 0x%04X"
          % (os.path.basename(sys.argv[1]), len(d), disp))

    # The dispatcher splits on bit 7 and reaches a pc-relative jump table.
    j = d.find(b'\x4e\xfb\x00\x02', disp, disp + 0x60)
    tab = j + 4
    print("\nsynchronous CDC family -- jump table at 0x%04X" % tab)
    for i in range(25):
        a = tab + 4 * i
        disp16 = struct.unpack('>h', d[a+2:a+4])[0]
        print("  0x%02X %-12s -> 0x%04X" % (0x80+i, CDC.get(0x80+i, '?'),
                                            a + 2 + disp16))

    print("""
asynchronous drive commands -- everything below 0x80

These are not executed when you call them. The dispatcher writes the
function number and its arguments into a command block at a5+0x5AF2,
sets bit 7 to mark it pending, and returns. The drive's own exchange
picks it up. How many arguments are copied depends on the high nibble:
0x0n takes none, 0x1n one longword from (a0), 0x2n and up two.

  0x10 DRVINIT   { first track, last track }
  0x20 ROMREAD   { LBA, sector count }

which is why a read is ROMREAD once and then CDCSTAT (0x8A, and 0x8A is
in the synchronous half) polled until the carry clears. ROMREAD posts;
it does not read.""")

    # ...and the table itself, which the prose above described without
    # ever printing. 44 entries; several share one reject stub, so the
    # family is smaller than its numbering suggests. Naming them is not
    # done: an address with no citation is printed as an address.
    base = disp + 0x1C0                 # 0x2FD8 for the US dispatcher
    if d[base:base+2] == b'\x60\x00':
        stub = base + 4*0x0D + 2 + struct.unpack('>h', d[base+4*0x0D+2:base+4*0x0D+4])[0]
        print("\nasynchronous command table at 0x%04X" % base)
        for i in range(44):
            tgt = base + 4*i + 2 + struct.unpack('>h', d[base+4*i+2:base+4*i+4])[0]
            n = {0: 0, 1: 1}.get(i >> 4, 2)
            note = {0x10: "DRVINIT  -- CDCSTOP then yields via 0x2F54",
                    0x20: "ROMREAD"}.get(i, "")
            if tgt == stub and not note:
                note = "(unimplemented: shares the reject stub)"
            print("  0x%02X -> 0x%04X  %d arg%-2s %s"
                  % (i, tgt, n, "" if n == 1 else "s", note))

if __name__ == '__main__':
    main()
