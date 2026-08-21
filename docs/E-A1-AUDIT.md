# E-A1: CDBIOS vs the ST low-memory ABI — audit result

**Verdict: EVICT.** The resident Sega CDBIOS cannot coexist with EmuTOS's
ST-authentic low memory. After the boot loader finishes staging, EmuTOS
takes the whole sub CPU; the CDBIOS is gone until a native CDD/CDC driver
restores live CD access (milestone E5).

## Method

Dynamic diff on real BIOS state, run on the user's own console BIOS image
(JP Mega CD model 1, BOOT ROM 12/28-1991 ver 1.00, RetroSix chip = patched
stock) under Genesis Plus GX:

1. `AUDIT=1 tools/build-iso.sh J` builds a disc whose SP snapshots sub
   PRG-RAM `$0000–$5FFF` into Word RAM (SP op 3), bracketing the CD
   activity: snapshot A right after the boot handshake, snapshot B after
   the ISO9660 reads, a file load, and ~2 s of BIOS idle interrupts.
2. The payload (`boot/m_audit.S`) parks both snapshots in main work RAM;
   the libretro harness's `--dump-wram` lands them on the host (dump is
   16-bit byte-swapped; analyzer swaps back).

## Findings

- **All observed live BIOS variables sit in `$5800–$5FFF`**: 91 bytes
  changed A→B, every one in `$584A–$5E6F`. Interrupt vectors L1–L6 point
  at redirect stubs `$5F76–$5F94`; traps at `$5FA0+`; the `_CDBIOS`-style
  jump table occupies `$5F00–$5FFF`.
- **`$400–$5FF` changed zero bytes** — but it is NOT free: 419/512 bytes
  are nonzero and disassemble as real code (`lea $5F28,a0` / `jsr $5F28`
  sequences), and **the BIOS jump table dispatches into `$206`, `$20C`,
  `$20E`, `$5E8`, `$5FA`** — live dispatch stubs below `$600`.
- Conclusion: the BIOS's *working data* is compact (top ~2 KB), but its
  *code* occupies low PRG-RAM including the exact window the ST ABI needs
  for sysvars (`$400–$5FF`) and it owns the vector table contents.
  Overwriting `$400+` corrupts BIOS dispatch paths, not scratch.

## Consequences (now reflected in PLAN.md)

1. Boot loader (running under the BIOS, as today) must stage **everything**
   before takeover: EmuTOS image + the `A:` ramdisk image, in one pass.
2. Takeover: stop CD-related interrupt sources, install EmuTOS's vector
   table over `$0–$FF`, zero `$400+`, boot. The sub owns a clean machine
   with RAM at 0 — the whole point of the sub-CPU decision (D1).
3. No live CD access between takeover and the native CDD/CDC driver (E5).
   Interim drives: `A:` ramdisk (preloaded), `C:` backup RAM cart via the
   main-CPU servant — both BIOS-free paths.
4. Bonus reclaimed: `$0000–$5FFF` (24 KB) returns to ST-RAM instead of
   being a BIOS hole.

## Caveat

A 2-second dynamic sample is evidence, not proof, of the full BIOS
working set — but the decision doesn't hinge on it: the *code* overlap at
`$400–$5FF` alone rules out sharing, and that finding is static.
