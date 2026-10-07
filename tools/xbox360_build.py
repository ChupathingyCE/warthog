"""Ninja rules for an Xbox 360 (Xenon) build (``ninja xbox360``).

Target: PowerPC Xenon 32-bit big-endian, compiled by OXDK360's patched clang
(the Xenon calling convention) against your own Xbox 360 SDK, linked and
wrapped by OXDK360's oxdklink into build/xbox360/default.xex.

Phase 0 (port/xbox360/README.md): port/xbox360/src is a boot test that logs
the console's memory and platform facts. None of the game is compiled for
this console yet (docs/consoles.md).

The flags are OXDK360's C path (oxdk360.mk). Nothing from the SDK belongs in
this repository.
"""

import os
import shlex
from pathlib import Path
from typing import Any, List, Optional

from .ninja_syntax import Writer

PORT_DIR = Path("port/xbox360")
CONSOLE_DIR = Path("port/console")

TARGET_TRIPLE = "powerpc-unknown-none-elf"

# oxdk360.mk's C flags: the SDK's headers are MSVC 2005's (_MSC_VER 1400; its
# _MSC_FULL_VER picks the vadefs.h va_start clang implements), _PPC_ and
# _M_PPCBE select their PowerPC blocks, wchar_t is 16 bits, and the Xenon
# calling convention is the patched clang's xenon-abi feature
BASE_FLAGS = [
    "-target", TARGET_TRIPLE,
    "-ffreestanding",
    "-fno-builtin",
    "-fms-extensions",
    "-fms-compatibility",
    "-fms-compatibility-version=14",
    "-fshort-wchar",
    "-fno-autolink",
    "-Wno-invalid-token-paste",
    "-D_WIN32",
    "-D_M_PPCBE",
    "-D_M_PPC",
    "-D_PPC_",
    "-D_XBOX",
    "-D_XENON",
    "-DXBOX",
    "-D_MSC_VER=1400",
    "-D_MSC_FULL_VER=140050727",
    "-D_SIZE_T_DEFINED",
    "-D_W64=",
    # SAL 2 annotations clang would read as K&R prototypes
    "'-D_Releases_lock_(x)='",
    "-Xclang", "-target-feature", "-Xclang", "+xenon-abi",
    "-O2",
]

# the phase 0 boot test's warnings stay on: on this big-endian ABI a pointer
# or integer mismatch is a real bug (OXDK360 ports)
PORT_FLAGS = [
    "-Wall",
    "-Wno-unknown-pragmas",
    "-Wno-microsoft-anon-tag",
    "-Werror=incompatible-pointer-types",
    "-Werror=int-conversion",
    "-Werror=implicit-function-declaration",
    "-Werror=format",
]

# what the XEX links: XAM and the kernel come in as imports through xapilib
# and xboxkrnl
XDK_LIBS = "xapilib,d3d9,xgraphics,xboxkrnl,xnet,oldnames,libcMT"
XEX_BASE = "0x82000000"
XEX_STACK = "0x100000"


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
        # OXDK360's own search (tools/common/toolchain.py)
        Path.home() / "xdk360" / "XDK",
        Path.home() / "xdk360-extract" / "sdk" / "XDK",
        Path("/opt/xdk360/XDK"),
    ]
    for c in candidates:
        if c:
            p = Path(c).expanduser().resolve()
            if (p / "lib" / "xbox" / "xboxkrnl.lib").is_file():
                return p
    return None


def _llvm_tool(llvm_bin: Path, name: str) -> Path:
    for directory in (llvm_bin, Path("/opt/homebrew/opt/llvm/bin"), Path("/usr/local/opt/llvm/bin")):
        if (directory / name).is_file():
            return directory / name
    return Path(name)


def xbox360_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py."""
    return [
        Path(__file__),
        PORT_DIR / "src",
        PORT_DIR / "include",
        CONSOLE_DIR / "include",
    ]


def _missing(n: Writer, missing: List[str]) -> None:
    """`ninja xbox360` says what is missing rather than not existing"""
    text = "; ".join(missing)
    n.comment("Xbox 360 build (ninja xbox360): not available")
    n.rule(
        name="xbox360_missing",
        command=f"echo {shlex.quote('ninja xbox360 needs: ' + text + ' (port/xbox360/README.md)')} && exit 1",
        description="XBOX360 not configured",
    )
    n.build(outputs="xbox360", rule="xbox360_missing")
    n.newline()


def generate_xbox360_build(n: Writer, sln: Any) -> None:
    oxdk360 = _oxdk360_dir(sln)
    llvm_bin = _llvm_xenon_bin(sln)
    xdk = _x360_sdk_dir(sln)

    missing = []
    if not oxdk360:
        missing.append("OXDK360 (--oxdk360 or OXDK360_DIR)")
    if not llvm_bin:
        missing.append("the patched clang (--llvm-xenon or LLVM_XENON_DIR)")
    if not xdk:
        missing.append("your extracted Xbox 360 SDK with lib/xbox/xboxkrnl.lib (--xdk360 or XDK_DIR)")
    if missing:
        _missing(n, missing)
        return

    clang = llvm_bin / "clang"
    objcopy = _llvm_tool(llvm_bin, "llvm-objcopy")
    oxdklink = oxdk360 / "tools" / "oxdklink" / "oxdklink.py"
    cxex = oxdk360 / "tools" / "cxex" / "cxex"
    linker_script = oxdk360 / "oxdk360" / "title.ld"
    builtins = oxdk360 / "oxdk360" / "builtins" / "builtins.c"

    build_dir: Path = sln.build_dir / "xbox360"
    obj_dir = build_dir / "obj"
    gen_dir = build_dir / "gen"
    xex = build_dir / "default.xex"
    stamp = gen_dir / "xbox360_build_stamp.h"

    n.comment("Xbox 360 build (ninja xbox360)")
    n.variable("xbox360_cc", _quote(clang))
    n.variable("xbox360_objcopy", _quote(objcopy))
    n.variable("xbox360_link", f"$python {_quote(oxdklink)}")

    # (MS compatibility records #pragma comment(lib) in .deplibs, which
    # ld.lld would try to honor: oxdk360.mk strips it the same way)
    n.rule(
        name="xbox360_cc",
        command="$xbox360_cc -MMD -MF $out.d $cflags -c $in -o $out && $xbox360_objcopy --remove-section=.deplibs $out",
        description="XBOX360 CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name="xbox360_xex",
        command="$xbox360_link $in -o $out $ldflags",
        description="XBOX360 XEX $out",
    )
    n.rule(
        name="xbox360_stamp",
        command="$python tools/xbox360_stamp.py $out",
        description="XBOX360 STAMP $out",
    )

    # the stamp is written on every build: the XEX's first log line says
    # which build ran
    n.build(outputs="xbox360_always", rule="phony")
    n.build(outputs=stamp, rule="xbox360_stamp", implicit=["xbox360_always", Path("tools/xbox360_stamp.py")])

    sdk_includes = [
        f"-I{_quote(oxdk360 / 'oxdk360' / 'header-shim')}",
        f"-I{_quote(xdk / 'include' / 'xbox')}",
        f"-I{_quote(xdk / 'TechPreview' / 'Jul12Compiler' / 'include' / 'xbox')}",
    ]
    cflags = " ".join([*BASE_FLAGS, *PORT_FLAGS, *sdk_includes, f"-I{_quote(gen_dir)}"])
    # OXDK360's own unit: its flags only (oxdk360.mk)
    builtins_cflags = " ".join([*BASE_FLAGS, *sdk_includes])

    objects: List[Path] = []
    for source in sorted((PORT_DIR / "src").glob("*.c")):
        obj = obj_dir / f"{source.stem}.o"
        objects.append(obj)
        n.build(outputs=obj, rule="xbox360_cc", inputs=source, order_only=[stamp], variables={"cflags": cflags})
    builtins_obj = obj_dir / "oxdk360_builtins.o"
    objects.append(builtins_obj)
    n.build(outputs=builtins_obj, rule="xbox360_cc", inputs=builtins, variables={"cflags": builtins_cflags})

    ldflags_list = [
        f"-T {_quote(linker_script)}",
        f"--xdk {_quote(xdk)}",
        f"--libs {XDK_LIBS}",
        f"--base {XEX_BASE}",
        f"--stack {XEX_STACK}",
        "--name default.exe",
    ]

    n.build(
        outputs=xex,
        rule="xbox360_xex",
        inputs=objects,
        implicit=[p for p in (linker_script, cxex, oxdklink) if p.is_file()],
        variables={"ldflags": " ".join(ldflags_list)},
    )
    n.build(outputs="xbox360", rule="phony", inputs=xex)
    n.newline()
