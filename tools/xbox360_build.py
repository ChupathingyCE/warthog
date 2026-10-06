"""Ninja rules for an Xbox 360 (Xenon) build (``ninja xbox360``).

Target: PowerPC Xenon 32-bit big-endian with OXDK360 and llvm-xenon,
producing build/xbox360/default.xex.

Scaffolding only: this builds the stub in port/xbox360, which logs a line and
exits. None of the game is compiled for this console yet (docs/consoles.md).
"""

import os
import shlex
from pathlib import Path
from typing import Any, List, Optional

from .ninja_syntax import Writer

PORT_DIR = Path("port/xbox360")
CONSOLE_DIR = Path("port/console")

TARGET_TRIPLE = "powerpc-unknown-none-elf"

BASE_FLAGS = [
    "-target", TARGET_TRIPLE,
    "-c",
    "-ffreestanding",
    "-fno-builtin",
    "-fms-extensions",
    "-fms-compatibility",
    "-fms-compatibility-version=14",
    "-fshort-wchar",
    "-fno-autolink",
    "-O2",
    "-D_WIN32",
    "-D_M_PPCBE",
    "-D_M_PPC",
    "-D_PPC_",
    "-D_XBOX",
    "-D_XENON",
    "-DXBOX",
    "-DHALO_CONSOLE",
    "-DHALO_XBOX360_CONSOLE",
    "-DHALO_BIG_ENDIAN",
    "-Xclang", "-target-feature", "-Xclang", "+xenon-abi",
    "-w",
]


def _quote(path: Any) -> str:
    text = str(path).replace(os.sep, "/")
    return shlex.quote(text)


def _oxdk360_dir(sln: Any) -> Optional[Path]:
    candidates = [
        getattr(sln, "oxdk360_dir", None),
        os.environ.get("OXDK360_DIR"),
        Path.home() / "OXDK360",
        Path.home() / "OXDK" / "xbox360",
    ]
    for c in candidates:
        if c:
            p = Path(c).expanduser().resolve()
            if p.is_dir():
                return p
    return None


def _llvm_xenon_bin(sln: Any) -> Optional[Path]:
    candidates = [
        getattr(sln, "llvm_xenon", None),
        os.environ.get("LLVM_XENON_DIR"),
        Path.home() / "llvm-xenon",
    ]
    for c in candidates:
        if c:
            p = Path(c).expanduser().resolve()
            if (p / "build" / "bin" / "clang").is_file():
                return p / "build" / "bin"
            if (p / "bin" / "clang").is_file():
                return p / "bin"
    return None


def _x360_sdk_dir(sln: Any) -> Optional[Path]:
    candidates = [
        getattr(sln, "x360_sdk", None),
        os.environ.get("XDK_DIR"),
        os.environ.get("OXDK_XBOX360_XDK"),
        Path.home() / "xdk360-extract" / "sdk" / "XDK",
        Path.home() / "XDK360",
    ]
    for c in candidates:
        if c:
            p = Path(c).expanduser().resolve()
            if (p / "lib" / "xbox" / "xboxkrnl.lib").is_file():
                return p
    return None


def xbox360_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py."""
    return [
        Path(__file__),
        PORT_DIR / "src",
        PORT_DIR / "include",
        CONSOLE_DIR / "include",
    ]


def generate_xbox360_build(n: Writer, sln: Any) -> None:
    oxdk360 = _oxdk360_dir(sln)
    llvm_bin = _llvm_xenon_bin(sln)
    xdk = _x360_sdk_dir(sln)

    # no target without the toolchain and the user's own SDK (XDK_DIR):
    # oxdklink cannot make a XEX without it
    if not oxdk360 or not llvm_bin or not xdk:
        return

    clang = llvm_bin / "clang"
    oxdklink = oxdk360 / "tools" / "oxdklink" / "oxdklink.py"
    linker_script = oxdk360 / "oxdk360" / "title.ld"

    build_dir: Path = sln.build_dir / "xbox360"
    obj_dir = build_dir / "obj"
    xex = build_dir / "default.xex"

    n.comment("Xbox 360 build (ninja xbox360)")
    n.variable("xbox360_cc", _quote(clang))
    n.variable("xbox360_link", f"$python {_quote(oxdklink)}")

    n.rule(
        name="xbox360_cc",
        command="$xbox360_cc -MMD -MF $out.d $cflags -c $in -o $out",
        description="XBOX360 CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name="xbox360_xex",
        command="$xbox360_link $in -o $out $ldflags",
        description="XBOX360 XEX $out",
    )

    prefix_header = PORT_DIR / "include" / "halo_xbox360_prefix.h"
    common_inc = CONSOLE_DIR / "include"
    cflags = " ".join([
        *BASE_FLAGS,
        f"-include {_quote(prefix_header)}",
        f"-I{_quote(PORT_DIR / 'include')}",
        f"-I{_quote(common_inc)}",
    ])

    objects: List[Path] = []
    sources = sorted((PORT_DIR / "src").glob("*.c"))
    for source in sources:
        obj = obj_dir / f"{source.stem}.o"
        objects.append(obj)
        n.build(
            outputs=obj,
            rule="xbox360_cc",
            inputs=source,
            implicit=[prefix_header],
            variables={"cflags": cflags},
        )

    ldflags_list = [f"-T {_quote(linker_script)}", "--name default.exe", f"--xdk {_quote(xdk)}"]

    n.build(
        outputs=xex,
        rule="xbox360_xex",
        inputs=objects,
        implicit=[linker_script] if linker_script.is_file() else [],
        variables={"ldflags": " ".join(ldflags_list)},
    )
    n.build(outputs="xbox360", rule="phony", inputs=xex)
    n.newline()
