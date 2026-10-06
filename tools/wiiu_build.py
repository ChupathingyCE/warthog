"""Ninja rules for a Nintendo Wii U build (``ninja wiiu``).

Target: PowerPC Espresso (750CL) 32-bit big-endian with devkitPPC and wut,
producing build/wiiu/halo.rpx.

Scaffolding only: this builds the stub in port/wiiu, which logs a line and
exits. None of the game is compiled for this console yet (docs/consoles.md).
"""

import os
import shlex
from pathlib import Path
from typing import Any, List, Optional

from .ninja_syntax import Writer

PORT_DIR = Path("port/wiiu")
CONSOLE_DIR = Path("port/console")

BASE_FLAGS = [
    "-mcpu=750",
    "-meabi",
    "-mhard-float",
    "-O2",
    "-Wall",
    "-D__WIIU__",
    "-D__WUT__",
    "-DHALO_CONSOLE",
    "-DHALO_WIIU_CONSOLE",
    "-DHALO_BIG_ENDIAN",
]


def _quote(path: Any) -> str:
    text = str(path).replace(os.sep, "/")
    return shlex.quote(text)


def _devkitppc_dir(sln: Any) -> Optional[Path]:
    candidates = [
        getattr(sln, "devkitppc_dir", None),
        os.environ.get("DEVKITPPC"),
        Path(os.environ.get("DEVKITPRO", "")) / "devkitPPC" if "DEVKITPRO" in os.environ else None,
        Path("/opt/devkitpro/devkitPPC"),
    ]
    for c in candidates:
        if c:
            p = Path(c).expanduser().resolve()
            if (p / "bin" / "powerpc-eabi-gcc").is_file():
                return p
    return None


def _wut_dir(sln: Any) -> Optional[Path]:
    candidates = [
        getattr(sln, "wut_dir", None),
        Path(os.environ.get("DEVKITPRO", "")) / "wut" if "DEVKITPRO" in os.environ else None,
        Path("/opt/devkitpro/wut"),
    ]
    for c in candidates:
        if c:
            p = Path(c).expanduser().resolve()
            if (p / "include").is_dir():
                return p
    return None


def _elf2rpl(sln: Any) -> Optional[Path]:
    candidates = [
        Path(os.environ.get("DEVKITPRO", "")) / "tools" / "bin" / "elf2rpl" if "DEVKITPRO" in os.environ else None,
        Path("/opt/devkitpro/tools/bin/elf2rpl"),
    ]
    for c in candidates:
        if c and Path(c).is_file():
            return Path(c)
    return None


def wiiu_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py."""
    return [
        Path(__file__),
        PORT_DIR / "src",
        PORT_DIR / "include",
        CONSOLE_DIR / "include",
    ]


def generate_wiiu_build(n: Writer, sln: Any) -> None:
    devkitppc = _devkitppc_dir(sln)
    wut = _wut_dir(sln)
    elf2rpl = _elf2rpl(sln)

    if not devkitppc or not wut:
        return

    gcc = devkitppc / "bin" / "powerpc-eabi-gcc"

    build_dir: Path = sln.build_dir / "wiiu"
    obj_dir = build_dir / "obj"
    elf = build_dir / "halo.elf"
    rpx = build_dir / "halo.rpx"

    n.comment("Nintendo Wii U build (ninja wiiu)")
    n.variable("wiiu_cc", _quote(gcc))
    n.variable("wiiu_link", _quote(gcc))

    devkitpro_dir = devkitppc.parent
    env_prefix = f"DEVKITPRO={_quote(devkitpro_dir)} "

    n.rule(
        name="wiiu_cc",
        command=f"{env_prefix}$wiiu_cc -MMD -MF $out.d $cflags -c $in -o $out",
        description="WIIU CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name="wiiu_elf",
        command=f"{env_prefix}$wiiu_link $in -o $out $ldflags",
        description="WIIU LINK $out",
    )

    prefix_header = PORT_DIR / "include" / "halo_wiiu_prefix.h"
    common_inc = CONSOLE_DIR / "include"
    cflags = " ".join([
        *BASE_FLAGS,
        f"-include {_quote(prefix_header)}",
        f"-I{_quote(PORT_DIR / 'include')}",
        f"-I{_quote(common_inc)}",
        f"-I{_quote(wut / 'include')}",
    ])

    objects: List[Path] = []
    sources = sorted((PORT_DIR / "src").glob("*.c"))
    for source in sources:
        obj = obj_dir / f"{source.stem}.o"
        objects.append(obj)
        n.build(
            outputs=obj,
            rule="wiiu_cc",
            inputs=source,
            implicit=[prefix_header],
            variables={"cflags": cflags},
        )

    specs_file = wut / "share" / "wut.specs"
    specs_flag = f"-specs={_quote(specs_file)} " if specs_file.is_file() else ""
    ldflags = f"{specs_flag}-L{_quote(wut / 'lib')} -lwut"
    n.build(
        outputs=elf,
        rule="wiiu_elf",
        inputs=objects,
        variables={"ldflags": ldflags},
    )

    if elf2rpl:
        n.rule(
            name="wiiu_rpx",
            command=f"{_quote(elf2rpl)} $in $out",
            description="WIIU RPX $out",
        )
        n.build(outputs=rpx, rule="wiiu_rpx", inputs=elf)
        n.build(outputs="wiiu", rule="phony", inputs=rpx)
    else:
        n.build(outputs="wiiu", rule="phony", inputs=elf)

    n.newline()
