"""Writes the console build's stamp (port/xbox/src/xbox_memory.c logs it): the
git commit, whether the tree has changes, and the time. port/xbox/build.py
runs it on every `ninja xbox`, so the stamp is of this build, not of the last
time one file was compiled. Writes only when the text changed."""

import subprocess
import sys
import time
from pathlib import Path


def git(*arguments: str) -> str:
    try:
        return subprocess.run(["git", *arguments], capture_output=True, text=True, check=False).stdout.strip()
    except OSError:
        return ""


def main() -> None:
    output = Path(sys.argv[1])
    commit = git("rev-parse", "--short=9", "HEAD") or "unknown"
    dirty = "+changes" if git("status", "--porcelain", "--untracked-files=no") else ""
    stamp = f"{commit}{dirty} {time.strftime('%Y-%m-%d %H:%M:%S')}"
    text = f'const char xbox_build_stamp[] = "{stamp}";\n'
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.is_file() or output.read_text(encoding="utf-8") != text:
        output.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
