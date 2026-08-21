# Ghidra headless pre-script: give the analyser somewhere to start.
#
# subbios.bin is a raw blob with no header, so auto-analysis finds
# almost nothing and most of the image is never disassembled at all --
# which makes any "no references found" result meaningless rather than
# informative.
#
# Two things make the image tractable:
#
#  1. The jump table at $5F00-$5FA0: a run of `jmp abs.l` entries covering
#     the BIOS entry points and the interrupt vectors. Every one is a
#     genuine function start.
#
#  2. CDBIOS itself, at $5F22 -> $2E34. It splits on bit 7 of the function
#     number: functions >= $80 go through a `jmp $2E60(pc,d0.w)` table of
#     25 `bra.w` entries (so $80..$98), and functions < $80 are queued for
#     the drive state machine at $2F28. Decoding the first table gives a
#     name for every synchronous BIOS call, which is what turns the
#     decompiler output from anonymous FUN_0000xxxx into something that
#     can be checked against the documented API.
#
# @category MegaCD
from ghidra.program.model.symbol import SourceType

# The drive-command state machine is reached only through a saved PC in
# $5AFE(a5), which the analyser cannot follow. Aggressive instruction
# finding is the standard treatment for a raw image and is what makes
# that code visible at all.
for opt in ("Aggressive Instruction Finder",
            "Decompiler Switch Analysis",
            "Non-Returning Functions - Discovered"):
    try:
        setAnalysisOption(currentProgram, opt, "true")
    except Exception as e:
        print("option %s: %s" % (opt, e))

af = currentProgram.getAddressFactory().getDefaultAddressSpace()
mem = currentProgram.getMemory()

def rb(off):
    return mem.getByte(af.getAddress(off)) & 0xFF

def rw(off):
    return (rb(off) << 8) | rb(off + 1)

def rws(off):
    v = rw(off)
    return v - 0x10000 if v & 0x8000 else v

def rl(off):
    return (rw(off) << 16) | rw(off + 2)

# name -> address, filled in below.
seeds = {}

def seed(addr, name):
    # First name wins: the dispatch-table names are more informative than
    # the generic sub_xxxx ones, and are added first.
    if 0x200 <= addr < 0x6000:
        seeds.setdefault(addr, name)

# ---------------------------------------------------------------- table 1
# $5F00-$5FA0, `jmp abs.l`.
for off in range(0x5F00, 0x5FA2, 2):
    if rw(off) == 0x4EF9:
        seed(rl(off + 2), "vec_%04X" % off)

# ---------------------------------------------------------------- table 2
# CDBIOS functions $80..$98: `bra.w` entries at $2E60. The names are the
# documented CDBIOS API in function-number order; two of them can be
# checked against the code without trusting the list, which is why the
# order is worth relying on. $80 CDBCHK returns bit 7 of the busy word at
# $5AF2(a5) and nothing else, and $81 CDBSTAT fills the status block at
# $5E80 -- both exactly as documented.
API = ["CDBCHK", "CDBSTAT", "CDBTOCWRITE", "CDBTOCREAD", "CDBPAUSE",
       "FDRSET", "FDRCHG",
       "CDCSTART", "CDCSTARTP", "CDCSTOP", "CDCSTAT", "CDCREAD",
       "CDCTRN", "CDCACK",
       "SCDINIT", "SCDSTART", "SCDSTOP", "SCDSTAT", "SCDREAD",
       "SCDPQ", "SCDPQL",
       "bios_95", "bios_96", "bios_97", "bios_98"]

for i, name in enumerate(API):
    off = 0x2E60 + i * 4
    if rw(off) != 0x6000:
        print("dispatch entry %d is not a bra.w -- table decode is wrong" % i)
        break
    seed(off + 2 + rws(off + 2), "bios%02X_%s" % (0x80 + i, name))

# ------------------------------------------------------------- coroutine
# Functions below $80 are asynchronous: $2F28 stores the command word and
# its arguments at $5AF2(a5), sets the busy bit, and returns. The work is
# done by a coroutine that yields with `bsr $2F6C` (which pops the return
# address into $5AFE(a5)) and is resumed from $2F7C by `jmp (a0)` on the
# saved address. Every yield point is therefore an entry point that no
# static analyser can find by following calls, so seed the resume targets
# by scanning for the yield sequence.
seed(0x2F28, "bios_queue_command")
seed(0x2F6C, "coro_yield")
seed(0x2F72, "coro_resume")
seed(0x2F8A, "coro_main")

# `bsr.b $2F6C` / `bsr.w $2F6C`: the instruction after one is a resume
# target. Seeding those keeps the disassembler from running off the rails
# at the yield and losing the rest of the routine.
resumes = []
for off in range(0x200, 0x6000, 2):
    op = rw(off)
    tgt = None
    if (op & 0xFF00) == 0x6100 and (op & 0xFF) not in (0x00, 0xFF):
        d = op & 0xFF
        tgt = off + 2 + (d - 0x100 if d & 0x80 else d)
    elif op == 0x6100:
        tgt = off + 2 + rws(off + 2)
    if tgt == 0x2F6C:
        resumes.append(off)
print("found %d yield sites" % len(resumes))

# Entry points established while reverse-engineering the CDD, so that the
# analyser reaches the same code by a second route.
for a in (0x03D4, 0x05FA, 0x060A, 0x0628, 0x064C, 0x0660,
          0x1334, 0x1390, 0x13FE, 0x140E, 0x1568, 0x17B6,
          0x198A, 0x19F0, 0x1F0A):
    seed(a, "sub_%04X" % a)

print("seeding %d entry points" % len(seeds))
for s in sorted(seeds):
    addr = af.getAddress(s)
    try:
        disassemble(addr)
        createFunction(addr, seeds[s])
    except Exception as e:
        print("  %04X %s: %s" % (s, seeds[s], e))

# Force the body of the coroutine to be disassembled even where control
# flow cannot reach it, so its CDC accesses show up in the survey.
for off in resumes:
    try:
        disassemble(af.getAddress(off + 2))
    except Exception:
        pass
