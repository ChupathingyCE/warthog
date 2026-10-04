#!/usr/bin/env python3
"""Lay out the original Xbox build for the console's hard disk.

    python3 tools/xbox_package.py --maps assets/maps --out ~/Downloads/Warthog-xbox

writes OUT/default.xbe (build/xbox/default.xbe, from `ninja xbox`) and
OUT/maps with the maps folder's *.map and loading.tga, hard-linked where the
two folders share a disk (copied where they don't). Copy OUT to the console
as one folder, e.g. E:\\Games\\Warthog (port/xbox/README.md).

The maps are the user's own (the NTSC disc's, 01.10.12.2276); none are in
this repository.
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
    parser.add_argument("--xbe", type=Path, default=Path("build/xbox/default.xbe"))
    parser.add_argument("--maps", type=Path, default=Path("assets/maps"), help="the disc's maps folder")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    maps = args.maps.expanduser()
    out = args.out.expanduser()
    if not args.xbe.is_file():
        print(f"no {args.xbe}: run ninja xbox first", file=sys.stderr)
        return 1
    files = sorted(maps.glob("*.map")) + [p for p in [maps / "loading.tga"] if p.is_file()]
    if not any(p.name == "ui.map" for p in files):
        print(f"no ui.map in {maps}", file=sys.stderr)
        return 1
    (out / "maps").mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.xbe, out / "default.xbe")
    for path in files:
        place(path, out / "maps" / path.name)
    print(f"{out}: default.xbe and {len(files)} map files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
