#!/usr/bin/env python3
"""Lay out the Xbox 360 build for console or emulator.

    python3 tools/xbox360_package.py --maps assets/maps --out ~/Downloads/Warthog-360

writes OUT/default.xex (build/xbox360/default.xex, from `ninja xbox360`) and
OUT/maps with the maps folder's *.map and loading.tga. Copy OUT to the Xbox
360 hard drive (e.g. Hdd1:\\Games\\Warthog\\) or launch directly in Xenia.
"""

import argparse
import os
import shutil
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
    parser.add_argument("--xex", type=Path, default=Path("build/xbox360/default.xex"))
    parser.add_argument("--maps", type=Path, default=Path("assets/maps"), help="the disc's maps folder")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    maps = args.maps.expanduser()
    out = args.out.expanduser()
    if not args.xex.is_file():
        print(f"no {args.xex}: run ninja xbox360 first", file=sys.stderr)
        return 1
    files = sorted(maps.glob("*.map")) + [p for p in [maps / "loading.tga"] if p.is_file()]
    if not any(p.name == "ui.map" for p in files):
        print(f"no ui.map in {maps}", file=sys.stderr)
        return 1
    (out / "maps").mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.xex, out / "default.xex")
    for path in files:
        place(path, out / "maps" / path.name)
    print(f"{out}: default.xex and {len(files)} map files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
