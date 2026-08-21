# The EmuTOS port, as patches

`emutos/` is a submodule pointing at upstream `emutos/emutos`, and the
port lives on a local `segacd` branch there. The parent repository
records that branch's commit id -- an id that exists on no server, so a
fresh `git clone --recursive` of this repository gets upstream EmuTOS
and none of the work. Every line of the machine layer, the CD driver
and the disk plumbing was recoverable only from one container's disk.

So the branch is exported here as well, as a patch series against the
upstream commit it is based on. To rebuild the tree from nothing:

```sh
git submodule update --init emutos
cd emutos
git checkout -b segacd a084e52da9556baf1470b2ffc865af2377923d37
git am ../patches/emutos/*.patch
```

Refresh it after adding commits to the submodule:

```sh
rm -f patches/emutos/*.patch
cd emutos && git format-patch -o ../patches/emutos --zero-commit \
    --no-signature a084e52da9556baf1470b2ffc865af2377923d37..HEAD
```

The base is upstream, unmodified. Nothing here is EmuTOS's code
redistributed -- these are the diffs that make it run on a Mega CD, and
they carry EmuTOS's own licence, which is GPL v2.
