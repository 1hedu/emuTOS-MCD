#!/usr/bin/env bash
# Build the m68k-elf cross-toolchain EmuTOS's ELF=1 path expects.
#
# Why from source: Ubuntu's m68k-linux-gnu gcc ICEs on -mshort (reload
# form_sum, every version 10-14 — it's the linux-gnu target config, not a
# version regression), and its libgcc is 68020-encoded (bsr.l in __divsi3),
# which would crash a real 68000. The launchpad cross-mint PPA that EmuTOS
# CI uses is unreachable from some build environments, so we build the same
# era toolchain (binutils 2.42, gcc 13) from the FreeMiNT project's GitHub
# mirrors, targeting plain m68k-elf.
#
# Usage: tools/setup-toolchain.sh [prefix]   (default /opt/m68k-elf)
# Produces: $PREFIX/bin/m68k-elf-{gcc,as,ld,...}; add to PATH.
# Afterwards: cd emutos && make 192 ELF=1 UNIQUE=us
set -euo pipefail

PREFIX="${1:-/opt/m68k-elf}"
JOBS="$(nproc)"
WORK="${TOOLCHAIN_WORK:-$HOME/.cache/m68k-elf-build}"
# Vanilla binutils: the mintelf branch's atariprg BFD patches fail to link
# when configured for plain m68k-elf, so build from the pristine upstream
# tag, which the freemint fork carries.
BINUTILS_REPO=https://github.com/freemint/m68k-atari-mint-binutils-gdb
BINUTILS_BRANCH=binutils-2_42   # vanilla upstream tag, not the -mintelf branch
GCC_REPO=https://github.com/freemint/m68k-atari-mint-gcc
GCC_BRANCH=gnu/releases/gcc-13    # vanilla gcc 13 mirror branch

command -v "$PREFIX/bin/m68k-elf-gcc" >/dev/null 2>&1 && {
  echo "m68k-elf-gcc already at $PREFIX — nothing to do."; exit 0; }

sudo apt-get install -y -qq libgmp-dev libmpfr-dev libmpc-dev texinfo bison flex

mkdir -p "$WORK" && cd "$WORK"

# --- binutils ---------------------------------------------------------------
if [[ ! -x "$PREFIX/bin/m68k-elf-as" ]]; then
  [[ -d binutils ]] || git clone --depth 1 -b "$BINUTILS_BRANCH" "$BINUTILS_REPO" binutils
  mkdir -p binutils-build && cd binutils-build
  ../binutils/configure --target=m68k-elf --prefix="$PREFIX" \
    --disable-gdb --disable-gdbserver --disable-sim --disable-gprofng \
    --disable-werror --disable-nls
  make -j"$JOBS"
  sudo make install
  cd "$WORK"
fi

# --- gcc (C only, freestanding, with libgcc for the m68k multilibs) ---------
[[ -d gcc ]] || git clone --depth 1 -b "$GCC_BRANCH" "$GCC_REPO" gcc
mkdir -p gcc-build && cd gcc-build
../gcc/configure --target=m68k-elf --prefix="$PREFIX" \
  --enable-languages=c --disable-nls --disable-shared --disable-threads \
  --disable-libssp --disable-libquadmath --disable-libatomic \
  --without-headers --with-newlib
make -j"$JOBS" all-gcc all-target-libgcc
sudo make install-gcc install-target-libgcc

echo "Done. $($PREFIX/bin/m68k-elf-gcc --version | head -1)"
echo 'Add to PATH:  export PATH='"$PREFIX"'/bin:$PATH'
