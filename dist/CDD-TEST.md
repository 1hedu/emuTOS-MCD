# CD drive test disc — one colour, one answer

This disc does not run EmuTOS. It exists to answer a single question,
because eight discs of protocol corrections never moved the symptom and
that means the protocol may not be what is wrong.

It runs **the same CD driver** as the EmuTOS build, in the same place —
the drive's own level-4 interrupt on the sub CPU — but with everything
else taken away:

- the Sega CDBIOS stays resident; nothing is evicted
- EmuTOS is not running, so there is no VBL handler and no 200 Hz timer
  masking the drive's interrupt out
- the Genesis is not pumping the screen, so nothing halts the sub with a
  bus request in the middle of an exchange

Burn it exactly like the others: `.iso` or `.cue`, data disc,
disc-at-once, slowest speed. `cddtest-J.iso` matches your console.

## What you will see

The **whole screen turns a solid colour** a few seconds after the Sega
licence screen. That is the entire user interface. Report the colour.

| Colour | Meaning |
|---|---|
| **Green** | The drive answered. It named two different questions it was asked, so this is a real round trip and not a coincidence. |
| **Cyan** | It named one question, then stopped. Partial. |
| **Yellow** | Status packets arrive and checksum perfectly, but the drive never named a question. This is the symptom you have been seeing under EmuTOS. |
| **Red** | No status packet ever checksummed — the link itself is down. |
| **Blue** | The test never started. My fault, not the console's. |
| Sega licence screen, stuck | The disc failed to boot. Also my fault. |

Both emulators go **green** in about five seconds.

## What each answer means for the project

**Green on your console** — the driver is correct and the fault is
entirely in how EmuTOS shares the sub CPU with the drive. That is a
much smaller problem than the one I have been chasing, and it is in
code I control rather than in a protocol I was guessing at. The two
fixes already shipped (keeping the Genesis off the bus during an
exchange, and dropping the interrupt handlers from level 7 to level 3)
are the first two candidates, and there will be others.

**Yellow on your console** — the driver is still wrong despite matching
Sega's own disassembled code line for line, and the fault is in the
protocol after all. Then the remaining unknowns in
`docs/cdd-bios-notes.md` are where I go: the `$FF8001` bit-0 wait in the
BIOS's init, whether an idle NOP must go out on every exchange, and the
four other subsystems the BIOS's level-4 handler calls.

Either way it halves the search, which is more than any of the last
eight discs managed.

## Honesty about this disc

It took several tries to get right, and every failure was mine, not the
console's — the test code was assembled after the BIOS's sector buffer
and got overwritten during boot; an odd number of bytes in a data block
misaligned everything after it and hung the machine outright; a word
write cleared one byte more than intended; and the first version changed
its question every exchange, so it compared each reply against a
question it was no longer asking and could never have gone green.

All four were caught in emulation rather than on a disc, which is what
the emulator is for. I mention them because the value of this test
depends on it failing honestly, and it has now been seen to fail in four
distinct ways before passing.
