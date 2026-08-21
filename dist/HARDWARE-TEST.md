# EmuTOS for Sega Mega CD — hardware test build

Atari TOS (EmuTOS) running on the Mega CD's sub 68000, with the Genesis
CPU acting as display/input servant. This is the first build meant for
real hardware.

## Which image

| File | Region |
|---|---|
| `emutosmd-J.iso` / `.cue` | Japan — **use this one** on a JP Mega CD |
| `emutosmd-U.iso` / `.cue` | US |
| `emutosmd-E.iso` / `.cue` | Europe — PAL, verified booting at 49.7 Hz under both emulators |

Your BIOS is region-free, so any of them should boot; `J` matches the
console. Both `J` and `U` are verified booting to the desktop under
Genesis Plus GX with your own BIOS dumps.

## Burning

The `.iso` is a Mode 1 / 2048-byte data track — burn it as a **data
disc, disc-at-once**, at the **slowest speed your drive offers** (4x if
available). CD-R, not CD-RW; older drives read CD-R far more reliably.

- ImgBurn: *Write image file to disc*, point at the `.cue` (preferred)
  or the `.iso`.
- macOS/Linux: `cdrecord -v -dao speed=4 dev=/dev/sr0 emutosmd-J.iso`

The disc is padded to ~2.8 MB. That is deliberate: the real payload is
about 300 KB, which is under the Red Book minimum track length and gets
rejected by some burners and drives.

## Before you boot

- **Remove any cartridge** from the slot, and never insert one while the
  console is running. A cart present at power-on makes the machine boot
  the *cart* (Mode 1), so the CD is never reached; hot-plugging one
  crashes the 68000 outright. A flashcart cannot stand in for a backup
  RAM cart either — a backup cart is dumb glue (size ID at `$400001`,
  SRAM at `$600001`, write-protect latch at `$700001`) that a ROM cart
  simply does not decode.
- **To use the console's internal 8 KB backup RAM as drive B:, hold C on
  pad 1 from power-on through the boot.** See below.
- If you have a Sega Mouse, plug it into **port 2**. A control pad in
  port 1 works either way.

## What you should see

1. Sega CD BIOS screen (press Start if it waits at the animation).
2. A few seconds of drive activity while ~340 KB loads.
3. The **GEM desktop**: menu bar (Desk / File / View / Options), a mouse
   pointer, `DISK A` and possibly `DISK B` icons, Trash and Printer.
4. A green **status line** along the bottom letterbox:

   ```
   CART0000 BRAMY BWY MSEN SNDY CDP00 F1A2B
   ```

   - `CART nnnn` — sectors found on a backup RAM cart (`0000` = none,
     which is expected with no cart in the slot)
   - `BRAM x` — the console's internal 8 KB backup RAM:
     `N` none found, `S` Sega-formatted so **left untouched** (your game
     saves are safe), `Y` formatted for EmuTOS and mounted as `B:`
   - `BW Y/N` — **the answer to "can B: actually be written?"** A
     write/read-back test on the internal BRAM at boot. `Y` means the
     memory takes writes; `N` means real hardware refuses them and no
     amount of driver work will save it (get a backup RAM cart).
   - `MSE Y/N` — a Sega Mouse answered the handshake on port 2
   - `SND x` — `Y` the PCM chip is set up and keyclick is enabled,
     `I` set up but keyclick disabled, `N` not set up
   - `CDP ab` — two separate hex digits. **`a` counts CD drive commands
     the drive was actually handed**, mod 16, and should climb by one
     about twice a second. `b` is a flags digit: odd means the CD
     interrupt stormed and was switched off, and the rest is keyclicks
     mod 8. Both digits have been rented out to whatever question was
     open at the time; the sound ones are answered now.
   - `F nnnn` — frame counter in hex, **always incrementing**. This one
     runs on the Genesis, so it proves only that the screen is being
     drawn — which the screen already proves. The number that matters
     is the sub CPU's heartbeat in the `CD` field below.

   A second row reports the last drive-B: request:

   ```
   LAST RW W1 LBA 007 ERR 0000
   ```

   ```
   D1  RW W1  LBA 007  ERR 0000   FS6010
   ```

   `FS` is the **filesystem read back from the drive itself** after
   formatting: it must read **`6010`** (the boot sector's first byte,
   and 16 total sectors). Anything else means the bytes we wrote did
   not stay written, which is exactly what makes GEMDOS refuse the
   drive with "unknown device" and report 0 bytes.

### Read these two digits — they end the guessing

The three digits after the probe used to be drive state, accepted, and
track flags. Two of them are now **the two checksums**:

```
CD s i v  p  r  o t  X  c
             │  │ │  │
             │  │ │  round trip proven?
             │  │ the checksum the packet carried
             │  the checksum we computed over it
             which question the drive answered
```

Every packet is failing its checksum while the drive is plainly
answering, and there are only two ways that happens. Printing both
numbers tells them apart at a glance instead of costing another disc:

| What you see | What it means |
|---|---|
| the two digits differ by **the same amount every time** | our arithmetic is wrong — a fixed, findable bug |
| the two digits **wander independently** | the packet is arriving stale or torn — a timing problem |
| the two digits **match** | it is fixed; the score will be non-zero and the last digit `Y` |

I have guessed between those two possibilities three times now and been
wrong each time. This stops me doing it a fourth.

### The CD field — the rest of it

```
D0 RW R1LBA 003 ERR0000.C CDF6C 3 104 Y 4
                        ^^   ^^^ ^ ^^^ ^ ^
                        |||  ||| | ||| | transfer-status bits
                        |||  ||| | ||| round trip proven?
                        |||  ||| | ||track flags
                        |||  ||| | |did the drive accept the question?
                        |||  ||| | drive state
                        |||  ||| which question is being asked
                        |||  ||sub CPU heartbeat
                        |||  |CD drive interrupt heartbeat
                        |||  link score
                        |sub: CD commands enabled
                        pad: A was held at power-on
```

**Read the two heartbeats first.** They are single hex digits that must
both be spinning too fast to follow. Between them they say which half
of the machine stopped, and nothing else on screen can:

| Heartbeats | Meaning |
|---|---|
| both spinning | everything is alive; read the rest of the field |
| **sub frozen**, drive spinning | the sub CPU died — the desktop will be frozen too |
| sub spinning, **drive frozen** | the CD interrupt stopped; the score drops to `0` within a fifth of a second |
| score shows **`S`** | the interrupt stormed and was switched off to save the machine |

The last row is why this build exists. The screen you sent me read a
perfect link score with a frozen packet, and I called it healthy. It
wasn't: the score was only ever written *by* the CD interrupt, so when
that interrupt stopped, the last good value simply stayed on screen. An
instrument that goes quiet when the thing it measures fails is worse
than no instrument. The score is now watched from outside and forced to
`0` when the interrupt goes quiet, the frame counter on the top line
has been replaced by the sub's own heartbeat (the old one ran on the
*Genesis*, so it kept climbing happily with the sub dead), and a runaway
interrupt now masks itself off instead of hanging the console.

**What the drive is doing.** Your last report had it answering `F` —
the CDD's documented "invalid / not ready" — to both questions I was
asking. Both happened to be clock queries. So this build sweeps seven
questions instead, about one every half second, cycling:

| Probe | Question |
|---|---|
| `0` | get drive status — asks for nothing, cannot be out of range |
| `1` | absolute clock |
| `2` | track-relative clock |
| `3` | current track number |
| `4` | total disc length |
| `5` | first and last track |
| `6` | the drive's own record of its last error |

None of them move the drive. The accept nibble should **echo the probe
number**, one step behind the probe digit. Under both emulators it
echoes every one of the seven exactly.

Your console answered `F` — "invalid" — to all seven, including probe
`0`, which asks for nothing at all and cannot be out of range. A drive
that refuses even that is not refusing the question; it never heard
one. So the last digit is new, and it is the one I actually need:

### The last digit — and this one is not a guess

You asked why I was guessing at what the BIOS does instead of just
reading it. Correct question, and it ended the search.

Putting the BIOS on the disc wouldn't have worked — it's compressed in
the ROM, and the sub-CPU half is position-dependent code that expects
to live at `$0000–$5FFF`, exactly the low memory the ST system
variables need. That collision is why it gets evicted in the first
place. But the console decompresses it into PRG-RAM at boot, and this
project already had an audit payload that snapshots precisely that
region. So Sega's own CD routine could just be read:

```
btst   #1,$FF8037     spin until DRS goes low — the incoming
dbeq   d0,...         packet has finished arriving
lea    $FF8038,a1     read ten status bytes
move.b #4,$FF8037     acknowledge: HOCK stays, DTS and DRS clear
lea    $FF8042,a1     only now load the ten command bytes
```

and it does all of that **every exchange, 75 times a second**, sending
a NOP when it has nothing to ask.

**That last write is the whole answer.** Four discs went looking for a
trigger among the writes to the command registers, and there is no such
trigger: the transfer is handed over by writing the control register
back to HOCK-only, which clears the two transfer bits. This driver
never wrote that register again after boot — so the gate array sat
waiting for an acknowledgement that never came, no command was ever
carried, and the drive went on reporting its own status and answering
"invalid" to questions it had never been asked. Every symptom you
reported, exactly.

Four differences, all of which earlier discs had wrong. The
acknowledgement comes **before** the command is loaded, not after. The
wait is for `DRS` to go **low** — `dbeq` exits when its condition is
true and `btst` sets Z when the bit is zero, which reads as the
opposite at a glance. The BIOS writes a command on **every** exchange,
75 a second, with a NOP when idle. And the acknowledgement is
**conditional on DTS being set** — writing it also zeroes DRS, and a
zero written to DRS aborts an incoming transfer, so doing it
unconditionally tears down packets that were about to arrive. That
last one is almost certainly why the previous disc turned a merely
unhelpful link into a dead one.

### The test disc came back green

The standalone test — same driver, same interrupt, with EmuTOS and the
screen pump taken away — **answered green on your console**. The drive
named two different questions it was asked. So the protocol is right,
and everything wrong is in how EmuTOS shares the sub CPU with the drive.

Diffing the green test against the EmuTOS driver left exactly one
behavioural difference: **the test writes a command on every single
exchange, and EmuTOS only wrote one when it had a question**, going
silent in between. The drive is evidently not willing to be talked to
intermittently — the exchange is a conversation with a turn every
1/75 s, and missing your turn ends it. EmuTOS now always takes its turn.

That also flushed out a regression worth knowing about: with the sub
talking to the drive 75 times a second, the bus interlock below starved
the screen so completely that the desktop stopped being drawn. The
interlock now gives up after four frames, and the busy flag covers only
the register accesses rather than the whole handler.

### The drive started answering

The last disc got `RS1 = 3` out of your console — the drive **named the
sub-command it was asked for**, for the first time in nine attempts.
Always taking our turn was the missing behaviour, and the protocol
question is closed.

What was left was that the score read `0`: the answers were arriving and
being thrown away, because the packets were failing their checksum. That
is the bus interlock's fault, not the drive's. The starvation guard I
added grabbed the bus regardless after four frames, and a halt in the
middle of a ten-byte read tears the packet in half — a perfectly good
reply, ruined on the way in.

The registers hold the packet until the next exchange, so the fix is
simply to **read it again** when the checksum fails. One retry, in the
sub, costing microseconds.

**Systematic, not occasional.** The score stayed at `0` — every one of
sixteen packets failing, not one in five — which rules out a torn read
entirely. Diffing the green test against this driver one last time
found the reason, and it is embarrassing:

```
the BIOS and the green test:   move.l / move.l / move.w   5 bus cycles
this driver:                   ten byte reads in a C loop
```

The gate array updates those registers on its own clock. A read spread
over ten separate accesses plus loop overhead straddles an update
*every* time. I wrote "long, long, word" in my own notes, implemented
it for the command write, and never once looked at the read.

**The probe digit was also telling me something.** It read `8` on every
report, and bit 3 of that digit is the bus interlock's busy flag — so
the flag had been raised on the first exchange and never lowered. An
edit lost the line that clears it and nothing complained, because a
flag that is never lowered looks exactly like a machine that is always
busy: the interlock protected nothing, the servant fell back to taking
the bus anyway every fifth frame, and the screen quietly ran at a fifth
of its rate. It is cleared again now.

If the score comes off `0` this time, that was the cause. If it does
not, the packets are failing systematically rather than occasionally —
every one of sixteen, not one in five — and that is a different problem
from a torn read, which is the next thing I will chase.

(I first tried to fix this by splitting the screen copy into five short
holds and waiting for the drive's window before each. That spun the
Genesis for up to 39 ms a frame, the sub stopped getting its VBL, and
the whole display went blank. Caught in emulation, reverted, not
shipped.)

### The one that was never about the protocol

Your last report said `CDP10` — **one command sent, then it stopped** —
which is the same signature as the very first hardware test, and it has
not changed no matter what I did to the sequence. That is a strong hint
the sequence was never the problem.

The Genesis halts the sub CPU with a bus request every frame, to read
the screen out of PRG-RAM. If that halt lands inside a CD exchange, it
stretches the exchange across whole milliseconds — far outside the
1/75 s window the drive's gate array is clocking. A status *read* that
arrives late costs nothing, which is exactly why the passive link has
always been perfect. A *command* that arrives late is corruption.

So this build makes the Genesis ask permission: the sub raises a flag
for the few microseconds it is talking to the drive, and the servant
checks it before taking the bus and again after being granted it,
backing off and skipping that frame's screen copy if the sub is busy.
The cost is an occasional dropped frame of repaint. No emulator can
show this either way — their bus arbitration is instant and neither
models the drive's transfer window at all.

That was half the idea. The Genesis is not the only thing that takes
this CPU away from the drive: **the sub's own interrupt handlers were
running at level 7**, which locks out the drive's level-4 exchange for
their entire duration — the whole VBL handler, mouse and keyboard and
every status field, sixty times a second, plus a 200 Hz timer. That is
milliseconds of the drive's window, lost, by exactly the same mechanism
as the bus grab and by a different route. They now run at level 3 so
the drive can interrupt them.

This is a hypothesis, not a reading. But it is the first class of
explanation that covers *all* the evidence: perfect while passive
(a late read is harmless), dead at the first command (a late command is
corruption), indifferent to every protocol change, and invisible in
emulation, where arbitration is instant and neither core models the
transfer window at all.

And the fix that came out of the disassembly, which stays in: **the BIOS
re-sends the same command on every exchange until the drive answers
it** — a retry count of 75, a full second of asking. Every build I have
sent you fires a command once and moves on. Both emulators accept a
single shot, which is precisely why six discs looked right here and did
nothing on your console.

The full annotated disassembly is in `docs/cdd-bios-notes.md`,
including the command opcode table and the three things still
unaccounted for.

So there is one digit to read now:

| Digit | Meaning |
|---|---|
| `-` | the drive has not answered a question yet |
| `Y` | **round trip proven** — stage 2 is done |

`Y` is not "the code ran". The drive has to name the sub-command it was
asked for, for two *different* sub-commands, because a single match
could be a value already sitting in the register. Both emulators reach
`Y` in about five seconds, on all three regions.

## Controls

| Input | Action |
|---|---|
| D-pad | Move the mouse pointer (accelerates while held) |
| A | Left mouse button |
| B | Right mouse button |
| **Start** | Show / hide the on-screen keyboard |
| **C held from power-on** | Claim the internal backup RAM as `B:` (erases Sega saves) |
| **A held from power-on** | Send the CD drive no commands at all — the passive-link fallback |
| D-pad (keyboard up) | Move between keys |
| A (keyboard up) | Press the highlighted key |
| Sega Mouse | Moves the pointer directly, buttons work |

## Test checklist

Roughly in order of what tells us the most:

1. **Boots to the desktop.** The whole cross-CPU pipeline works.
2. **Status line counts up.** Servant alive, VBL flowing.
3. **Pointer moves with the pad**, menus drop and close cleanly.
4. **`DISK A` opens** (double-click) and lists `README.TXT`.
   Double-click the file — it should open a text window.
5. **Start brings up the keyboard**; d-pad moves the highlight; A on a
   key types into a dialog (*File → New folder*). ✅ confirmed working
   on hardware.
6. **Writing to B:**: open both `A:` and `B:` windows, then drag
   `README.TXT` from A: onto the B: window. It should copy, and opening
   it on B: should show the same text as on A:. Then delete it from B:,
   and create a folder. All three used to fail; all three should now
   work. Check `BW` on the status line first: if it reads `N`, skip
   this, the hardware simply will not take the writes.
   Then try **Options → Save Desktop** (it writes to A:, which had the
   same bug).
6. **Mouse**: does `MOUSE` read `Y`? Does the pointer track smoothly,
   and does it move the **correct way vertically**? If Y is inverted,
   that is the known US-vs-JP polarity difference and it is a one-line
   fix — just tell me which way it went.
7. **Speed feel**: window dragging and menu opening should be
   markedly quicker than the last build — a full-screen repaint is
   **3.4x faster** (measured 63.0 -> 18.4 frames, both cores agreeing).
   Still not instant. Almost all of what is left is the conversion
   arithmetic on the Genesis CPU; the VDP is under a tenth of it.
8. **CD link.** Let it sit on the desktop for half a minute and read
   the `CD` field. One digit matters more than the rest: **the last
   one**. If it ever shows `4`, `5`, `C` or `D`, the drive is being
   sent commands for the first time. If it stays `0`, `2`, `8` or `A`,
   it never has been, and that is the whole answer. Second question,
   if there is anything left to ask: does the accept nibble echo the
   probe number for any of the seven?
9. **Sound.** Already **confirmed audible on hardware** — but only
   through the CD unit's
   own output. If you hear nothing, plug headphones into the back of
   the Mega CD before you doubt anything else: the console's own A/V
   out carries the Genesis's FM sound and not the CD unit's PCM, which
   is why a US BIOS boot jingle can sound fine while every click is
   missing.

   **No cable changes needed.** The Sega CD returns its audio to the
   console through the expansion connector, so a standard A/V lead
   already carries the mixed CD and cartridge sound — mono on a Model
   1, stereo on a Model 2. The RCA jacks on the CD unit are a
   sound-quality option, not a requirement.

   **The chip was switched off.** `CTRL` bit 7 is the master enable and
   bit 6 chooses whether the low bits mean a wave RAM bank or a
   channel — and those were inverted here, so the whole channel setup
   ran with the chip disabled. Every register written to exactly the
   right place, nothing sounding, and nothing observable to say so:
   `SND Y` and a rising `CLK` were both perfectly true and perfectly
   useless. Fixed, along with the settling-time pauses the chip needs
   between control-register writes.

   Then, in this order:
   - **A single chime during boot**, before the desktop paints. This is
     the whole audio path in one event — if you hear it, the PCM chip,
     the wave RAM and the channel programming all work.
   - **A click on every on-screen keyboard keypress**, anywhere, with
     or without a dialog open. The click happens where EmuTOS turns a
     scancode into a key event, so it does not care whether anything is
     listening. Shift may be silent; that is expected.
   - **Nothing should drone.** The sample ends itself after ~60 ms, so
     a tone that keeps going means the chip is not reading the end
     marker and I want to know.

## Drive B: on a stock console — internal backup RAM

Every Mega CD has 8 KB of battery-backed RAM on the sub CPU's own bus,
with no cartridge involved. This build can use it as drive `B:` — small,
but genuinely persistent across power cycles.

It is **opt-in and off by default**, because that memory usually holds
Sega-format game saves: the status line will read `BRAM S` and the drive
will not appear. To claim it:

> **Hold C on pad 1 from power-on, through the BIOS screen, until the
> desktop appears.**

This **erases whatever is in the internal backup RAM** — Sega game
saves *or* an existing EmuTOS volume — and formats it fresh. Keep it
for the case where `B:` is too broken to reach the desktop; the normal
way to reformat is now a program.

## Reformatting B: — `FORMAT.PRG`

**Fixed in this build — the first version did not work on hardware.**
It used logical sector I/O, which needs the volume to be mountable, and
the only volume anyone formats is one that is not. It now goes straight
to the driver. A second bug turned up underneath: a drive with no
readable boot sector was assumed to have 9 sectors per track rather
than our 16, so raw reads inside the volume reported "sector not
found".

Open `DISK A` and double-click **`FORMAT.PRG`**. It reports the size of
`B:`, asks for confirmation, writes a fresh filesystem, and reads the
boot sector back to prove it stuck.

This is the fix for **folders on `B:` that refuse to delete** ("cannot
delete directory"). Those folders date from the builds where writes
reported failure after having partly landed, so their directory
clusters never received their `.` and `..` entries, and the desktop
cannot empty a folder it cannot enumerate. Nothing is wrong with
`Ddelete` — it returns 0 on an empty folder and −36 on a full one — and
nothing short of a reformat will clear them.

The program is not Mega CD specific: it asks the drive how big it is by
probing for its last readable sector, so it works on the 8 KB internal
BRAM and on a backup RAM cart without being told which is fitted. Afterwards it stays EmuTOS-formatted, and normal
boots (no C) will mount `B:` automatically — the status line reads
`BRAM Y`. To give it back to Sega, use the console's own BIOS Backup RAM
manager to reformat.

With a real backup RAM cart the cart wins automatically (it is far
bigger) and the internal BRAM is left alone.

## Writing to B: (fixed)

**Every write used to be reported as failed while actually landing.**
That is why folders you created survived a power cycle even though the
desktop said the copy had failed, why a dragged `README.TXT` appeared
on B: with no contents, and why deleting gave "cannot delete
directory".

EmuTOS follows every floppy write with a verify pass (`fverify` is on
by default). That verify has a per-machine implementation and the
fallback returns "unknown device" — and there was no Sega CD
implementation. So the write succeeded, the verify failed it, and
GEMDOS reported an error on a device that had just serviced the
request. −15 is outside EmuDesk's error table, which is how it became
the "output device is not receiving data" alert.

There is now a Sega CD verify that does what the XBIOS asks: read the
written sectors back and report any that will not read. A full
create / write 400 bytes / close / reopen / read-back round trip now
verifies byte-exact on both A: and B:.

This also fixes **Save Desktop**, which writes to A:.

The B: volume is still genuinely tiny — 8 KB, 11 usable clusters, and
every folder costs one — so a real "disk full" is easy to hit. That
looks different now: you will get a proper full-disk error rather than
an "output device" alert.

## Viewing README.TXT (fixed)

**This was the "screen fills with E's" bug, and it is fixed properly
this time.** Colour 0 is transparent on the Genesis VDP, and the ST
background *is* colour 0 — so a text page is a hole straight through
the foreground plane. The background plane's nametable had never been
initialised, and an all-zero nametable makes every cell draw **tile
0** — the screen's top-left character cell. So the lower plane tiled
the whole display with the page's first letter and showed it through
the paper: 'E' from "EmuTOS…", 'W' from "Welcome to EmuCON". An empty
file was clean because its first cell is blank. The GEM desktop hid it
entirely — its background is opaque.

The servant now wipes VRAM at startup (the Sega CD BIOS leaves its own
fonts and logo tiles there) and parks both the background and window
planes on a blank tile.

The two earlier explanations shipped in previous builds — CRLF line
endings and the hardcoded screen base — were wrong. Both of those
changes were real improvements and are still in, but neither was the
cause. This one reproduces and disappears on demand under both
emulators.

## Known limits in this build

- **`DISK B` is 8 KB** unless a real backup RAM cart is fitted, in which
  case it is the full cart. The cart path is tested against the emulated
  512 KB cart; the internal-BRAM path is tested on PicoDrive, which
  emulates no cart.
- **No CD drive as a GEMDOS volume yet.** The disc is a boot medium
  only; the Sega CDBIOS is evicted at handoff because it occupies the
  memory the Atari ST ABI needs. A native CDD/CDC driver is the next
  milestone.
- **Sound is new and unheard.** There is now a PCM bell and keyclick
  driven straight from the RF5C164 — no BIOS involved. Both emulator
  cores stub the registers that would prove it is sounding, so you are
  the first ear on it. There is a boot chime and a click per keypress;
  see the checklist.
- **The bell has no other trigger.** It rings on a BEL character
  reaching the console, and nothing in GEM, the desktop or EmuCON ever
  writes one — real TOS alerts are silent too, so I have not changed
  GEM to beep. The chime and the keyclick are the audio paths.
- The desktop's "Save Desktop" now works, but it writes to the boot
  drive, which is the RAM disk — so it still will not survive a power
  cycle. Saving it to B: would.

## If it fails

Tell me what the screen and the status line were doing — that pair
narrows it enormously:

| Symptom | Likely meaning |
|---|---|
| Sega CD logo, then nothing | Disc not accepted / burn issue, or security block mismatch |
| Black screen, no status line | The servant never started — boot chain failed early |
| Status line present, `F` frozen | Servant crashed after init |
| Status line counting, screen frozen | EmuTOS crashed on the sub CPU |
| Garbled tiles | Display transport issue (real-hardware timing) |
| Repeated letter over the whole screen | The plane-B bug — should be gone; tell me if it is back |
| Desktop but no pointer motion | Input path |

A photo of the screen is worth a lot, especially if anything is
garbled.
