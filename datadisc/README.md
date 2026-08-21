# The data disc

Anything dropped in here lands on **D:** of the data disc, and nowhere near
the base EmuTOS disc.

    tools/build-datadisc.sh U

builds `build/emutosmd-data-U.iso`. Its boot half is identical to the base
disc's, so the data disc can be booted on its own — or the base disc can be
booted and this one swapped in with EJECT.PRG, which is the same thing from
the other end.

The split exists because the boot disc is a system disk and should stay one.
An application belongs on a disc you can rebuild and reburn without touching
the thing that starts the machine.

Names are 8.3 and are upper-cased on the way in, because D: is FAT16 and TOS
wants them that way. Subdirectories are not descended: this is one flat drive.

Nothing in here is committed except this file — the same rule `vendor/` follows.
The pipeline is ours; what you put through it is yours.
