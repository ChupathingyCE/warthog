"""Ninja rules for a Nintendo Switch build (``ninja switch``).

Target: ARM Cortex-A57 (AArch64) 64-bit little-endian with devkitA64 and libnx,
producing build/switch/halo.nro.

Scaffolding only: this builds the stub in port/switch, which logs a line and
exits. None of the game is compiled for this console yet (docs/consoles.md).
"""

import os
import shlex
from pathlib import Path
from typing import Any, List, Optional

from .ninja_syntax import Writer

PORT_DIR = Path("port/switch")
CONSOLE_DIR = Path("port/console")

BASE_FLAGS = [
    "-march=armv8-a+crc+crypto",
    "-mtune=cortex-a57",
    "-mtp=soft",
    "-fPIC",
    "-ftls-model=local-exec",
    "-O2",
    "-Wall",
    "-D__SWITCH__",
    "-DHALO_CONSOLE",
    "-DHALO_SWITCH_CONSOLE",
]


def _quote(path: Any) -> str:
    text = str(path).replace(os.sep, "/")
    return shlex.quote(text)


def _devkita64_dir(sln: Any) -> Optional[Path]:
    candidates = [
        getattr(sln, "devkita64_dir", None),
        os.environ.get("DEVKITA64"),
        Path(os.environ.get("DEVKITPRO", "")) / "devkitA64" if "DEVKITPRO" in os.environ else None,
        Path("/opt/devkitpro/devkitA64"),
    ]
    for c in candidates:
        if c:
            p = Path(c).expanduser().resolve()
            if (p / "bin" / "aarch64-none-elf-gcc").is_file():
                return p
    return None


def _libnx_dir(sln: Any) -> Optional[Path]:
    candidates = [
        getattr(sln, "libnx_dir", None),
        Path(os.environ.get("DEVKITPRO", "")) / "libnx" if "DEVKITPRO" in os.environ else None,
        Path("/opt/devkitpro/libnx"),
    ]
    for c in candidates:
        if c:
            p = Path(c).expanduser().resolve()
            if (p / "include").is_dir():
                return p
    return None


def _elf2nro(sln: Any) -> Optional[Path]:
    candidates = [
        Path(os.environ.get("DEVKITPRO", "")) / "tools" / "bin" / "elf2nro" if "DEVKITPRO" in os.environ else None,
        Path("/opt/devkitpro/tools/bin/elf2nro"),
    ]
    for c in candidates:
        if c and Path(c).is_file():
            return Path(c)
    return None


def switch_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py."""
    return [
        Path(__file__),
        PORT_DIR / "src",
        PORT_DIR / "include",
        CONSOLE_DIR / "include",
    ]


def generate_switch_build(n: Writer, sln: Any) -> None:
    devkita64 = _devkita64_dir(sln)
    libnx = _libnx_dir(sln)
    elf2nro = _elf2nro(sln)

    if not devkita64 or not libnx:
        return

    gcc = devkita64 / "bin" / "aarch64-none-elf-gcc"

    build_dir: Path = sln.build_dir / "switch"
    obj_dir = build_dir / "obj"
    elf = build_dir / "halo.elf"
    nro = build_dir / "halo.nro"

    n.comment("Nintendo Switch build (ninja switch)")
    n.variable("switch_cc", _quote(gcc))
    n.variable("switch_link", _quote(gcc))

    n.rule(
        name="switch_cc",
        command="$switch_cc -MMD -MF $out.d $cflags -c $in -o $out",
        description="SWITCH CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name="switch_elf",
        command="$switch_link $in -o $out $ldflags",
        description="SWITCH LINK $out",
    )

    prefix_header = PORT_DIR / "include" / "halo_switch_prefix.h"
    common_inc = CONSOLE_DIR / "include"
    cflags = " ".join([
        *BASE_FLAGS,
        f"-include {_quote(prefix_header)}",
        f"-I{_quote(PORT_DIR / 'include')}",
        f"-I{_quote(common_inc)}",
        f"-I{_quote(libnx / 'include')}",
    ])

    objects: List[Path] = []
    sources = sorted((PORT_DIR / "src").glob("*.c"))
    for source in sources:
        obj = obj_dir / f"{source.stem}.o"
        objects.append(obj)
        n.build(
            outputs=obj,
            rule="switch_cc",
            inputs=source,
            implicit=[prefix_header],
            variables={"cflags": cflags},
        )

    ldflags = f"-specs={_quote(libnx / 'switch.specs')} -L{_quote(libnx / 'lib')} -lnx"
    n.build(
        outputs=elf,
        rule="switch_elf",
        inputs=objects,
        variables={"ldflags": ldflags},
    )

    if elf2nro:
        n.rule(
            name="switch_nro",
            command=f"{_quote(elf2nro)} $in $out --nacp=$nacp --icon=$icon",
            description="SWITCH NRO $out",
        )
        n.build(outputs=nro, rule="switch_nro", inputs=elf)
        n.build(outputs="switch", rule="phony", inputs=nro)
    else:
        n.build(outputs="switch", rule="phony", inputs=elf)

    n.newline()
