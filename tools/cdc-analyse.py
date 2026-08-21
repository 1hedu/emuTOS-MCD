# Ghidra headless script: recover the Mega CD sub-CPU CDBIOS CDC driver.
#
# Companion to cdd-analyse.py. Where that one was pointed at known
# addresses, this one finds its own targets: the CDC lives behind a
# handful of gate-array registers, so every function that touches one is
# by definition part of the driver. Letting the tool find the function
# boundaries matters -- picking them by eye is how the CDD work produced
# fiction out of a linear disassembler.
#
# The registers are listed by address only. What each one does is a
# conclusion to be drawn from how the BIOS uses it, not an assumption to
# feed into reading it.
#
# @category MegaCD
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

# Absolute-short addressing sign-extends, so `$8005.w` on the sub CPU is
# $FFFF8005 as far as the disassembler is concerned. Byte accesses land on
# the odd half, so both halves of each word are listed.
LO, HI = 0xFFFF8000, 0xFFFF803F
SKIP = set()

af = currentProgram.getAddressFactory().getDefaultAddressSpace()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
monitor = ConsoleTaskMonitor()

# Which functions touch a gate-array register, and which ones each uses.
users = {}
for ins in listing.getInstructions(True):
    for i in range(ins.getNumOperands()):
        for op in ins.getOpObjects(i):
            try:
                v = op.getOffset() & 0xFFFFFFFF
            except Exception:
                continue
            if LO <= v <= HI and v not in SKIP:
                f = fm.getFunctionContaining(ins.getAddress())
                key = f.getEntryPoint().getOffset() if f else -1
                users.setdefault(key, set()).add(v)

def regs(entry):
    return " ".join("%04X" % (v & 0xFFFF) for v in sorted(users[entry]))

print("=" * 70)
print("Functions touching gate-array registers $FF8000-$FF803F")
print("=" * 70)
for entry in sorted(users):
    f = fm.getFunctionAt(af.getAddress(entry)) if entry >= 0 else None
    name = f.getName() if f else "(outside any function)"
    print("  %-24s %s" % (name, regs(entry)))

decomp = DecompInterface()
decomp.openProgram(currentProgram)

for entry in sorted(users):
    if entry < 0:
        continue
    f = fm.getFunctionAt(af.getAddress(entry))
    print("")
    print("=" * 70)
    print("%04X %s   uses: %s" % (entry, f.getName(), regs(entry)))
    print("=" * 70)
    res = decomp.decompileFunction(f, 60, monitor)
    if res.decompileCompleted():
        print(res.getDecompiledFunction().getC())
    else:
        for ins in listing.getInstructions(f.getBody(), True):
            print("  %04X  %s" % (ins.getAddress().getOffset(), ins))
