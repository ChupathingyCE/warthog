# Xbox 360

Scaffolding, not a port yet: `ninja xbox360` builds a stub (`build/xbox360/default.xex`) that logs
a line and exits. None of the game is compiled for this console, and it has
not run on hardware. docs/consoles.md has the status of every console and
what each needs first.

## Requirements

[OXDK360](https://github.com/MrMilenko/OXDK360) in `~/OXDK360` (`--oxdk360 DIR`), its patched clang in `~/llvm-xenon` (`--llvm-xenon DIR`), and your own extracted Xbox 360 SDK (`XDK_DIR`). Without the SDK there is no target. Nothing from the SDK is in this repository, and none of it may be committed.

## Build

```sh
python3 configure.py --oxdk360 ~/OXDK360 --llvm-xenon ~/llvm-xenon
ninja xbox360
```

## Testing

Xenia runs unsigned XEX files; then an RGH or JTAG console through a launcher such as Aurora.

## Plan

The plan is the top-level README's "Xbox 360": big-endian (tag fields swapped as each tag loads, network fields in the message codec), alignment-safe readers, Direct3D 9 for Xenos, and 512 MB, so the PCs' capacities fit. First milestone: a XEX that logs a line in Xenia.
