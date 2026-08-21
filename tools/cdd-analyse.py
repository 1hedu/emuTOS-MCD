# Ghidra headless script: recover the Mega CD sub-CPU CDBIOS CDD driver.
#
# Run via analyzeHeadless. objdump -b binary is a *linear* disassembler:
# it has no idea where an instruction starts, so a single misaligned byte
# turns the next several lines into fiction. Several of the claims in
# docs/cdd-bios-notes.md were read off exactly that kind of output, so
# they get re-derived here by a tool that follows control flow.
#
# @category MegaCD
from ghidra.program.model.symbol import SourceType
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

ENTRIES = {
    0x0628: "cdd_int4_handler",
    0x1334: "cdd_exchange",
    0x1390: "cdd_wait_read_ack",
    0x13FE: "cdd_write_command",
    0x140E: "cdd_status_action",
    0x1568: "cdd_status_dispatch",
    0x17B6: "cdd_command_state",
    0x198A: "cdd_build_packet",
    0x19F0: "cdd_retry",
    0x03D4: "cdd_init",
}

fm = currentProgram.getFunctionManager()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
monitor = ConsoleTaskMonitor()

for off, name in sorted(ENTRIES.items()):
    addr = af.getAddress(off)
    createFunction(addr, name)
    f = fm.getFunctionAt(addr)
    if f is not None:
        f.setName(name, SourceType.USER_DEFINED)

decomp = DecompInterface()
decomp.openProgram(currentProgram)

for off, name in sorted(ENTRIES.items()):
    addr = af.getAddress(off)
    f = fm.getFunctionAt(addr)
    print("=" * 70)
    print("%s  @ 0x%04X" % (name, off))
    print("=" * 70)
    if f is None:
        print("  (no function recovered)")
        continue
    listing = currentProgram.getListing()
    for ins in listing.getInstructions(f.getBody(), True):
        print("  %04X  %-22s %s" % (ins.getAddress().getOffset(),
                                    ins.getMnemonicString() + " " +
                                    ",".join(str(ins.getDefaultOperandRepresentation(i))
                                             for i in range(ins.getNumOperands())),
                                    ""))
    res = decomp.decompileFunction(f, 60, monitor)
    if res.decompileCompleted():
        print("--- decompiled ---")
        print(res.getDecompiledFunction().getC())
