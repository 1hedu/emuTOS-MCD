# Ghidra headless post-script: how the BIOS lays out a CDD command.
#
# Stage 2 proved the transport (nine nibbles plus a checksum, one nibble
# per byte, written to $FF8042). Stage 3 needs the *content* of the one
# command that matters for reading data: which nibble is the command
# code, which nibbles carry the target MSF, and in what order.
#
# The BIOS builds each command as an eight-byte image at $584C(a5) and
# $198A copies it out with the checksum appended. Every constant-valued
# command can be read straight off the `move.l #imm` that builds it, but
# the one carrying an MSF is filled in by $1098, which is a chain of
# shifts that is very easy to simulate incorrectly by hand -- so let the
# decompiler state it instead.
#
# @category MegaCD
from ghidra.program.model.symbol import SourceType
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

ENTRIES = {
    0x1098: "cdd_msf_to_nibbles",
    0x116C: "toc_lookup_track",
    0x1C58: "drive_state2_prepare",
    0x1B42: "cdd_post_command",
    0x1B5E: "drive_state_dispatch",
    0x0806: "lba_to_msf_a",
    0x0846: "lba_to_msf_b",
    0x0832: "sub_0832",
    0x07C2: "cdc_ring_next",
    0x3AEE: "coro_ROMREAD",
    0x3B56: "coro_start_cdc",
    0x200A: "cdc_start_body",
}

fm = currentProgram.getFunctionManager()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
listing = currentProgram.getListing()
monitor = ConsoleTaskMonitor()

for off, name in sorted(ENTRIES.items()):
    addr = af.getAddress(off)
    try:
        disassemble(addr)
        createFunction(addr, name)
    except Exception as e:
        print("%04X: %s" % (off, e))
    f = fm.getFunctionAt(addr)
    if f is not None:
        f.setName(name, SourceType.USER_DEFINED)

decomp = DecompInterface()
decomp.openProgram(currentProgram)

for off, name in sorted(ENTRIES.items()):
    f = fm.getFunctionAt(af.getAddress(off))
    print("")
    print("=" * 70)
    print("%04X  %s" % (off, name))
    print("=" * 70)
    if f is None:
        print("  no function")
        continue
    res = decomp.decompileFunction(f, 60, monitor)
    if res.decompileCompleted():
        print(res.getDecompiledFunction().getC())
    else:
        for ins in listing.getInstructions(f.getBody(), True):
            print("  %04X  %s" % (ins.getAddress().getOffset(), ins))
