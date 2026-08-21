# The CDD exchange, read out of the sub-CPU CDBIOS

Notes from disassembling Sega's own CD-drive driver, because five discs
of guessing at it was five too many.

**Verified with Ghidra and radare2.** The first pass used
`objdump -b binary`, which is a *linear* disassembler: it has no idea
where an instruction begins, so one misaligned byte turns the following
lines into fiction — and it visibly did, several times, in the same
output these notes were read from. Given how many of my readings had
already turned out wrong, every claim below was re-derived with a
recursive-descent disassembler that follows control flow, plus Ghidra's
decompiler. `tools/cdd-analyse.py` reproduces it:

```
analyzeHeadless <proj> cdd -import subbios.bin \
  -processor 68000:BE:32:MC68030 -loader BinaryLoader -loader-baseAddr 0x0 \
  -postScript tools/cdd-analyse.py
```

Everything held. The listings here are the objdump ones because they
are more compact, but each has been checked against the decompiler
output, and the decompilation of the two routines that matter most is
quoted below.

## Getting at it

The sub-CPU CDBIOS is **compressed** inside the console's BIOS ROM —
its header string is visibly interleaved with garbage in a hex dump, so
there is nothing to disassemble there. The boot process decompresses it
into PRG-RAM `$0000–$5FFF`, and this project already had an audit
payload that snapshots exactly that region into Genesis work RAM, where
the test harness dumps it to a file:

```
AUDIT=1 tools/build-iso.sh J
tools/run-emu.sh gpgx build/emutosmd-audit.cue 1800     # needs --input
# un-byteswap the dump; snapshot A is at offset 0x1000, 24 KB
m68k-elf-objdump -D -b binary -m m68k:68000 subbios.bin
```

The BIOS addresses the gate array with **absolute-short** addressing —
`$8042.w` sign-extends to `$FFFF8042`, which is `$FF8042` on a 24-bit
bus. Searching for absolute-long references finds almost nothing; this
is why the code looked absent at first.

`a5` is 0 throughout, so `a5@(22580)` is simply `$5834`. The BIOS keeps
its state at `$5834–$587x`.

## Map

| Address | What |
|---|---|
| `$5F88` | level-4 vector slot → `$0628` |
| `$0628` | CDD interrupt handler; calls `$1334` then four other subsystems |
| `$1334` | the exchange |
| `$1390` | wait, read status, acknowledge |
| `$13FE` | write the ten command bytes |
| `$1568` | dispatch on status nibble 1 (software only) |
| `$17B6` | command state machine |
| `$198E` | build a command packet and its checksum |
| `$5834` | flags: bit 0 reentrancy, bit 7 CDD enabled |
| `$5864` | ten-byte command buffer |
| `$586E` | ten-byte status buffer |

## The exchange (`$1334`)

```
bset  #0,$5834          reentrancy guard; already inside -> leave
btst  #7,$5834          CDD enabled at all?
bsr   $1568             process the previous status packet
bsr   $1390             wait, read, acknowledge
bcs   out               it timed out
                        sum all ten status bytes, NOT, AND #$0F
bne   out               must come out zero
bsr   $140E             act on the status
bsr   $1568             dispatch on it
bsr   $17B6             build the next command
bsr   $13FE             write it
```

A command goes out on **every** exchange — 75 times a second — with a
NOP when there is nothing to ask.

## Wait, read, acknowledge (`$1390`)

```
move.w #256,d0
btst   #1,$FF8037       DRS
dbeq   d0,-             loops while DRS is SET
bne    timeout          still set after 256 tries
lea    $586E,a0
lea    $FF8038,a1
move.l (a1)+,(a0)+      ten status bytes as long, long, word
move.l (a1)+,(a0)+
move.w (a1)+,(a0)+
...
btst   #0,$FF8037       DTS
beq    skip             clear -> do NOT write
move.b #4,$FF8037       set -> acknowledge
```

Three things here are easy to get backwards, and I got all three wrong
at least once:

- **`dbeq` waits for DRS to go LOW.** `DBcc` exits when its condition is
  *true*, and `btst` sets `Z` when the bit is *zero*. Read quickly it
  looks like the opposite.
- **The acknowledgement is conditional on DTS.** Writing `#4` also puts
  zero into DRS, and zero written to DRS *aborts an incoming transfer*.
  Doing it unconditionally tears down packets that were about to
  arrive.
- **It happens before the command is loaded,** not after.

## Writing a command (`$13FE`)

```
lea    $5864,a0
lea    $FF8042,a1
move.l (a0)+,(a1)+      ten bytes, long, long, word
move.l (a0)+,(a1)+
move.w (a0)+,(a1)+
```

That is all. **There is no trigger among the command registers** — five
discs were spent looking for one. The transfer is started by the
control register, not by any particular write to the command block.

## The checksum (`$198E`)

```
clr.b  d0
moveq  #3,d1
add.b  (a0),d0          four iterations of two bytes = eight
move.b (a0)+,(a1)+
add.b  (a0),d0
move.b (a0)+,(a1)+
dbf    d1,-
not.b  d0
andi.w #$0F,d0
move.w d0,(a1)          a WORD: byte 8 = 0, byte 9 = checksum
```

The sum covers **eight** nibbles, not nine — but the word write forces
nibble 8 to zero, so summing eight or nine is the same arithmetic. Our
`~(sum of 0..8) & 0x0F` is equivalent for any command whose nibble 8 is
zero, which is all of them here.

The receive side checks it the other way round: sum all ten, invert,
mask, require zero.

## Init (`$03D4`)

```
btst  #0,$FF8001        wait for bit 0 to become 1
beq   -
ori.b #$14,$FF8033      enable level 2 and level 4
move.b #4,$FF8037       HOCK on
move.b #$80,$5834       mark CDD enabled
move.w #$2000,sr        interrupts on
```

`$FF8001` bit 0 is cleared in the reset path at `$0266` and waited on
here. Not yet accounted for in our driver — by the time EmuTOS starts
the boot has long since passed this point, but it is the one init step
we have not deliberately reproduced.

## Commands are held up, not posted (`$1B42`, `$19F0`)

This is the piece every hardware attempt was missing. Ghidra's
decompilation of `$19F0`, which is reached once per exchange while a
command is outstanding:

```c
cVar1 = *(char *)(unaff_A5 + 0x583d);        /* retry counter    */
*(char *)(unaff_A5 + 0x583d) = cVar1 + -1;   /* spend one        */
if (cVar1 == '\0') {                         /* exhausted:       */
    *(byte *)(unaff_A5 + 0x583b) |= 1;       /*   raise an error */
    pcVar5 = (char *)(unaff_A5 + 0x584c);    /*   and fall back  */
    pcVar5[0] = '\x01';                      /*   to command 1   */
}
/* falls through and rebuilds the packet either way */
```

and of the wait/acknowledge at `$1390`:

```c
do {
    bVar3 = (DAT_ffff8037 & 2) == 0;   /* DRS clear?             */
    if (bVar3) break;                  /* yes -> proceed to read */
    sVar2 = sVar2 + -1;
} while (sVar2 != -1);
...
if ((DAT_ffff8037 & 1) == 0) { flag |= 0x10; }   /* DTS clear: no write */
else                         { DAT_ffff8037 = 4; } /* DTS set: acknowledge */
```

```
$1B42   move.w #1500,$5846      overall timeout
        move.b #75,$583D        retry count -- 75 exchanges, one second
        move.b #2,$583E
        bset   #1,$5836
        bra    $198A            build the packet

$19F0   lea    $5864,a0
        subq.b #1,$583D         each exchange, spend one retry
        bcc    $198A            still some left: rebuild and send it AGAIN
        bset   #0,$583B         exhausted: raise an error
```

The identical command goes out on **every exchange** until the drive
answers it or 75 exchanges have passed. A command is not a message
posted once; it is a request held up until it is taken. Both emulators
accept a single shot, which is exactly why six discs looked correct
here and did nothing on the console.

## The command opcodes the BIOS actually issues

Read off the immediates loaded into the source buffer at `$584C`,
where each byte is one nibble:

| Written | Command |
|---|---|
| `$00000000` | `0` — NOP / get status, the idle command |
| `$01000000` | `1` — stop |
| `$02000004` | `2` sub `4` — TOC: first and last track |
| `$0400....` | `4` — seek, parameters filled in by `$1098` |
| `$06000000` | `6` — pause |
| `$07000000` | `7` — resume |

So the idle command really is `0`, which at least one guess got right.

## What the BIOS never had to worry about

Everything above is about the protocol, and the protocol may never have
been the problem. The CDBIOS ran on a machine where **nothing else was
taking the sub CPU's bus.** This port has the Genesis halting the sub
with SBRQ every frame to read the screen out of PRG-RAM.

A halt inside a CDD exchange is not a delay, it is a corruption: the
gate array clocks a fixed exchange every 1/75 s, and a grab lasting
milliseconds drops the sub out of that window. A status read that
arrives late costs nothing -- which is precisely why the passive link
has been flawless from the first hardware test. A command that arrives
late is destroyed.

The evidence fits nothing else: perfect while passive, dead at the
first command, unchanged by every correction to ordering, width, wait
direction, acknowledgement and retry, and invisible on both emulators,
whose bus arbitration is instant and neither of which models the
transfer window at all.

The servant now asks permission — the sub raises a flag while it is
mid-exchange, and the Genesis backs off and skips that frame's screen
copy. Unproven, but it is the first explanation that covers all of the
evidence rather than some of it.

## Still unexplained

The exchange above is now reproduced faithfully and the round trip is
confirmed on both emulators, but on real hardware the drive has yet to
answer. Remaining candidates, in the order they are worth chasing:

1. `$FF8001` bit 0, above — the one init step not deliberately
   reproduced.
2. Whether an idle NOP must go out on every exchange. The BIOS does it;
   the one build here that copied it stopped checksumming on hardware,
   so it is currently left out until the retry behaviour is proven.
3. The four other subsystems the level-4 handler calls (`$2704`,
   `$2F72`, `$2D5C`, `$68E`), one of which may also touch the drive.

---

# Stage 3: the CDC, in progress

Same method as the CDD: read Sega's driver rather than infer one.
`tools/cdc-seed.py` (pre-script) and `tools/cdc-analyse.py`
(post-script) do it, the first seeding the analyser from the BIOS jump
table at `$5F00-$5FA0` because a raw blob gives it nothing to start
from, the second finding every function that touches a CDC register and
decompiling it.

## Established so far

**Register access.** The CDC's internal registers are reached through a
select/data pair, byte-wide, on the odd halves -- the same shape as the
CDD command block:

| Address | Role |
|---|---|
| `$FF8005` | select which internal CDC register `$FF8007` addresses |
| `$FF8007` | that register's data |
| `$FF8008` | host data: decoded sector words are read from here |
| `$FF800A` | DMA address for hardware transfer |

**`$1FBE` -- stop CDC interrupts**, decompiled:

```c
flags &= 0xfd;                  /* clear a software bit */
DAT_ffff8033 &= 0xdf;           /* mask off level 5 -- the CDC interrupt */
DAT_ffff8005 = 1;               /* select CDC register 1 (IFCTRL) */
DAT_ffff8007 = 0x3a;            /* and write it */
```

**`$1D52`** manages a ring of sector buffers (head and tail at `$5A72`
and `$5A74`) and finishes by writing CDC register 2 with zero.

**Level 5 is the CDC interrupt**, from the vector table: `$5F8E` holds
a `jmp` to `$064C`, which calls `$1F0A`.

## The dispatch table, decoded

The blocker above is gone. `CDBIOS` is `$5F22 -> $2E34`, and it splits
on bit 7 of the function number:

```
bclr  #7,d0
beq   $2F28             below $80: queue it for the drive machine
bsr   $2E52             $80 and up: synchronous, dispatch here
...
$2E52:  add.w d0,d0
        add.w d0,d0
        cmpi.w #$64,d0          25 entries
        bcc   out
        jmp   $2E60(pc,d0.w)
```

Twenty-five `bra.w` entries at `$2E60`, so functions `$80`-`$98`. In
function-number order they are exactly the documented CDBIOS API, and
two of them can be checked against their own code without trusting the
list: `$80` is a routine whose entire body is `btst #7,$5AF2(a5)` /
`sne` / `lsr.b` -- CDBCHK, "is the drive busy" -- and `$81` fills the
status block at `$5E80`, which is CDBSTAT. The rest follow:

| | | | | |
|---|---|---|---|---|
| `$80` CDBCHK `$2EC6` | `$81` CDBSTAT `$2ED2` | `$82` CDBTOCWRITE `$2F0A` | `$83` CDBTOCREAD `$2F14` | `$84` CDBPAUSE `$2F22` |
| `$85` FDRSET `$2C68` | `$86` FDRCHG `$2D10` | `$87` CDCSTART `$1FF4` | `$88` CDCSTARTP `$1FFC` | `$89` CDCSTOP `$1FB6` |
| `$8A` CDCSTAT `$1D28` | `$8B` CDCREAD `$1D52` | `$8C` CDCTRN `$1E08` | `$8D` CDCACK `$1EB4` | `$8E` SCDINIT `$23E6` |
| `$8F` SCDSTART `$2444` | `$90` SCDSTOP `$2438` | `$91` SCDSTAT `$24AA` | `$92` SCDREAD `$250A` | `$93` SCDPQ `$24C0` |
| `$94` SCDPQL `$24BA` | `$95` `$2F1C` | `$96` `$1EF0` | `$97` `$2C40` | `$98` `$2C44` |

Functions below `$80` are asynchronous. `$2F28` copies the function
number and its arguments into `$5AF2(a5)`, sets a busy bit and returns;
the work is done later by a coroutine that yields with `bsr $2F6C`
(which pops its own return address into `$5AFE(a5)`) and is resumed
from `$2F7C` by `jmp (a0)`. How many argument longs get copied is
decided by the *high* nibble of the function byte: none for `$0x`, one
for `$1x`, two for `$2x` and up. So `ROMREAD` (`$20`) is handed a
pointer to `{ LBA, sector count }`.

## The CDC register file

`$FF8005` is an address register and `$FF8007` is the data port, and
**the address auto-increments after each access to `$FF8007`**. That is
what makes `CDCREAD` five consecutive byte writes rather than five
select/write pairs. The register file is the one from the LC8951-family
decoder, and four separate uses in this BIOS agree on it:

| n | read | write | seen as |
|---|---|---|---|
| 1 | IFSTAT | IFCTRL | `$1FBE` writes `$3A`; `$1F0A` reads it |
| 2,3 | DBC | DBC | `CDCREAD` writes the byte count, low then high |
| 4,5 | HEAD0,1 | DAC | `CDCREAD` writes the buffer address; `$1F0A` reads the header |
| 6 | HEAD2 | DTTRG | `CDCREAD`'s fifth write, which starts the transfer |
| 7 | HEAD3 | DTACK | `CDCACK` is nothing but `AR=7; data=0` |
| 8,9 | PTL,PTH | WA | `$21C8` zeroes them at reset; `$1F0A` reads them |
| A,B | WA | CTRL0,1 | four pairs, see below -- this row used to name two |
| C,D | STAT0,1 | PT | `$21C8` zeroes them at reset |
| E,F | STAT2,3 | CTRL2, RESET | `$21C8` opens with `AR=$F; data=0` |

`$FF8004` is the gate array's own byte, not a CDC register: bit 7 is
end-of-transfer and bit 6 is data-ready -- `CDCTRN` waits on one, then
the other -- and the low three bits are the transfer destination.
`$4146` sets it to 3, sub-CPU read, immediately before `CDCREAD`.

## Init, and the values to write

```
$21C8   AR=$F  <- 0            reset the decoder
        AR=1   <- $3A          IFCTRL
        AR=8   <- 0, 0         WA = 0
        AR=$C  <- 0, 0         PT = 0
$21F2   AR=$A  <- $A0, $F8     CTRL0, CTRL1
```

### The dispatcher, and why ROMREAD does not read

`tools/subbios-map.py` prints this from any boot ROM. It should have
been the first thing written rather than the last: the CTRL ladder, the
coroutine and the three blobs were all visible from the dispatch tables,
and each was instead found one at a time by chasing a symptom.

The entry at `$5F22` is a stub the firmware's own startup installs,
pointing at the dispatcher (US/EU `0x2E18`, JP `0x2E30`). It splits on
bit 7 of the function number:

**`>= $80` is synchronous** -- `bclr #7,d0`, scale by four, bounds-check
against 100, and `jmp` a 25-entry pc-relative table. US addresses:

| fn | name | | fn | name | | fn | name |
|---|---|---|---|---|---|---|---|
| `$80` | CDBCHK `2EAE` | | `$87` | CDCSTART `1FDC` | | `$8E` | SCDINIT `23CE` |
| `$81` | CDBSTAT `2EBA` | | `$88` | CDCSTARTP `1FE4` | | `$8F` | SCDSTART `242C` |
| `$82` | CDBTOCWRITE `2EF2` | | `$89` | CDCSTOP `1F9E` | | `$95` | LEDSET `2F04` |
| `$83` | CDBTOCREAD `2EFC` | | `$8A` | CDCSTAT `1D10` | | `$96` | CDCSETMODE `1ED8` |
| `$84` | CDBPAUSE `2F0A` | | `$8B` | CDCREAD `1D3A` | | `$97` | WONDERREQ `2C28` |
| `$85` | FDRSET `2C50` | | `$8C` | CDCTRN `1DF0` | | `$98` | WONDERCHK `2C2C` |
| `$86` | FDRCHG `2CF8` | | `$8D` | CDCACK `1E9C` | | | |

**`< $80` is a queue.** The dispatcher does not execute those at all. It
writes the function number and its arguments into a command block at
`a5+$5AF2`, sets bit 7 to mark it pending, and returns:

```
$2F30   lea $5AF2(a5),a1 / move.w d0,(a1)+ / move.l (a0)+,(a1)+
                                           / move.l (a0)+,(a1)+   fn >= $20
$2F3C   lea $5AF2(a5),a1 / move.w d0,(a1)+ / move.l (a0)+,(a1)+   fn $10-$1F
$2F46   lea $5AF2(a5),a1 / move.w d0,(a1)+                        fn $00-$0F
$2F4C   bset #7,$5AF2(a5) / rts
```

The drive's own exchange picks the request up later. So `ROMREAD` posts
a request and returns; it does not read. That is why the read recipe is
`ROMREAD` once and then `CDCSTAT` -- which is in the *synchronous* half
-- polled until the carry clears, and it is the same fact `$1F3E`
expresses from the other side.

Two halves, then: an asynchronous command queue clocked by the drive,
and a synchronous CDC register family. This driver was written as
though the whole thing were synchronous.

### Two shipping Mode 1 programs, neither reimplements the CDC

**Doom CD32X Fusion v3.0** (`SEGA 32X`, 4 MB cart + a 22 MB ISO) is the
closest reference there is: a 32X cartridge that boots with a disc in
the tray and browses its filesystem. What it does:

* Unpacks the console's sub-CDBIOS out of the boot ROM -- the same
  blobs Pier Solar uses, `0x415800` and `0x41AD00`, selected from a
  version table and unpacked through a function pointer.
* Zeroes the 128 KB PRG window, then `$FF00` to `$A12002` and `3`,
  `2`, `0` to `$A12001` -- Pier Solar's sequence, and the BIOS's own.
* Uploads a sub program at `0x6000` with the header
  `"MAIN-SUBCPU"`, version `0x0001`, jump table at `+0x20`.
* Does **every** CD operation through `jsr $5F22`: DRVINIT, ROMREAD,
  CDBSTAT, CDBTOCREAD, FDRSET, MSCSTOP/PAUSE, and the whole CDC family
  CDCSTART / CDCSTOP / CDCSTAT / CDCREAD / CDCTRN / CDCACK.
* Contains **no reference to `$FF8005` or `$FF8007` at all.** It never
  touches a CDC register directly.

So the header version field is not validated by anything: megadev
writes `0x0100`, the BIOS's own module has `0x0010`, Doom CD32X has
`0x0001`, and all three work.

And the strategic point. Two independent shipping programs read a CD in
Mode 1, and neither writes a CDC driver -- both let Sega's firmware do
it. This port wrote one, and today's hardware run showed the firmware
reading LBA 1600 on the same machine our driver cannot.

### Why the timeshare has never worked, probably

The CDBIOS cannot simply be left resident: it occupies sub `$0-$5FFF`
and EmuTOS's low RAM runs to `$13E14`, with the ST vector table and
sysvars at fixed addresses that are the whole reason for the port. So
it has to be swapped in and out, which is what the timeshare does, and
the timeshare has never once succeeded on hardware.

`$1F3E` explains it. The firmware's drive sequences are coroutines: a
step runs, `bsr $1F3E` saves `d5-a0/a2-a4` plus its own resume address
into the state block and returns to the caller, and the *drive's next
exchange* resumes it. A read is not a call that completes; it is a
sequence spread across dozens of 1/75 s exchanges.

Swapping the firmware in, making one call, and swapping it out gives it
one exchange in which to finish something that needs many. It would
fail exactly the way the timeshare fails -- the BIOS executes, the
exchange happens, and no sector is ever announced.

A timeshare that could work has to keep the firmware resident for the
whole duration of a read, several hundred milliseconds, with EmuTOS's
low memory saved and the sub given over to the firmware for that span.
Expensive, but a disc read is expensive anyway, and it is the only
shape that matches how the firmware is built.

### CDCSTART is one routine, and we have it as four steps

`ROMREAD` (function `$20`) is the only thing in the firmware that calls
`CDCSTART`: US `0x3B50`, which pulls the LBA from `a5+$5B0C` and the
count from `a5+$5B10` -- the two longwords of the parameter block --
and `bsr`s `0x1FF2`. `CDCSTART` in turn is the only caller of the
decoder init besides one other site.

That settles a claim `segacd.c` made without a citation: that a retry
of `bcs $410C` re-issues ROMREAD and therefore re-runs the whole
initialisation. It does. Resetting the decoder on every retry is
correct and matches Sega.

What does not match is the shape. `CDCSTART` takes the LBA and the
count and does everything in one flow:

```
$1FF2   entry, d0 = LBA, d1 = count
$2018   clr.b $5A31(a5)          the state byte
        lea $FF8005,a2 / $FF8007,a3
        bsr <decoder init>       AR=$F<-0, IFCTRL, WA=0, PT=0
        move.w 6(a4),4(a4)       flush the sector ring
        bset #5,$FF8033          let level 5 through
        bsr <CTRL0/1 = $A0,$F8>
        move.w #30,d7            three retry counters
        move.w #5,d6
        move.w #5,d5
        bsr $1F3E                ...and a wait
        ...then the CTRL ladder, polling the drive between rungs
```

We do the same register writes in the same order and then stop: our
`cdc_setup()` writes `$A0,$F8` and the next rung back to back with no
wait and no status check between them, and the drive command is issued
separately afterwards by a different function. The firmware never
writes two CTRL pairs without waiting and looking in between, and it
carries three nested retry budgets through the whole sequence.

`segacd.c:610` has said "and it waits on the drive's status in between,
which this driver does not" for some time. This is what that costs.

### The CTRL0/CTRL1 pairs are a ladder, and there are four

This section used to record two of them, `$A0,$F8` and `$A4,$D8`, from
the JP image, and describe the second as what the BIOS writes "once the
drive has locked". Both halves of that were wrong, and the driver spent
three hardware sessions writing a third value, `$A7,$F0`, that appeared
in no note at all. `tools/subbios.py` decompresses the sub-CDBIOS out of
a boot ROM, and searching it for `AR=$A` followed by two data writes
finds four, identical in every region:

| CTRL0, CTRL1 | US | EU | JP |
|---|---|---|---|
| `$A7, $F0` | `0x211A` | `0x211A` | `0x2132` |
| `$A4, $D8` | `0x207A` | `0x207A` | `0x2092` |
| `$A3, $B0` | `0x2168` | `0x2168` | `0x2180` |
| `$A0, $F8` | `0x21DA` | `0x21DA` | `0x21F2` |

They are not alternatives, they are a retry ladder. The firmware writes
a pair, waits, checks the drive's status, and drops to the next one on
failure. `$A0,$F8` is not "the reset pair" in any useful sense either:
it is where the firmware lands when it gives up, immediately before
setting a flag and bailing out (`bsr $21DA; bset #0,...; bra $1F98`).

The addresses this file originally recorded, `$2092` and `$21F2`, are
the JP addresses of the second and fourth rungs -- so the note captured
two rungs of a four-rung ladder and described one of them as the
destination rather than a step.

`segacd.c` walks the ladder now, dropping a rung whenever the previous
configuration put no sector in the ring. Two places write the pair --
`cdc_setup()` and the deferred write in the level 4 handler -- and they
share one table, because two spelled-out copies of a constant is how
this rotted the first time.

`CDCSTART`'s body at `$200A` runs `$21C8`, flushes the sector ring,
sets `$FF8033` bit 5 to let level 5 through, then runs `$21F2`.
`CDCSTOP` at `$1FB6` writes IFCTRL `$38` then `$3A`, and finishes by
reading `$FF8004` and writing the same value straight back.

## Reading one sector

`$4124` is the BIOS reading its own filesystem, and it is the whole
recipe:

```
        CDBIOS $20 (ROMREAD) with { LBA, count }      once, to start
loop:   CDBIOS $8A (CDCSTAT)     until carry clear    a sector is ready
        move.b #3,$FF8004                             destination = sub CPU
        CDBIOS $8B (CDCREAD)                          program DBC/DAC, trigger
        CDBIOS $8C (CDCTRN)      a0 = data, a1 = header
        compare the header against the frame expected, else start over
        CDBIOS $8D (CDCACK)
        LBA++, count--, again
```

`CDCREAD` (`$1D52`) is where the sizes live. It takes the buffer
address from the ring entry, byte-swaps it, and picks a count:

```
        move.w #$092F,d0        2352 raw bytes, minus one
        btst   #3,$5A30(a5)     raw mode?  then that is the count
        move.w #$0803,d0        otherwise 2052: 4 header + 2048 data
        btst   #2,$FF8004       destination >= 4, i.e. a DMA target?
        subq.w #4,d0            then drop the header
        addq.w #4,d1
        btst   #2,$5A30(a5)     wanted with ECC?
        addi.w #$120,d0         then 288 bytes more
        lea    $FF8007,a3
        move.b #2,$FF8005       AR = 2, and it auto-increments from here
        move.b d0,(a3)          DBCL
        lsr.w  #8,d0
        move.b d0,(a3)          DBCH
        move.b d1,(a3)          DACL
        lsr.w  #8,d1
        move.b d1,(a3)          DACH
        move.b #0,(a3)          DTTRG -- go
```

`CDCTRN` (`$1E08`) then waits for `$FF8004` bit 6, reads two words of
header into `(a1)`, 1024 words of data into `(a0)` through an unrolled
loop of eight `move.w $FF8008,(a0)+` with a `nop` between each, waits
for bit 7, and returns. On timeout it rewrites IFCTRL `$38`/`$3A` and
returns carry set.

## The command that starts the drive reading

The last piece, and the one that could not be guessed. `$F70` is the
BIOS's single entry point for "ask the drive to do something": `d0` is
a request code, `d1` is a 32-bit argument.

```
$0FA8   lea    $5854(a5),a1
        andi.w #$FF,d0
        lsl.w  #4,d0           split the low byte into two nibbles
        lsr.b  #4,d0
        move.w d0,(a1)+        nibble 0 = command, nibble 1 = sub-command
```

So request `$30` is CDD command 3 sub 0, `$40` is command 4 sub 0, and
`$2B` is command 2 sub `$B`. The argument arrives as `movea.l d1,a0` at
`$F90` -- an address register used as a data register, which is why
`$1098` reads its input from `a0`. The dispatch two instructions later
sends commands 3 and 4, and only those, to `$108C -> $1098`, the
routine that lays an MSF out into the command nibbles.

`$846` builds the argument: `LBA -> M, S, F -> BCD each -> $MMSSFF00`.
`$1098` clamps it to a floor of `$00017000`, which reads as `00:01:70`
-- the last frame before the two-second pregap, so the floor is the
earliest legal target -- and then writes six bytes:

```
        M M S S F F   into nibbles 2 through 7
```

Simulating the shift chain confirms the order: `$12345600` comes out as
`1 2 3 4 5 6`.

Which leaves the answer. `ROMREAD` (`$3AEE -> $3B0C`) converts its
starting LBA and then sets

```
        move.w #$30,$5B28(a5)
```

which `$3594` later feeds to `$F70` together with the MSF. **The drive
is put into data-read mode by CDD command 3, sub-command 0, with the
target MSF in nibbles 2 through 7.** Command 4 with the same argument
is the seek used by the pause and position paths. After that the drive
streams sectors on its own and the CDC does the rest.

## Not yet covered

The hardware DMA path -- `$FF800A`, at `$2FBC`, `$312E`, `$31DA` and
`$34F2` -- is untouched, because the sub-CPU-read destination above
does not use it. It is what stage 4 will want if copying 2 KB a sector
through `$FF8008` turns out to cost too much.

The sector ring is described but not reproduced. `CDCREAD` takes the
buffer address from a ring entry that the level-5 handler fills in;
a driver that polls instead can read the same value straight out of
`PT` (registers 8 and 9), which is where the handler gets it. That is
the one place our driver will deliberately differ from Sega's, and it
is the place to look first if it misbehaves.

`$1F0A` is reached from `$064C`, which is the level-5 vector, but it is
not a plain handler: it reads IFSTAT, `HEAD0`-`HEAD3`, `PT` and
`STAT0`-`STAT3` into `$5A62(a5)` and then resumes a coroutine saved by
`$1F56`. The coroutine body is the sector-ring bookkeeping.

---

# Stage 3: the driver, and what it does differently

Written after all of the above was read, not during. It is in
`emutos/bios/segacd.c` under "CDC" and "Reading one sector", with the
two paced transfers in `segacd2.S`.

## What it does

```
cdc_setup()     $21C8, $21F2, and the CTRL0/CTRL1 pair from $2132
cdd_ask_read()  CDD command 3 sub 0 at LBA-2, repeated every exchange
                until the decoder reports a sector, then the all-zero
                status packet
level 5         per sector: IFSTAT, HEAD0-3, PT, STAT0-3. Latch PT when
                the header is the one we want
cdc_grab()      DBC = 2051, DAC = PT, DTTRG; wait DSR; read 2 words of
                header and 1024 of data from $FF8008; wait EDT; DTACK
cdc_stop()      $1FB6
```

## Where it differs from Sega's, and why

**No sector ring.** The BIOS's level-5 handler appends each decoded
sector to a ring and `CDCREAD` takes the buffer address from a ring
entry; ours latches `PT` in the handler and hands it straight to the
transfer. Same number, one hop shorter. The ring buys the ability to
fall several sectors behind, which stage 4 will want and stage 3 does
not.

**The transfer runs in the VBL, not in the interrupt.** Level 5 does
thirteen register reads and returns; 2 KB moves at 60 Hz, well inside
the roughly 93 ms of slack the decoder's 16 KB buffer gives at 75
sectors a second. The Genesis interlock at `$FF8022` bit 3 is held
across it, for the same reason the CDD holds it.

**Level 4 masks level 5.** Our CDD handler runs at `#0x2700`, so a
decoded sector waits for the drive exchange to finish. Sega's does not
mask it. This is the safer way round -- the CDD exchange is the thing
with a deadline -- and it costs the CDC nothing, since the delay is
tens of microseconds against a sector period of 13 ms.

**Two sectors, not one.** `cdr_lbas` holds 16 and 1225. The first is
recognisable, the second is over a thousand sectors away and is what
makes the result mean something.

## How it is checked

`tools/check-cdread.py` against the ISO the disc was built from. The
sub hashes each captured sector, the servant copies the report and the
last sector into work RAM, and the harness dumps work RAM. So the
question "did we read sector N" is a byte comparison, not a reading of
a status line.

Both cores, first attempt, no disc burned:

```
slot 0: LBA 16   = MSF 00:02:16   hash 610EDEEF: MATCH
slot 1: LBA 1225 = MSF 00:18:25   hash 2B448D9C: MATCH
BYTES: slot 1, LBA 1225, all 2048 bytes match the image
```

## Still unverified on hardware

Everything above. Emulators model the CDC's registers and buffer, but
neither models the transfer window that cost stage 2 eight discs, and
neither charges for the pacing `nop`s that are in the burst loop
precisely because Sega put them there.

---

# The drive's own state, and the sixteen handlers

Written after four rounds of tuning timeouts around a state machine
that had never been opened. Everything below is from `$140E` and the
handlers it dispatches to.

## The dispatch

A status packet arrives, `$13BA` sets bit 4 of `$5837`, and `$140E`
picks it up:

```
btst   #4,$5837        a packet is waiting
beq    out
andi.b #$D0,$5837      consume it, keeping bits 4, 6, 7
clr.b  $583A
move.b $586E,d0        status nibble 0: what the drive is doing
move.b d0,$5878        ...kept here, where the read path polls it
andi.w #$0F,d0
add.w  d0,d0
add.w  d0,d0
jmp    $1434(pc,d0.w)  sixteen ways
```

`$5878` is the byte `$11CE` returns and `ROMREAD`'s wait loop compares
against 8. **This driver has never read it.**

## Two helpers the handlers share

`$1558` -- **forget where the head is.** Writes -1 to `$5A22` (long),
`$5A26` (long) and `$5A2A` (word): the three cached position fields.

`$1498` -- **there is no disc information.** Zeroes `$5A16`, `$5A1A`,
`$5A1E` and `$5A1F` and then sets bits 0, 1 and 2 of `$5839` to say so
authoritatively. `$11E0` and its neighbours test those bits before
handing any of it out, so this is not "unknown", it is "known to be
nothing".

Which handlers call `$1498` is the single most useful thing here: it
partitions the sixteen states into the ones where the drive still knows
where it is and the ones where it does not.

## The sixteen

| n | goes to | what it does | position | disc info |
|---|---|---|---|---|
| 0 | `$1498` | nothing but publish empty disc info | kept | **cleared** |
| 1 | `$14E6` | `$5820 = 3`, wake `$583A`, event 2 | kept | kept |
| 2 | `$14BC` | `$5820` 1 -> 2, count in `$5822`, wake, event 2 | **lost** | kept |
| 3 | `$14F0` | event 2 | kept | kept |
| 4 | `$14F8` | `$5820` 2 -> 3, event 2 | kept | kept |
| 5 | `$1476` | clear 6 set 5, wipe `$587A`/`$587B`/`$587E`, `andi #$9F,$5838`, then `$1498` | **lost** | **cleared** |
| 6 | `$150E` | event 1, `$1498`, wake | kept | **cleared** |
| 7 | `$1520` | event 0, `$1498` | kept | **cleared** |
| 8 | `$151C` | wake, then falls into 7 | kept | **cleared** |
| 9 | `$152C` | `ori #$48` -- events 3 and 6 -- wake | kept | kept |
| A | `$1538` | event 2, `$1558` | **lost** | kept |
| B | `$1542` | event 5, `$1498`, `$1558` | **lost** | **cleared** |
| C | `$1550` | event 2 | kept | kept |
| D | `$150E` | as 6 | kept | **cleared** |
| E | `$1476` | as 5 | **lost** | **cleared** |
| F | `$150E` | as 6 | kept | **cleared** |

The events are bits of `$5837` and each has a consumer: bit 0 at
`$1798` and `$17CA`, bit 1 at `$17F6` and `$1816`, bit 2 at `$1A2C`,
bit 3 at `$1BE4`, bit 5 at `$1B7E`. `$583A` is polled by the handler at
`$0660`, which calls `$254A` when it is set.

## What that means for us

Three groups, and the driver needs to treat them differently:

**Working: 1, 3, 4, 9, C** -- and 2 and A, which keep the disc
information but lose the position because the head is moving. These are
the states in which waiting for a sector is sensible. State 1 sets the
mode variable to 3 and is the one to wait in.

**No information: 6, 7, 8, D, F** -- the drive is telling us it has
nothing. Sega's response is to publish "no disc info" and raise an
event; ours is to keep waiting for a header that is never coming, for
the whole timeout, three times over. This is almost certainly what a
failing read on hardware looks like from the inside.

**The disc changed: 5, B, E** -- the TOC is thrown away and the position
with it. Anything cached is void. Our sector cache and ring would both
be serving fiction.

`ROMREAD` singles out 8 and retries on it, which fits: of the
no-information states it is the one the BIOS thinks is worth another
go.

## The correction this makes to the driver

Waiting for a sector must be conditional on the drive saying it is
doing something. At the moment `cd_wait_hit` looks only at whether a
sector has appeared, so a drive reporting 6, 7, 8, D or F is
indistinguishable from a slow one -- and six seconds of silence gets
read as bad luck rather than as a refusal. That is the same shape as
the stage 2 failure: the drive was talking and we were not listening.

Not yet implemented. Written down first, on purpose.

---

# What the BIOS does before it asks for data

`$34E6`, on the `ROMREAD` path, and the reason this driver has been
failing on hardware while passing in emulation. It is short:

```
        bsr    $11CE           the drive's state, from $5878
        cmpi.b #1,d0  beq ready
        cmpi.b #4,d0  beq ready
        cmpi.b #$C,d0 beq ready
        cmpi.b #5,d0  beq give_up
        cmpi.b #$B,d0 beq give_up
        cmpi.b #$E,d0 beq give_up
$352E:  move.w #$60,d0          command 6, no arguments
        bsr    $F70
        bcs    yield_and_retry
        bsr    $2F6C            yield
        bsr    $F5A             did it error?
        tst.b  d0
        bne    $34E6            yes: look again from the top
ready:  ...
$3580:  move.l $5B2C,d1         the target position
        move.w #$A0,d0          command $A
        bsr    $F70
$3594:  move.l $5B2C,d1
        move.w $5B28,d0         command 3, for ROMREAD
        bsr    $F70
```

So a read is **three commands**, not one:

1. If the drive is not in state 1, 4 or C, **command 6** puts it in one,
   and the state is checked again. States 5, B and E abandon the whole
   operation.
2. **Command `$A`** with the target position.
3. **Command 3** with the same position -- the only one this driver has
   ever sent.

## Command $A, and why it is not implemented

`$10CE` builds its argument, and the first thing it does is call
`$11E0`, which returns the disc information only if bit 0 of `$5839`
says that information is valid -- and returns carry set if it does not,
which sends `$10CE` straight to its error exit at `$1124`. Then `$EB0`
turns the position into whatever `$A` actually carries, which is a word
and two bytes rather than six MSF digits.

In other words **command `$A` needs a table of contents**, and this
driver has never read one. Adding it means implementing the TOC read
first, which is its own piece of work. Written down rather than
guessed at.

## What was implemented

The state precondition, with one deliberate departure: a drive that
will not reach state 1, 4 or C is **nudged and then read from anyway**,
rather than refused. Both emulators sit in state 9 at rest and read
perfectly from it, so gating on the list turned two working reads into
two failures the moment it was added. Either the list is narrower than
the hardware requires, or emulators report a state the real BIOS never
sees. The count of reads that started from an unready drive is on the
status line, so the disagreement is visible rather than assumed.

States 5, B and E do abandon, because those are the ones where the
drive is saying the disc is not the disc any more.

## The table of contents, and what command $A really needs

Where the BIOS keeps it, from the three report handlers `$1568`
dispatches to on status nibble 1:

| report | handler | goes to |
|---|---|---|
| 3, disc length | `$16EE` | `$587E` (long), then `$E96` publishes a derived word to `$581E` |
| 4, first and last track | `$1714` | `$587A` and `$587B` |
| 5, a track's start | `$1738` | `$5882` + track x 4, after matching the echoed track number in `8(a4)` |

`$116C` reads that table back and clears bit 15 of the entry with
`bclr #$F,d0 / sne d1`, so the top bit of a track's start flags what
kind of track it is.

**Command `$A` needs only the disc length.** `$10CE` opens by calling
`$11E0`, which returns that one number and fails with carry set if bit
0 of `$5839` says it has never been read -- and a failure there sends
`$10CE` straight to its error exit at `$1124`. Then `$EB0` compares the
target against the disc length, works in whatever unit `$B04` produces,
and picks among thresholds at `$5810`, `$5812` and `$5814`, halving or
subtracting. That is a seek-distance calculation: how far the head has
to travel, which cannot be known from a timecode without knowing how
much disc there is.

So `$A` is a **positioning hint**, not a prerequisite. The decisive
evidence is on the user's own console: stage 3 read LBA 1400 and
verified all 2048 bytes against the pattern with command 3 and nothing
else. Reproducing `$EB0` means reproducing a drive-mechanics model, and
the payoff is seek time rather than correctness.

## What was implemented

The disc length, caught from the probe rotation that was already asking
for it twice a second -- one comparison, no extra traffic. It is worth
having regardless of `$A`: a read past the end of the disc can be an
error instead of a seek into the dark.

It is also a free check on the whole CDD path that involves no sector
read at all. The drive says `01:28:48` and the image is 6498 sectors;
6498 + 150 is 6648 frames, which is one minute, twenty-eight seconds
and forty-eight frames. `tools/check-cdread.py` asserts it.

## $B04, and why command $A can be written after all

The reason for not implementing `$A` was going to be that `$EB0` needs
a lookup table out of Sega's ROM, and copying one into a GPL project is
not a thing to do casually. That reason does not survive looking at the
table.

`$B3E` picks a (step, offset) pair from `$B56` and leaves the step in
`$580A` and a pointer in `$580C`. The step is `$1194` -- 4500, the
frames in a minute -- so the table is indexed per minute of playing
time, and `$B04` interpolates between neighbours:

```
bsr     $806            position -> frames
move.w  $580A,d2        4500
divu.w  d2,d0           minutes, remainder frames
add.w   d0,d0
movea.l $580C,a0
move.w  2(a0,d0.w),d1   the next minute's entry
move.w  (a0,d0.w),d0    this minute's
sub.w   d0,d1           delta
swap    d0              the remainder
mulu.w  d0,d1
divu.w  d2,d1           delta scaled by how far into the minute
```

The table itself starts at `$B6A`: 0, 470, 927, 1372, 1806, 2228, 2642
... rising with a shrinking step. That is the shape of a CD spiral. A
disc is constant linear velocity with a constant track pitch, so after
t seconds the head has travelled

```
r(t) = sqrt(r0^2 + v*p*t/pi)      turns = (r - r0) / p
```

With the Red Book numbers -- programme area from 25 mm, 1.6 um pitch,
1.25 m/s -- **22 of the 24 entries come out exactly right and the other
two are one off, from rounding.** The table is the number of turns of
the spiral at each minute, and `$B04` is "how far out is the head".

`$EB0` then takes the difference between two of those and sorts it into
bands at `$5810`, `$5812` and `$5814` -- which `$E38` computes at
runtime from `$E62` and the constant `$380`, not from any table. So
`$A` carries a seek distance in turns of the spiral, banded.

None of that is Sega's data. It is the geometry of a compact disc, and
it can be generated from four constants at build time. The gap was
left for a reason that turned out not to exist, which is worth
recording: the argument sounded principled and it was not checked.

## Where the bands come from, and how command $A is built

`$E38` computes the six thresholds from one long and the constant
`$380`:

```
$5810 = $380 + low(x)     $5812 = $5810 * 2      $5814 = $5812 >> 2
$5816 = $380 + high(x)    $5818 = $5816 * 2      $581A = $5818 >> 2
```

and `x` comes from `$CB2`, which reads `$FFFE0000` -- the console's own
internal backup RAM -- with `movep.l` at offsets `$19`, `$11`, `$09`
and `$01`, comparing three copies against each other and gated on
`$5EA0` lying between 4 and 7. **The bands are per-console drive
calibration stored in BRAM**, and when it is missing, or the copies
disagree, or the gate fails, `$CB2` returns zero.

Zero is the case that matters to us: it gives bands of `$380`, `$700`
and `$1C0`, which are constants. We neither need nor want to read a
user's calibration.

## $EB0 and $10CE, complete

```
turns_len = B04(disc length)        turns from the spiral formula
turns_tgt = B04(target)
$581C = turns_tgt
if turns_len > turns_tgt:           the ordinary case
        dir = $FF
        d = turns_len - turns_tgt
        if d <= $581A:  no command at all
        elif d <= $5816: d -= $581A
        elif d <= $5818: d >>= 1
        else:            d -= $5816
        if $581C < $514: d -= $581A >> 4;  borrow -> no command
else:                               target at or past the end
        dir = 0
        d = turns_tgt - turns_len
        ...the same with $5814, $5810, $5812
```

and `$10CE` packs it:

| nibble | 0 | 1 | 2,3 | 4,5 | 6,7 |
|---|---|---|---|---|---|
| | `$A` | 0 | direction, `00` or `0F` | `d` bits 15-8, split | `d` bits 7-0, split |

**When `$EB0` returns "no command", `$10CE` never sets the post flag at
`$1110` and nothing is sent.** So for short seeks the BIOS goes straight
to command 3 -- which is exactly what this driver has always done, and
why its reads work at all. `$A` is a long-seek assist and nothing more.

That is the whole model, with no data taken from the ROM: a square
root, four Red Book constants, three thresholds and a subtraction.

## What the drive is actually told: the runtime trace

Everything above is read off a disassembly. All of it could be right and
the driver could still fail, because a disassembly does not say which
branches a real console takes, and eight discs went by learning that the
expensive way. So `boot/cddtrace.S` writes down the conversation instead
of predicting it.

It works because the console reads this disc perfectly, several times,
every single boot -- before the BIOS is evicted. The loader pulls the
volume descriptor, the root directory, the servant and both images off
the drive and has never once failed. That is a working read on the exact
hardware, and until now nobody had looked at what one consists of.

The tracer takes no vector and clears no flag. The BIOS keeps its
outgoing command packet at `$5864`, the last status packet at `$586E`,
the latched drive state at `$5878` and its own mode at `$5820`; the SP's
`ReadCD` samples all four from the loop where it waits for a sector.
An entry is opened only when the conversation changes, and command 2 is
compared on its first nibble alone -- between reads the BIOS rotates
"where are you" / "where in the track" / "which track" one per exchange
forever, and treating that as news filled the log with idling.

Because every file on the disc sits within a few seconds of the others,
an ordinary boot only ever shows the drive nudging forward. So the first
thing the SP does is three deliberate reads -- LBA 1600, LBA 16, LBA
1601 -- which is the shape of every read the D: driver does and gets
wrong.

Reading it: hold **Start** at power-on and the servant puts the log on
screen (A pages, Start boots). In emulation,
`tools/dump-cdtrace.py build/emu-*/wram.bin` decodes the same bytes.

### What both emulator cores say a long seek looks like

```
no  mode command     status      where     xchg
1   0    200000000D  1100001741  00:02:18  2     report request / playing
2   3    3000232302  100002184F  00:02:18  1     read / playing
3   3    000000000F  2F0000000E  00:00:00  1     no change / seeking, not ready
4   3    000000000F  1000232340  00:23:23  1     no change / playing
5   3    200200000B  1000232340  01:03:27  6     report request / playing
```

Four things fall out of that, none of which the disassembly made
obvious:

* **The read command goes out once.** The exchange after it the command
  block is already back to `0` -- no change -- and it stays there for
  the whole seek. Repeating command 3 at 75 Hz, as this driver did for
  hours, is not what the BIOS does at any point.
* **The commanded MSF is two frames before the target.** LBA 16 is
  `00:02:16` and the BIOS asks for `00:02:14`; LBA 1600 is `00:23:25`
  and it asks for `00:23:23`. Every read in the trace is offset the
  same way.
* **A seek of twenty-one seconds of disc took one exchange to start and
  arrived in the next** -- state `2` with report type `F`, then state
  `1` at the destination. No command `$A` appears anywhere in the trace,
  which fits `$EB0`: these are short enough in turns that it declines to
  send one.
* **Between reads the BIOS never stops or pauses the drive.** It polls
  position, the drive keeps playing forward, and the next read command
  simply names a new place. State `4`, paused, shows up only once and
  the BIOS reads straight out of it.

Both cores produce the same trace, which makes it a statement about the
*model* two independent emulator authors built, not yet about the
console. The console's own answer needs a burn.

### What the console actually says

The emulator trace above was two authors' model. This is the machine.
Three pages off a real Mega CD, tracing the CDBIOS through the three
deliberate probe reads -- LBA 1600, LBA 16, LBA 1601 -- transcribed from
the console's own screen:

```
no  m  command     status      where   xchg
03  0  3000232302  4000044658  000446  0001   read 00:23:23, from PAUSED
04  0  000000000F  2F0004465B  000828  0002   command 0. state 2, seeking
05  0  000000000F  2000082856  000828  0001
...                                            position ramps, ~26 entries
1C  0  000000000F  2000230 65D 002321  0002
1D  0  000000000F  1000232043  002320  0007   state 1: arrived
...
21  0  3000021405  2000175259  001752  0001   read 00:02:14, backwards
22  0  000000000F  2F0017525A  001073  0002   command 0 again
...                                            00:17 -> 00:10 -> 00:09 ->
2B  3  000000000F  2000051 45E 000514  0001    00:08 -> 00:06 -> 00:05 ->
33  3  000000000F  2000025150  000251  0001    00:03 -> 00:02
38  3  000000000F  1000021146  000212  0002   state 1: arrived
```

Four things, all of which contradicted this driver:

* **The whole vocabulary is three commands.** `3` to read, sent once;
  `0` for the entire seek; `2` to poll position between reads. Across
  three long seeks the console sends nothing else -- **no command 1, no
  command 6, no command $A**, not once. This driver was sending all
  three, and each one moved the drive somewhere the read path then had
  to recover from.
* **Command 1 was the expensive one.** It leaves the drive in state 0,
  and state 0 is what the console reported every time C: became
  unreadable. The stage-3 self-test parked the drive with it a few
  seconds into EmuTOS's boot -- about when the desktop mounts its
  drives -- so which of the two got there first decided whether C:
  worked at all. That is the "worked once and never again" pattern, and
  it was a race this driver was holding both ends of.
* **The commanded MSF is the target minus two frames**, on hardware as
  in emulation. LBA 16 is 00:02:16 and it asks for 00:02:14; LBA 1600
  is 00:23:25 and it asks for 00:23:23.
* **A twenty-one second seek takes about thirty exchanges** -- four
  tenths of a second -- reported as state 2 with the position ramping
  smoothly, then state 1 at the destination. Seeking back from the rim
  is cheap. Stopping the drive to avoid it never was.

The read is also commanded straight out of **state 4, paused** (entry
03), which settles that state 4 belongs on the ready list where `$34E6`
puts it.

## The drive's own firmware, from the people who measured it

BlastEm's CDD implementation (`cdd_mcu.c`, Michael Pavone) is built from
measurements of the real drive microcontroller, and it answers several
questions the BIOS disassembly cannot, because they are about the far
side of the link:

* **The command packet's nibble 1 must be zero** or the firmware
  answers with a checksum/command error. (Ours always is; now it is
  known rather than lucky.)
* **A read or seek command undershoots by 3 sectors inside the drive
  itself** (`seek_pba = target - 3`). The BIOS's 2-frame lead rides on
  top of that.
* **"Paused" is not stopped.** In state 4 the head keeps reading and
  jumps back one track each time it passes the pause point -- a hover.
  That is why reads issued from pause start instantly, and why pausing
  is what Sega's flow does between reads where stopping never is.
* **During a long seek the status stream itself slows down**: packets
  come every third sector, and periodically the drive reports NOT
  READY (format nibble F) mid-seek -- with a comment that the BIOS
  *depends* on seeing that to clear internal state. Both appear in our
  hardware trace (`2F...` rows, XCHG gaps).
* **Read commands are accepted from almost any state** -- only door
  open, tray moving, lead-in and lead-out refuse, plus a TOC that has
  never been read.

None of this contradicts the driver as it stands; what it does is
retire a whole class of "maybe the packet format is subtly wrong"
worries and sharpen the one place our sequence still knowingly differs
from Sega's: the decoder control pair. The BIOS writes $A0/$F8 at
decoder reset and $A7/$F0 only after the drive reports itself playing,
waiting on drive status in between. This driver wrote both irrespective
of the drive, back-to-back, at setup -- and neither emulator models the
registers, so only the console can judge the difference. It is the
third arm of the boot diagnostic's matrix (LATE), alongside the read
lead (LEAD 20/2) and the after-fill hold (HOLD).

## Sega's own read policy, from megadev

`vendor/megadev/lib/sub/cdrom.s` (drojaazu) carries the read loop from
Sega's Mega CD sample code, and it puts numbers on things this driver
had been sizing by feel:

* the **first sector** after ROMREADN gets `0x258` = **600 polls, one
  per frame -- ten seconds** -- before a single retry is counted;
* a **mid-stream gap** gets only **6 frames** before the whole read is
  reissued;
* **thirty full re-seeks** (`0x1E`) are allowed before the load is
  declared failed;
* every sector's BCD frame code is verified twice, after CDCREAD and
  after CDCTRN, and a mismatch triggers an immediate reissue rather
  than waiting for the wanted sector to come round;
* CDCSTOP precedes every ROMREADN.

The shape matches this driver's loop; the patience did not. The
per-fill budget is now 600 exchanges to match Sega's first-sector
figure, and the attempt cap is raised to eight with time as the real
limiter. (SGDK was checked too: it is cartridge-only and has nothing
for the CD side.)

## What a commercial game actually does: Lunar's sub program

The SP from the user's own dump of Lunar: The Silver Star (USA),
disassembled from the boot area (GameArts, 30 KB, loaded at $6000 like
every SP). Analysis only; the dump lives in vendor/games/, gitignored.

The entire hardware surface it touches directly is one register:
`$FF8004`, the CDC destination select, set to 3 (sub-CPU read) before
each CDCREAD -- exactly as megadev does. **No CDC address/data register
access, no CDD access, anywhere.** Every decoder register and every
drive command goes through the BIOS. The BIOS's internal sequencing --
including $A7/$F0 only after lock -- is the only sequencing any
commercial disc ever exercised.

Its read engine, reconstructed from $61E2-$63A8:

* per tick: poll CDCSTAT; for each ready sector CDCREAD / CDCTRN /
  CDCACK, advance, reset the per-sector counter to 32, and drain
  everything available before yielding;
* no sector: count the 32 down one per tick and yield -- about half a
  second of patience per sector;
* counter exhausted: **CDC_STOP, poll CDB_CHK idle, ROM_PAUSEON, poll
  CDB_CHK idle, reissue ROM_READ** -- up to fifteen reissues;
* and at $6914, the finisher run on every completion *and* every
  failure: **CDC_STOP + ROM_PAUSEON**. Every read run ends with the
  drive paused, no exceptions.

Three sources now agree -- Lunar pauses always, BlastEm shows pause is
a live hover with instant resume, and the hardware trace shows the
drive resting in state 4 -- so both behaviours are defaults now, not
experiment arms: the driver pauses after every fill, and its retry
path pauses and waits for state 4 before every reissued read, which is
also what $34E6's ready list of 1/4/C was saying all along. The
diagnostic's HOLD arm remains, so hardware can still demonstrate the
difference.

## The second pass, forced by the LED

"We use the drive the way Sega does" was said while the front-panel
lamp sat frozen in whatever state eviction caught it -- so the claim
got re-audited from zero: every subsystem the BIOS environment manages
on the sub side, against what this driver owns, and every step of the
recipe above against what the driver actually executes.

Found, in the recipe as transcribed on this very page and never
implemented: **"compare the header against the frame expected, else
start over."** The BIOS verifies every transferred sector; megadev and
Lunar both verify twice; this driver never did. And STAT0 -- read
every sector since stage 3, because reading STAT3 re-arms the
interrupt -- was never once consulted, so sectors the decoder itself
flagged as damaged entered the ring as good.

Both now enforced, plus the staleness they exposed: a ring entry the
stream has lapped points at some newer sector's bytes, because the
decoder's buffer is only five or six sectors deep. Entries more than
four behind the stream are refused.

The header check, switched on in emulation, immediately caught 24
wrong-sector deliveries per diagnostic run -- in the emulators, where
"everything passed" for five weeks. The occasional Fsfirst -33 and
err 3 in emulated runs were this. With the ring pruned at the source:
ten rounds, zero errors, zero wrong sectors, zero refusals, on both
cores -- the first clean sheet the diagnostic has ever printed.

Still deliberately unowned: the audio fader at $FF8034, frozen at
eviction like the LEDs were. Irrelevant to data reads; first item on
the stage-5 list.

## The lamp was right and the latch story was wrong

The access lamp transitions *after* the splash on builds where nothing
writes the LED register after eviction -- so it tracks real drive
activity on the console, whatever the register says. Read it that way,
both of its states were true reports: solid through the mount was the
retry storm, and solid at an idle desktop was the pause hover -- the
head physically re-reading one track 75 times a second, forever,
because "pause between reads" was adopted from Lunar without the other
half of the contract. CDBPAUSE's own documentation supplies that half,
with a warning: the pause-to-standby delay runs 0x1194 ticks and up,
and a drive that never stops "can damage the drive if used
improperly."

So the driver now implements Sega's standby: sixty seconds (0x1194,
their documented floor) after the last real work, command 1 stops the
motor. The read path wakes a stopped drive the way it wakes any other
unready state -- command 6 and patience -- and the lamp at an idle
desktop goes dark because idle finally means idle.

## The disc was the last variable, and it was ours

Two hardware failures named their addresses: LBA 1608, then 1618. Both
inside the D: image, and both, on inspection, 2042-plus identical
bytes -- the tail of the first FAT and the root directory of a nearly
empty filesystem, sitting inside 12 MB of zero padding.

Mode-1 CD data is scrambled before writing precisely because long
uniform runs produce pathological pit patterns; that is what ECMA-130's
scrambler exists for, and it is the mechanism copy protection later
weaponised as "weak sectors". On a pressed disc it costs nothing. On a
CD-R burned at 10x and read by a 1992 lens it is the worst case the
medium has. Every sector this console has ever read successfully --
volume descriptor, boot block, the self-check pattern -- held
structured data. Every persistent failure sat in a zero region. And
Lunar, dense data end to end, reads on the same spindle in the same
drive.

So the disc no longer contains a zero region anywhere the driver has
to read:

* PAD.BIN is generated pseudorandom rather than /dev/zero;
* the D: filesystem is nearly full -- a 4001 KB filler file leaves only
  a handful of free clusters, so the FAT's own tail is allocated data
  rather than zeros, which is the only way to fix a FAT tail (entries
  for free clusters must read zero);
* free clusters and unused root-directory entries are filled with
  pseudorandom bytes, both legal: a free cluster may hold anything, and
  0xE5 marks a deleted entry which -- unlike 0x00 -- does not terminate
  a directory scan.

Audit after: zero sectors in the D: region are more than 88% uniform,
against 497 before. The 62 that remain are ISO 9660 metadata the
driver never touches.

## The timeshare: Sega's firmware reads again, under EmuTOS

The premise of this project was that the CDBIOS and the Atari low-memory
map want the same 24 KB, so one of them has to go. That is true, and it
does not mean the BIOS has to be destroyed -- only that it cannot be
resident. It can be parked and visited.

The loader photographs 0x0000-0x5FFF into Word RAM at 0xBA000 on its way
past eviction: the machine's own firmware, still inside the machine it
shipped in, above the end of EmuTOS's code where the ST memory map has
no opinion about it. When a sector is wanted, low memory and the park
exchange contents, Sega's code runs the drive, and they exchange back.
Nothing is copied, transcribed, or redistributed; the bytes never leave
the console they came with.

Four things had to be true, and each cost a crash to learn:

* **Interrupts masked across the whole visit**, not each exchange.
  Between them the vector table at 0 is Sega's while every variable
  EmuTOS's handlers touch is parked, so one tick in that window runs
  one OS's handler against the other's state.
* **The visit needs its own stack.** EmuTOS's supervisor stack lives at
  0x800, inside the exchanged region, so the first swap turned the
  exchanging code's own return address into BIOS bytes. You cannot
  exchange the ground you stand on. The scratch stack, the state block
  and the read buffer live at 0x7C000-0x7EFFF, taken off the top of the
  A: ramdisk -- above phystop where EmuTOS never allocates, and outside
  the swap. That region is the only memory on the machine that is both.
* **a5 is the only register the CDBIOS preserves.** The dispatcher at
  $2E34 is `moveml %a5,%sp@-` / `moveal #0,%a5` / ... / `moveml
  %sp@+,%a5` and nothing else. Loop counters and result codes in other
  registers are debris after any call -- which is why the routine once
  returned 0x0FAA, a value matching no path in it. Every piece of state
  now lives at a5. sp.S's ReadCD has always worked for this reason.
* **The firmware must be woken.** Its idea of the drive stopped at
  eviction, thousands of our own commands ago, so a session starts the
  way a Sega program starts one: DRV_INIT for the TOC, then CDB_STAT
  until the drive reports paused or playing.

Then ROMREADN, and the CDCSTAT/CDCREAD/CDCTRN/CDCACK loop, with the gate
array passing levels 4 and 5 so Sega's own handlers drive the state
machine -- levels 2 and 3 stay masked, or the servant's frame interrupt
and EmuTOS's tick would run Sega's handlers against parked variables.

Verified on both emulator cores: the sector at LBA 1400 comes back and
all 2048 bytes match the pattern the driver regenerates from a formula,
with EmuTOS surviving the round trip and the diagnostic continuing
afterwards. It is not yet wired to D:; that is the next step.

## $5F7C: the freeze, found

Wiring it to D: hung the console. Both boots, ordinary and diagnostic,
stopped on the EmuTOS version banner with the HUD frozen and nothing
else on screen -- no error, no code, no way to reach the diagnostic
that would have said anything.

The visit does not poll the drive. ROMREADN arms the CDC and returns;
every byte after it moves inside the firmware's level-4 (CDD) and
level-5 (CDC) handlers, and DRVINIT waits for a table of contents that
only its own level 4 can deliver, with no timeout of its own. A `jsr`
that never comes back cannot be reported on. So four things had to be
true before the firmware was asked for anything, and none of them were:

* **$FF8037 HOCK.** Bit 2 is what makes the drive talk. Clear, no
  status packets arrive and level 4 never fires. Written as `#$04`, the
  acknowledge form the firmware uses -- not OR-ed in, because bits 0
  and 1 are DTS and DRS and a read-modify-write hands back whatever
  mid-exchange values it found.
* **$FF8033 OR-ed, not overwritten.** The firmware owns these enables
  and reprograms them; writing `$30` flat takes away the timer it runs
  its own waits on. Measured on the same disc and the same test:
  fifty-six reads by the nineteenth round OR-ing in, thirty-five
  overwriting.
* **$5F7C must stop being the firmware's INT2.** This is the freeze.
  The trampoline routes MDINT to the sub module's Int2 entry, which
  lived at $6000, which is EmuTOS now, and the main CPU raises MDINT
  every frame. The moment anything lowers the interrupt mask far
  enough, Sega's firmware jumps into the middle of somebody else's
  operating system. Enabling levels 2 and 3 with the slot as it was
  killed every visit; an `rte` written into the slot, same build, same
  disc, and it reads. That was the only difference between the two
  runs. The `rte` stays in the parked copy -- the routine it displaces
  has not existed since the loader jumped.
* **Level 4 must be seen to arrive before DRVINIT is called.** A stub
  in the vector counts the interrupts and chains to the firmware's own
  handler, and the visit spins for two of them -- 27 ms of drive time,
  half a second of bound -- before it calls anything that waits. If
  they do not come it returns stage 1 rather than taking the console
  with it. `CDBIOS probe 0000 read 0000 sw 14 i4 23` on the diagnostic
  is that number: twenty-three level 4s delivered, so the pipeline the
  read depends on is running and not merely assumed to be.

One ordering in the preamble is load-bearing and not yet understood:
moving the counter's `clr.l` below the SR write twenty lines further on
-- nothing else changed, same instructions in the same visit -- hangs
the console on the banner again, reproducibly. It is left where it
verifies, with a comment saying so.

D: on both cores with the firmware driving: Dfree, Dsetpath, Fsfirst,
Fopen and Fread all return, fifty-odd reads a run with five or six
failures among them, and the file's contents come back. Genesis Plus GX
and PicoDrive agree.

## `i4 1`: what the hardware said

That build reached the console and answered: `CDBIOS probe 007f read
0100 sw 30 i4 1`. Stage 1, thirteen visits, and **one** level 4 in the
whole visit -- then silence, every time. In emulation the same build
takes twenty-odd per visit.

`probe 007f` was worth nothing and should never have been printed: the
answer to CDBCHK is the carry flag, and d0 is scratch after any call.
It now returns bit 8 as the carry.

One level 4 then nothing has two completely different explanations --
the interrupt path does not really work with Sega's vectors installed,
or it works until Sega's handler runs once and then stops -- and the
counter as it stood could not separate them. So the visit now runs in
two phases:

* **Phase A** puts *our own* acknowledge in the level-4 vector: wait out
  DRS, take the ten status bytes, write `$04` back. That is the exact
  sequence EmuTOS's own handler uses, and it keeps this machine's drive
  talking at 75 Hz for minutes on end, because that is what the CD
  driver has been doing all along. Half a second, then the count is
  frozen and reported.
* **Phase B** installs the firmware's handler and does the read.

The diagnostic prints `i4 own/total`. If the two are equal, the count
stopped dead the moment Sega's handler took over and the firmware is
what silences the drive. If the total runs away from `own`, the path is
fine and the fault is further in. Emulation gives `i4 2/25`, which is
the second case; the console will say which one it is.

Two other numbers went on the same line. `v4` is where the parked
firmware routes level 4 -- `005F88` in emulation, the `_LEVEL4`
trampoline, so the park is intact. `ctl` is `$FF8037` and `$FF8033` read
on entry before the visit touches either; `041C` says HOCK was already
set and the mask already had CDD in it.

## The dead-man switch

The stage-1 gate is gone. It was refusing to call the one function whose
job is to put a drifted drive back in step, on the grounds that the
drive might have drifted.

What makes that safe is a watchdog on level 2. Every BIOS call is a
`jsr` with no timeout on our side; if it does not come back there is no
loop to bound and nothing to print, which is precisely how the console
came to be sitting on a version banner. But the Genesis raises MDINT
every frame whatever the drive is doing, so the visit is now bounded
from outside by a clock the CD cannot stop: three hundred frames and it
is abandoned where it stands -- interrupt frame discarded, stack reset,
stage 5, out through the normal exit. EmuTOS's stack pointer moved from
a6 into the state block for this, because a6 does not survive a BIOS
call any more than d0 does.

Six failed visits with no successful read and the timeshare stands down
altogether: our own driver takes the drive back, `cdd_cmd_on` restored
to whatever it was. An imperfect D: beats a machine that grinds.

The diagnostic also takes the timeshare without being asked. Right and
Down together is a diagonal, and the person holding it cannot tell
whether it registered.

## `i4 2/3`: the firmware's own handler is what stops the drive

The console answered:

```
CDBIOS probe 007F read 0500 sw 18
i4 2/3 v4 005F88 ctl 041C
```

`v4 005F88` -- the park routes level 4 through `_LEVEL4`, so the
snapshot is intact and the vector is sane. `ctl 041C` -- HOCK already
set, mask already carrying CDD. And **`i4 2/3`**: phase A reached its
two packets in the 27 ms it should take, and then, with the firmware's
handler installed, exactly one more level 4 arrived in the entire rest
of the visit. Emulation gives `2/21` on the same build.

So the interrupt path is not the problem. Something in one run of
Sega's CDD handler stops the drive talking. `read 0500` is the watchdog
catching the consequence five seconds later -- no freeze, a code.

That is the report's stated threshold reached: *"If INT4 is confirmed
firing and DRVINIT still hangs, suspect stale BIOS RAM variables in your
snapshot -- the BIOS assumes continuity since reset."* The parked
firmware's CDD state machine has been sitting in Word RAM since
eviction while a different driver drove the drive for half an hour.

Three things follow from it:

* **The gate array's mask is the first suspect, because the firmware
  writes it.** The watchdog now reads `$FF8033` every frame, records
  which bits it finds switched off, and switches CDD and CDC back on.
  The record is the diagnosis -- bit 4 in `ls` means the firmware
  disabled the drive's own interrupt -- and the re-assertion is the
  repair if that is what is happening. Emulation reports `ls EB`: bit 5
  goes off and on, which is just CDCSTOP masking the decoder, and bit 4
  never does.
* **The watchdog now says which call it caught.** The function number
  goes into the state block before every dispatch, so stage 5 returns
  `05xx` with `xx` the call that never came back -- `0510` DRVINIT,
  `0520` ROMREADN, `058C` CDCTRN.
* **DRV_INIT is off by default, and that is a correction.** No Sega code
  that reads a sector calls it: sp.S, ProjectCD's `ReadCD` and Lunar's
  loader all go CDCSTOP, ROMREADN, poll -- and that is the code that has
  read this disc on this console every time it has been asked. "Wake the
  firmware" was mine, not theirs, and it is the call the watchdog keeps
  catching. The diagnostic alternates it on and off round by round, `I0`
  and `I1` in the header, so the disc can say whether it helps.

Both cores with the diagnostic: gpgx 83 reads 13 failures, PicoDrive
79/15, all four D: calls returning, watchdog never firing. Plain boot
unchanged.

## `ls CB`, `read 058A`: two answers, one of them about the instrument

```
CDBIOS probe 007F read 058A sw 18
i4 2/3 v4 005F88 ctl 041C ls CB
```

**`ls CB`.** Bits 0, 1, 3, 6 and 7 were seen switched off during the
visit; **bits 4 and 5 never were.** The firmware does not disable the
CDD or CDC interrupt in the gate array. The mask theory is dead, which
is worth more than it sounds -- it was the cheapest remaining
explanation and it is now excluded rather than merely unfavoured.

**`read 058A`** was the instrument lying, and the instrument's fault was
mine. Stage 5 is "the watchdog caught a call that never returned" and
`8A` is CDCSTAT -- but CDCSTAT had returned perfectly well, thousands of
times, each one saying "no sector yet". The poll loop was bounded at
300000 iterations of a BIOS dispatch, which is longer than the
watchdog's five seconds, so the watchdog won a race it was never meant
to enter and a plain stage-3 timeout was reported as a hang.

Every bound in the visit now runs off the watchdog's own frame counter
instead of a guess about how many dispatches fit in a second: 40 frames
for the level-4 probe, 200 for the read, 300 for the watchdog. Same
clock, no race, honest stage codes.

`i4 2/3` held with DRV_INIT off, so the one exchange that silences the
drive is not DRV_INIT's. What is left is its content, so the visit now
photographs it on the way out: the four leading status bytes the drive
sent, the four command bytes the firmware wrote back, and both control
bytes. Emulation reads `st 01000000 cm 02000000 x 0434` -- drive state
1, command 2, which is a read, and the control bytes exactly as set.

The diagnostic re-arms the timeshare at the top of every round. Standing
down for good is right on an ordinary boot, where the user wants a
working machine; it is wrong in a diagnostic whose entire purpose is to
make the firmware try again. The read and failure counts are now
per-round rather than cumulative, which also makes the DRV_INIT arms
comparable.

## Four drivers on one disc

`a2dfa36` -- rebuilt exactly, same submodule commit, same tooling --
boots and shows DISK C and no file in it. So it is not the build that
worked; the one that populated C: is earlier than I thought. It also
boots visibly faster than the current build, which is a second
regression hiding in the same forty commits and worth its own hunt.

Finding the right commit by burning one disc per candidate is a stack of
CD-Rs nobody has. But the only thing that differs between those builds
is `EMUTOS.IMG`, about 230 KB, and the disc carries twelve megabytes of
padding doing nothing. So the disc carries four of them and the pad
chooses at boot:

| held | image | commit |
|---|---|---|
| nothing | `EMUTOS.IMG` | current |
| Left | `EMUTOS2.IMG` | `e6192f5` Stage 4: the disc is a disk |
| Up | `EMUTOS3.IMG` | `51f65e5` Stage 4: D: opens |
| Right | `EMUTOS4.IMG` | `596173f` D: fails fast rather than appearing to hang |

`sp.S` reads the servant's boot-flag word before it loads the image and
picks the filename from it. The check is on the `$5A` tag, so a disc
booted before the servant has answered falls back to `EMUTOS.IMG`
rather than to nothing. The older builds read only bits 0 and 1 out of
that word -- A and B -- and ignore the directions entirely, which is
what makes those bits safe to take.

The direction has to be held until the desktop appears; holding it
afterwards just drives the desktop, which is harmless but opens menus.
Left is no longer the current build's two-frame read lead: the
diagnostic sweeps that anyway.

## 1616 reads, 1624 does not

`51f65e5` -- "Stage 4: D: opens" -- boots fast on the console, opens C:
and lists both files. It will not open `READCD.TXT`.

Where the two things live settles what that means. The D: image is
FAT16 at CD sector 1600: reserved sector, two 32-sector FATs, then a
512-entry root directory, then the data area.

* the root directory entry naming `READCD.TXT` is in **CD 1616**
* the file's 187 bytes are in **CD 1624**

Eight sectors apart, one reads and the other does not. Listing a folder
only needs the first sector of the root directory, which is why the
window opens and the file will not. And the sectors this port has been
failing on all along -- 1608, 1617, 1618, 1620 -- sit between them.

So the driver that works is failing on one identified sector, and it
has never had a counter in it that could say so. `EMUTOS2.IMG` on the
four-era disc is now that build with the runtime diagnostic grafted on
and nothing else changed: the same read path, byte for byte, plus a
loop that reports Dfree, Dsetpath, Fsfirst, Fsnext, Fopen, Fread and
the driver's own seek, wait, error, `why` and last-failed-sector
counters. It runs on the direction alone -- holding a direction is already the
choice to run a diagnostic, and asking for a second button on top of
that means a diagonal, which the person holding it cannot confirm.

It sits behind **Left and Up both**. Left has been unreliable on this
console since long before this disc -- "left+down never worked" -- and
one boot spent finding that out again is one boot wasted. Up is the
direction proven to select an image on hardware. Right keeps the plain
build for comparison; the fourth era is dropped, being one commit from
the third and less interesting than having the test reachable twice.

Even in emulation this build shows `err 9` and `lastfail 1618` while
still returning 0 from every call, so the region is marginal there too
and the driver is recovering from it. On hardware it does not.

---

# The asynchronous half, read out rather than assumed

Everything above 0x80 was mapped a long time ago. Everything below it was
described by its *rule* — "the dispatcher queues it" — and then used on
faith: two function numbers were in the driver, 0x10 and 0x20, with the
names DRVINIT and ROMREADN attached to them by inheritance. This is the
table itself, from `$2FD8`, 44 entries, printed by `tools/subbios-map.py`.

## How a queued call works

```
2e26: bclr #7,d0
2e2a: beq  $2F10        bit 7 clear -> the queue
2e2c: bsr  $2E3A        bit 7 set   -> the synchronous jump table

2f10: ror.w #4,d0       ...leaves the HIGH nibble in d0.b
2f14: beq  $2F46        $0x: store the command word, nothing else
2f1a: beq  $2F3C        $1x: command word + one longword from (a0)
      $2F30             $2x and up: command word + two longwords
2f4c: bset #7,$5AF2(a5) pending
2f52: rts
```

**The call returns immediately.** It does not do the thing; it asks for
the thing. The drive's own exchange drains `$5AF2`, copies it to
`$5B0A`, and dispatches through the table. Function bodies suspend
themselves through `$2F54` (`move.l (sp)+,$5AFE(a5); rts` — save my
return address and get out) and resume on later exchanges.

That is why "DRV_INIT needs twenty seconds" was never a statement about
DRV_INIT. `$313E` issues CDCSTOP and yields on its fourth instruction.

## The variables the bodies move

`$5802` is the mode the command is asking for — 0 stop, 1 play, 2 pause,
3 and 4 seen but not pinned down. `$5B42` is a flag byte every one of
them edits. `$5B0C` is the position and `$5B10` the end position.

## What each entry does

Behaviour, not names: a name with no citation is worth less than an
address.

| n | body | what it does |
|---|---|---|
| $00 | 3292 | mode 0, and writes 0x1000 to `$5AFC` |
| $01 | 32DE | mode 4 |
| $02 | 3396 | mode 1, `$5B40`->`$5B2A`, refuses drive states 0, 5, 11 |
| $03 | 38C6 | mode 1, clears bit 2 of the flags |
| $04 | 38DA | mode 2, sets bit 7 clears bit 5, then CDD command `0x8060` |
| $05 | 3774 | mode 2, `andi #$CF` on the flags |
| $06 | 3760 | mode 2, clears flag bit 5 |
| $07 | 3752 | mode 2, sets flag bit 5 |
| $08 | 3BA6 | |
| $09 | 3BB4 | |
| $0A | 32E6 | mode 0, refuses drive states 0, 5, 11, 14 |
| $0B | 3388 | mode 3, sets flag bit 2 |
| $0C | 38CC | mode 1 |
| $0D-$0F | 3088 | the reject stub |
| **$10** | **313E** | **CDCSTOP, then yields. One long: {first track, last track}** |
| $11-$16 | 34A0 3484 3492 3438 346A 341E | one long each |
| **$17** | **3AEC** | **`$5B10 = -1` then falls into $21: a read with no end** |
| **$18** | **3A9C** | **seek: mode 1, LBA+150 through the MSF converter at `$82E`, step id 43, wait for drive state `$40`** |
| $19 | 3464 | |
| $1A-$1F | 3088 | the reject stub |
| **$20** | **3AD6** | **`$5B10 = $5B0C + 149 + count`: a bounded read** |
| $21 | 3AF4 | the shared read tail |
| $22-$2B | | two longs each |

## Two things this changes

**$20 is a bounded read and the driver already uses it.** `$5B0C + 149 +
count` is the last sector wanted; the count is honoured. The name
ROMREADN in `segacd2.S` is inherited and probably wrong, but the call is
the right one.

**$17 is a read with no count.** It writes -1 into the end position and
falls into the same tail. One longword of argument, the start. That is
the primitive for "start reading here and keep going", which is the
shape this driver has been trying to approximate by re-commanding the
drive once per 2048 bytes.

Doom calls $02, $03, $04, $0A, $10, $18 and $20. It does not call $17.

## $18 in full, and the number it settles

```
3a9c: move.w #1,$5802(a5)      mode 1, play
3aa2: move.l $5B0C(a5),d0      the position, as an LBA
3aa6: addi.l #150,d0           + the lead-in
3aac: bsr    $82E              -> BCD MSF: divu #75, divu #60, tobcd each
3ab0: move.l d0,$5B0C(a5)      written back over the LBA
3ab4: move.w #43,$5B06(a5)     step id
3aba: ori.b  #3,$5B42(a5)      flag bits 0 and 1
3ac0: move.w #$40,$5B28(a5)    the drive state to wait for
3ac6: lea    $5B0A(a5),a0
3aca: lea    $5B1E(a5),a1
3ace: bsr    $2F6A             copy the command block
3ad2: bra    $3450             the shared issue-and-wait tail
```

So $18 is a seek that ends parked on the target at state $40 — which is
the state the hardware notes describe as a hover, head still reading and
jumping back a track each pass, and the reason a read issued from pause
starts instantly.

The three constants together settle the lead, which this driver has been
treating as a tuning knob with a default of 20 frames:

| where | offset | meaning |
|---|---|---|
| $18, seek | LBA + **150** | the exact target |
| $20, read end | LBA + **149** | inclusive last sector |
| ROMREAD start | LBA + **148** | two frames of run-up |

Two. Not twenty, and not a preference — it falls out of the arithmetic
in three different places. `CDR_EARLY` defaulting to 20 has no support
anywhere in the firmware.
