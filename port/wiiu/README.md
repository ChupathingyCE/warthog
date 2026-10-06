# Wii U

Scaffolding, not a port yet: `ninja wiiu` builds a stub (`build/wiiu/halo.rpx`) that logs
a line and exits. None of the game is compiled for this console, and it has
not run on hardware. docs/consoles.md has the status of every console and
what each needs first.

## Requirements

devkitPro's devkitPPC and wut (`/opt/devkitpro`, or `--devkitppc DIR` and `--wut DIR`), with `elf2rpl`; `wuhbtool` for `--wuhb`.

## Build

```sh
python3 configure.py --devkitppc /opt/devkitpro/devkitPPC --wut /opt/devkitpro/wut
ninja wiiu
python3 tools/wiiu_package.py --maps /path/to/maps --out ~/Downloads/Warthog-wiiu
```

## Testing

Cemu, then Aroma on a console (`sd:/wiiu/apps/halo/`).

## Plan

Big-endian, like the Xbox 360: the same byte-order and alignment work comes first. `--wuhb` bundles the stub only, never the maps.
