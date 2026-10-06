#!/usr/bin/env python3
"""Lay out the Nintendo Wii U build for SD card or Cemu emulator.

    python3 tools/wiiu_package.py --maps assets/maps --out ~/Downloads/Warthog-wiiu

writes OUT/halo.rpx (build/wiiu/halo.rpx, from `ninja wiiu`) and
OUT/maps with the maps folder's *.map and loading.tga. Copy OUT to the Wii U
SD card at sd:/wiiu/apps/halo/ or launch directly in Cemu.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


def place(source: Path, destination: Path) -> None:
    if destination.exists():
        destination.unlink()
    try:
        os.link(source, destination)
    except OSError:
        shutil.copy2(source, destination)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rpx", type=Path, default=Path("build/wiiu/halo.rpx"))
    parser.add_argument("--maps", type=Path, default=Path("assets/maps"), help="the disc's maps folder")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--wuhb", action="store_true", help="also generate a .wuhb bundle of the stub (never the maps) using wuhbtool")
    args = parser.parse_args()

    maps = args.maps.expanduser()
    out = args.out.expanduser()
    if not args.rpx.is_file():
        print(f"no {args.rpx}: run ninja wiiu first", file=sys.stderr)
        return 1
    files = sorted(maps.glob("*.map")) + [p for p in [maps / "loading.tga"] if p.is_file()]
    if not any(p.name == "ui.map" for p in files):
        print(f"no ui.map in {maps}", file=sys.stderr)
        return 1
    (out / "maps").mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.rpx, out / "halo.rpx")
    for path in files:
        place(path, out / "maps" / path.name)

    # (the bundle holds the stub only: the maps are the user's own and never go
    # into a file that could be passed around; they stay a folder beside it)
    if args.wuhb:
        wuhbtool = shutil.which("wuhbtool") or "/opt/devkitpro/tools/bin/wuhbtool"
        if os.path.isfile(wuhbtool):
            wuhb_path = out / "halo.wuhb"
            cmd = [
                wuhbtool,
                str(args.rpx),
                str(wuhb_path),
                "--name=Halo: Combat Evolved (Warthog)",
                "--author=ChupathingyCE",
            ]
            try:
                subprocess.run(cmd, check=True)
                print(f"bundled {wuhb_path}")
            except subprocess.SubprocessError as e:
                print(f"wuhbtool warning: {e}", file=sys.stderr)

    print(f"{out}: halo.rpx and {len(files)} map files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
