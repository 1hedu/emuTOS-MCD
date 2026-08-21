# The diagnostic image, and how to rebuild it

`EMUTOS2.IMG` and `EMUTOS3.IMG` on the disc are not the current port.
They are `51f65e5`'s EmuTOS -- the build that opens C: and lists its
files on hardware, and boots fast doing it -- with a diagnostic grafted
on and the read path untouched.

That build lives at submodule commit
`ed2aeb5a654eeb50006e93e5463e613a392f1f97`, which exists on no server.
The patch here is the graft, so the image can be rebuilt from the
submodule and this repository alone:

```sh
git clone --reference emutos https://github.com/emutos/emutos.git /tmp/diag
cd /tmp/diag
git checkout --detach ed2aeb5a654eeb50006e93e5463e613a392f1f97
git am /path/to/patches/diag/*.patch
make segacd ELF=1
```

Then put it on a disc alongside the current build:

```sh
EMUIMG2=/tmp/diag/emutos-segacd.img \
EMUIMG3=/tmp/diag/emutos-segacd.img \
EMUIMG4=/path/to/plain-51f65e5.img \
    tools/build-iso.sh U
```

`boot/sp.S` reads the servant's boot-flag word before it loads the
image and picks the filename from it: nothing held is `EMUTOS.IMG`,
Left is `EMUTOS2.IMG`, Up is `EMUTOS3.IMG`, Right is `EMUTOS4.IMG`.

## What it prints, and where it goes

Each round: Dfree, Dsetpath, Fsfirst, Fsnext, Fopen and Fread against
C:, then the driver's own seek, wait and error counts, its `why`
nibbles, and the sector a failed fetch last gave up on with a repeat
count.

Every line also goes into a 3.5 KB buffer that is written whole to
`B:\CDLOG.TXT` after each round -- whole rather than appended, so the
file is complete and closed at every moment the power might go off. The
screen shows `log N` (bytes written, or a GEMDOS error) and `bram N`:

| `bram` | meaning |
|---|---|
| 0 | no backup RAM answered at all |
| 1 | internal BRAM, EmuTOS format -- the log is being written |
| 2 | internal BRAM holds Sega-format game saves; EmuTOS will not touch it |
| 3 | a RAM cart answered, so B: is the cart |

State 2 is the one to watch for on a console whose internal BRAM has
been formatted by a game. Holding **C** at power-on once claims that
memory for EmuTOS and reformats it, which erases what is in there --
which is why the port does not do it unasked.

The buffer is deliberately smaller than the space available: the BRAM
filesystem is 512-byte sectors, one reserved, two one-sector FATs and
two sectors of root directory out of sixteen, leaving eleven data
sectors -- 5632 bytes. When the buffer fills the file stops changing
and the screen says so.
