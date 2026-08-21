# Why a hot-swapped backup RAM cart cannot be a persistent drive

The Mode 1 branch lets the boot cartridge be pulled with the power on and a
backup RAM cart seated in its place (SWAP.PRG). The map flip works; mounting
works; formatting works; files work — and nothing survives the next power
cycle. This document is the closed case for why, so nobody reopens it with
another register write.

## What was measured, on hardware, with the servant's own probes

| Fact | Instrument |
|---|---|
| Map flips completely on the pull: Mega CD PRG window works at $020000, Mode 1 slot addresses read open bus ($000001/$200001 = FF) | evidence block, SDIAG |
| The cart behaves protocol-perfectly after a hot insert: ID $06 mirrored at odd bytes, memory odd-bytes-only (even half of the window holds nothing), ~512KB real distinct non-aliasing storage | even/odd hold tests, alias checks, sizing |
| The write-protect register at $7FFFFF gates the whole array: protected, both halves refuse writes; enabled, both hold | four-cell WP matrix |
| After every hot insert the entire array reads a repeatable-but-drifting pattern (same bytes, scattered single bits differing per cycle); nothing written in a session survives power-off | sector dumps across ~14 sessions |
| The same cart, seated before power-on, holds data across days of normal use | owner's daily use on the CD branch |
| First read of $400001 after a hot insert returns the ID with junk high bits (C6/DE), later reads a clean 06 | first-read vs header-dump comparison |

## The controlled experiment

Everything above still allowed one escape: the failing flow always
contained a power cycle *and* a map flip *and* an insertion, so the
insertion was only the most likely culprit, not a proven one. So the
other two were removed.

With the machine running, S: formatted and carrying a file, the backup
cartridge was pulled and the **same cartridge reseated** -- power never
cycled, and the map never moved, because a backup cartridge does not
ground /CART either way. SDIAG afterwards: generation 3 (both probes
ran, so the reseated cartridge really was examined), no stored volume
anywhere in the 4MB window, sector zero reading junk unlike even the
usual power-up pattern (EC 33 8E 0E 75 41, where every powered-up
session reads a BE-family fingerprint).

A volume that was readable seconds earlier, destroyed by nothing but
the trip across the connector. That is the mechanism observed directly
rather than inferred.

(The first attempt at this test was inconclusive through a bug of ours:
the servant disarmed after the press over the empty slot, so the
reseated cartridge was never probed. Fixed -- every swap is now the
same two-press conversation -- and the run above is the repeat.)

## The mechanism

The drifting pattern is a textbook SRAM power-up fingerprint (per-cell
threshold mismatch gives a stable pattern; marginal cells flip per cycle —
Holcomb/Burleson/Fu, IEEE Trans. Comput. 58(9), 2009). Battery-held cells
cannot produce one; an array that just lost its state always does. So the
array is arriving *blank* at every hot insert — and the one event unique to
the hot-swap flow is the insertion itself.

The Mega Drive cart slot is a plain 2×32 card edge with no staggered
ground-first contacts. Mating it against a live bus connects pins in random
order over tens of milliseconds; driven signal pins reach the SRAM before its
own power/ground do, back-powering the chip through its input protection
diodes and dropping its internal rail below the data-retention floor
(the documented NV-SRAM corruption mechanism — Maxim AN202). The array is
blanked to its power-up pattern before any instruction runs. The chip is
undamaged, which is why every probe afterwards finds a flawless cartridge
wearing fresh noise.

## The cartridge itself

The cart is a clone of an open KiCad design (WindDrake/SegaCD_Ramcart on
GitHub), which itself rebuilds the official Sega cart's topology
('138 decode, '74 write-protect latch, '32, '245 buffer) around a
CY62148ELL 512K×8 SRAM — matching the measured 512KB and ID $06 exactly.
Its battery backup is a CR2450 behind a dual-Schottky diode-OR plus a 10K
pullup to put the SRAM to sleep at power-down. There is **no supervisor IC**:
nothing gates chip-enable while VCC is out of tolerance, so during the mating
chaos the LS-TTL glue (floating inputs read high) can strobe the SRAM freely.
Seated before power-on, the diode and pullup are enough — hence the cart's
perfect record on the CD branch. Hot-inserted, nothing protects it.

## Consequences

- **Software cannot fix this.** The data is destroyed before the servant can
  execute anything. Every register theory (bank latches, /TIME state, WP
  ordering, smart-cart hooks) was chased and eliminated by measurement.
- **Hot-swapped S: is real but volatile**: format it, use it, lose it at
  power-off. SWAP.PRG says so on screen.
- **Persistence requires the cart to be seated before power-on** — which on
  Mode 1 means it never can be, since the boot cartridge owns the slot.
- Hardware paths that would open it: a hot-swap riser with ground-first
  sequencing / switched VCC; series resistors + clamp + bulk capacitance on
  the cart; or replacing the SRAM+battery with FRAM, which has no volatile
  array to lose.

## What this settles about the two branches

It reads as a loss and is closer to a clarification. Before this, both
boot paths could claim the cartridge and both could claim the disc, and
the same backup cart meant different things depending on how the machine
had started -- an ambiguity that had to be explained every time either
branch was described.

The hardware has now drawn the line for us, and it is a clean one:

- **Mode 1 owns the cartridge slot.** It boots from it, and the backup
  cart it can hot-swap in is a fast scratch volume: real, writable, and
  session-lifetime by physics rather than by policy.
- **The CD branch owns persistence.** The cart is seated before power-on,
  which is the only condition under which its battery is in charge, and
  it keeps data for as long as the cell lasts.

Neither needs to explain itself in terms of the other, and no build has
to pretend a hot-swapped cart might survive a power cycle.
