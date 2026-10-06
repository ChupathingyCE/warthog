# Switch

Scaffolding, not a port yet: `ninja switch` builds a stub (`build/switch/halo.nro`) that logs
a line and exits. None of the game is compiled for this console, and it has
not run on hardware. docs/consoles.md has the status of every console and
what each needs first.

## Requirements

devkitPro's devkitA64 and libnx (`/opt/devkitpro`, or `--devkita64 DIR` and `--libnx DIR`), with `elf2nro`.

## Build

```sh
python3 configure.py --devkita64 /opt/devkitpro/devkitA64 --libnx /opt/devkitpro/libnx
ninja switch
python3 tools/switch_package.py --maps /path/to/maps --out ~/Downloads/Warthog-switch
```

## Testing

Ryujinx, then Atmosphere's Homebrew Menu on a console (`sd:/switch/halo/`).

## Plan

Little-endian and 64-bit: the closest to the desktop builds (the 64-bit work, `HALO_64BIT`, and the Android port's arm64 plans).
