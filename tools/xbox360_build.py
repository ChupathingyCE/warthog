"""Ninja rules for the Xbox 360 build (``ninja xbox360``).

Target: PowerPC Xenon 32-bit big-endian, compiled by OXDK360's patched clang
(the Xenon calling convention) against your own Xbox 360 SDK, linked and
wrapped by OXDK360's oxdklink into build/xbox360/default.xex.

It is the Windows build's arrangement (tools/windows_build.py,
port/windows/README.md) on the console: the game's sources, the desktop
platform layer (port/linux/src) less what the console does its own way, and
port/xbox360/src. The game and port/linux/src see the Xbox SDK declarations
of port/include/xdk; port/xbox360/src/x360_*.c see the console's SDK. The
game has the 64-bit builds' pointer mode over a relocatable window of Xbox
addresses (HALO_64BIT, HALO_XBOX_WINDOW: source/cseries/xbox_address.h).

The flags are OXDK360's C path (oxdk360.mk). Nothing from the SDK belongs in
this repository.
"""

import json
import os
import shlex
from pathlib import Path
from typing import Any, List, Optional

from .embed_assets import hud_assets_build, hud_configure_inputs
from .linux_build import (EXPAT_DIR, KCP_DIR, MONOCYPHER_DIR, QRCODEGEN_DIR, TOML_DIR, XDK_INCLUDE, ZLIB_DEFINES,
                          ZLIB_DIR, ZLIB_SOURCES, game_defines_and_includes, game_sources, musl_math_sources,
                          xdk_headers)
from .lp64_build import lp64_excluded
from .ninja_syntax import Writer
from .server_build import SDL_INCLUDE, fetch_sdl_headers
from .version import version

PORT_DIR = Path("port/xbox360")
CONSOLE_DIR = Path("port/console")
LINUX_DIR = Path("port/linux")
WINDOWS_DIR = Path("port/windows")
MUSL_MATH_DIR = Path("port/third_party/musl-math")

TARGET_TRIPLE = "powerpc-unknown-none-elf"

# oxdk360.mk's C flags: the SDK's headers are MSVC 2005's (_MSC_VER 1400; its
# _MSC_FULL_VER picks the vadefs.h va_start clang implements), _PPC_ and
# _M_PPCBE select their PowerPC blocks, wchar_t is 16 bits, and the Xenon
# calling convention is the patched clang's xenon-abi feature
BASE_FLAGS = [
    "-target", TARGET_TRIPLE,
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
    # the SDK's 64-bit returns come back wrong from this compiler (the
    # register pair gap): the C runtime's 32-bit time_t
    "-D_USE_32BIT_TIME_T=1",
    # the C runtime headers' __inline functions are MSVC's pick-any COMDATs,
    # which an ELF target emits in every unit: each unit's own copy instead
    "'-D__inline=static __inline__'",
    "-Xclang", "-target-feature", "-Xclang", "+xenon-abi",
    # thread-local variables through x360_tls.c: the console's TLS is
    # Microsoft's compiler's, and ELF TLS reads through r2, which it leaves 0
    "-femulated-tls",
    # the Xenon core's square root instruction (the generic PowerPC has
    # none: every sqrtf was a call), and no errno from the math functions,
    # so sqrtf and fabsf are single instructions as on the other ports
    "-Xclang", "-target-feature", "-Xclang", "+fsqrt",
    "-fno-math-errno",
    "-O2",
]

# the console's own units, the third-party libraries and the SDK's helpers
# keep oxdk360.mk's freestanding build (x360_system.c defines C99 math the
# runtime lacks, which the compiler's builtins would turn into calls to
# themselves); the game and the platform layer have the builtins (memcpy of
# a small structure inlined, sqrtf an instruction), less the roundings whose
# float forms the runtime lacks (the compiler would make floorf of a float's
# floor) and stpcpy
FREESTANDING_FLAGS = ["-ffreestanding", "-fno-builtin"]
HOSTED_FLAGS = [f"-fno-builtin-{name}" for name in ("floor", "ceil", "fmod", "trunc", "round", "rint",
                                                     "nearbyint", "stpcpy")]

# the game's and the desktop platform layer's ABI, as the other ports give
# it (tools/linux_build.py): tentative definitions shared between units, no
# optimisations that assume the absence of MSVC-tolerated UB, the same
# floating point results on every port (no fused multiply-adds, which
# PowerPC would otherwise emit everywhere), and small structures and unions
# returned in registers as Win32 does (hs_runtime.c calls union-returning
# converters through pointers typed as returning long)
GAME_ABI_FLAGS = [
    "-fcommon",
    "-fno-strict-aliasing",
    "-fwrapv",
    "-fno-delete-null-pointer-checks",
    "-ffp-contract=off",
    "-msvr4-struct-return",
    # char is signed, as MSVC has it (PowerPC's is unsigned: a char holding
    # NONE would read 255)
    "-fsigned-char",
    "-DHALO_64BIT",
    "-DHALO_XBOX_WINDOW",
    "-DDEBUG",
]

GAME_FLAGS = [
    "-std=gnu89",
    "-w",
    "-Wno-error=incompatible-pointer-types",
    "-Wno-error=incompatible-function-pointer-types",
    "-Wno-error=int-conversion",
    "-Wno-error=implicit-function-declaration",
    "-Wno-error=implicit-int",
    "-Wno-error=return-type",
]

PLATFORM_FLAGS = [
    "-std=gnu11",
    "-DHALO_LINUX_PLATFORM_LAYER",
    "-Wall",
    "-Wno-unused-function",
    "-Wno-unknown-pragmas",
    "-Wno-microsoft-anon-tag",
    "-Wno-pragma-pack",
    "-Wno-ignored-attributes",
    "-Wno-duplicate-decl-specifier",
    "-Wno-missing-braces",
    "-Wno-unused-variable",
    "-Wno-unused-but-set-variable",
    "-Wno-ignored-pragmas",
    "-Wno-microsoft-enum-forward-reference",
    "-Wno-language-extension-token",
    "-Werror=incompatible-pointer-types",
    "-Werror=int-conversion",
    "-Werror=implicit-function-declaration",
]

# port/xbox360/src/x360_*.c talk to the console only: its SDK, warnings on
# (on this big-endian ABI a pointer or integer mismatch is a real bug)
CONSOLE_FLAGS = [
    "-std=gnu11",
    "-Wall",
    "-Wno-unknown-pragmas",
    "-Wno-microsoft-anon-tag",
    "-Wno-unused-function",
    "-Werror=incompatible-pointer-types",
    "-Werror=int-conversion",
    "-Werror=implicit-function-declaration",
    "-Werror=format",
]

# the desktop platform layer's units the console does its own way
# (port/xbox360/src), or has no use for
REPLACED_PLATFORM_SOURCES = {
    # the window, the input and the sound device (x360_platform.c,
    # x360_input.c; dsound_sdl.c's mixer stays, over x360_audio.c)
    "sdl_platform.c",
    "xinput_sdl.c",
    "touch_input.c",
    "touch_menu.c",
    # the C library's (the console's libcMT is MSVC's: windows_crt.c)
    "msvc_crt.c",
    "msvc_wide.c",
    # the host's files, sockets and processes (x360_files.c, x360_net.c,
    # x360_system.c)
    "posix_files.c",
    "posix_net.c",
    "posix_update.c",
    "posix_upnp.c",
    "posix_browser.c",
    "posix_ui_font.c",
    # page protection and the fault handler (x360_memory.c)
    "memory_watch.c",
    # the desktop crash reports: the console's faults go to its debug channel
    "posix_crash.c",
    # the self-updater: a console is updated with its files
    "updater.c",
    "update_signature.c",
}

# what the XEX links: XAM and the kernel come in as imports through xapilib
# and xboxkrnl
XDK_LIBS = "xapilib,d3d9,d3dx9,xgraphics,xaudio2,x3daudio,xmcore,xmahal,xboxkrnl,xnet,oldnames,libcMT"
XEX_BASE = "0x82000000"
# the game's main thread: the desktop builds give it 8 MB
XEX_STACK = "0x800000"


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
        CONSOLE_DIR / "game",
        LINUX_DIR / "src",
        LINUX_DIR / "game",
        *hud_configure_inputs(),
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
    if not missing and not fetch_sdl_headers():
        missing.append("SDL's headers (downloaded once into build/server/third_party)")
    if missing:
        _missing(n, missing)
        return

    linux_config = json.loads((LINUX_DIR / "port.json").read_text(encoding="utf-8"))

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
    semantics_header = gen_dir / "halo_msvc_semantics.h"
    platform_semantics_header = gen_dir / "platform_msvc_semantics.h"
    prefix_header = PORT_DIR / "include" / "halo_xbox360_prefix.h"

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
    n.rule(
        name="xbox360_msvc_semantics",
        command="$python tools/linux_msvc_semantics.py --output $out $scan",
        description="XBOX360 MSVC SEMANTICS $out",
        restat=True,
    )

    # the stamp is written on every build: the XEX's first log line says
    # which build ran
    n.build(outputs="xbox360_always", rule="phony")
    n.build(outputs=stamp, rule="xbox360_stamp", implicit=["xbox360_always", Path("tools/xbox360_stamp.py")])

    game_headers = sorted(p for p in Path("source").rglob("*") if p.suffix in (".c", ".h"))
    n.build(
        outputs=semantics_header,
        rule="xbox360_msvc_semantics",
        implicit=[Path("tools/linux_msvc_semantics.py"), *xdk_headers(), *game_headers],
        variables={"scan": f"--all-inlines --tags source --inlines source --inlines {XDK_INCLUDE}"},
    )
    n.build(
        outputs=platform_semantics_header,
        rule="xbox360_msvc_semantics",
        implicit=[Path("tools/linux_msvc_semantics.py"), *xdk_headers()],
        variables={"scan": f"--inlines {XDK_INCLUDE}"},
    )

    # the console's SDK: its own headers, then its compiler's C runtime
    sdk_includes = [
        f"-I{_quote(oxdk360 / 'oxdk360' / 'header-shim')}",
        f"-I{_quote(xdk / 'include' / 'xbox')}",
        f"-I{_quote(xdk / 'TechPreview' / 'Jul12Compiler' / 'include' / 'xbox')}",
    ]
    # the C runtime alone, behind the Xbox SDK declarations, for the game and
    # the desktop platform layer
    crt_includes = [
        f"-I{_quote(xdk / 'TechPreview' / 'Jul12Compiler' / 'include' / 'xbox')}",
        f"-I{_quote(xdk / 'include' / 'xbox')}",
    ]
    posix_include = PORT_DIR / "include" / "posix"
    windows_posix_include = WINDOWS_DIR / "include" / "posix"
    crt_wrappers = f"{PORT_DIR / 'include' / 'crt'} -I{WINDOWS_DIR / 'include' / 'crt'}"
    version_define = f"'-DHALO_VERSION=\"{version()}\"'"

    game_cflags = " ".join([
        *BASE_FLAGS,
        *HOSTED_FLAGS,
        *GAME_ABI_FLAGS,
        *GAME_FLAGS,
        f"-include {prefix_header}",
        f"-include {semantics_header}",
        f"-I{crt_wrappers}",
        f"-I{PORT_DIR / 'include'}",
        f"-I{CONSOLE_DIR / 'include'}",
        f"-iquote {LINUX_DIR / 'include'}",
        f"-iquote {Path(linux_config['game_sources'])}",
        game_defines_and_includes(linux_config),
        f"-I{XDK_INCLUDE}",
        *crt_includes,
    ])
    platform_cflags = " ".join([
        *BASE_FLAGS,
        *HOSTED_FLAGS,
        *GAME_ABI_FLAGS,
        *PLATFORM_FLAGS,
        version_define,
        f"-include {prefix_header}",
        f"-include {platform_semantics_header}",
        f"-I{posix_include}",
        f"-I{windows_posix_include}",
        f"-I{crt_wrappers}",
        f"-I{LINUX_DIR / 'src'}",
        f"-I{PORT_DIR / 'include'}",
        f"-I{TOML_DIR}",
        f"-I{EXPAT_DIR}",
        f"-I{KCP_DIR}",
        f"-I{QRCODEGEN_DIR}",
        f"-I{MONOCYPHER_DIR}",
        f"-I{ZLIB_DIR}",
        f"-iquote {LINUX_DIR / 'include'}",
        "-Isource -Isource/cseries",
        f"-I{SDL_INCLUDE}",
        f"-I{gen_dir}",
        f"-I{PORT_DIR / 'src'}",
        f"-I{XDK_INCLUDE}",
        *crt_includes,
    ])
    console_cflags = " ".join([
        *BASE_FLAGS,
        *FREESTANDING_FLAGS,
        "-DHALO_XBOX360=1",
        "-DHALO_64BIT",
        "-DHALO_XBOX_WINDOW",
        *CONSOLE_FLAGS,
        version_define,
        f"-I{posix_include}",
        f"-I{windows_posix_include}",
        f"-I{PORT_DIR / 'include' / 'crt'}",
        f"-I{LINUX_DIR / 'src'}",
        f"-I{PORT_DIR / 'include'}",
        f"-I{SDL_INCLUDE}",
        f"-I{gen_dir}",
        *sdk_includes,
    ])
    third_party_cflags = " ".join([*BASE_FLAGS, *FREESTANDING_FLAGS, *GAME_ABI_FLAGS, "-std=gnu11", "-w",
                                   f"-include {prefix_header}", f"-I{crt_wrappers}", f"-I{XDK_INCLUDE}",
                                   *crt_includes])
    implicit_common = [*xdk_headers(), prefix_header, PORT_DIR / "include" / "halo_xbox360_api_names.h"]

    objects: List[Path] = []

    def add_object(source: Path, cflags: str, implicit: List[Path]) -> None:
        obj = obj_dir / source.with_suffix(".o")
        objects.append(obj)
        n.build(outputs=obj, rule="xbox360_cc", inputs=source, implicit=implicit,
                order_only=[stamp], variables={"cflags": cflags})

    game_objects_start = len(objects)
    excluded = set(lp64_excluded())
    for source in game_sources(linux_config):
        if source.as_posix() not in excluded:
            add_object(source, game_cflags, [*implicit_common, semantics_header])
    for source in sorted(Path(linux_config["game_sources"]).glob("*.c")):
        add_object(source, game_cflags, [*implicit_common, semantics_header])
    # the consoles' shared game code: the maps' byte order
    for source in sorted((CONSOLE_DIR / "game").glob("*.c")):
        add_object(source, game_cflags, [*implicit_common, semantics_header])
    game_objects = objects[game_objects_start:]

    platform_objects_start = len(objects)
    for source in sorted((LINUX_DIR / "src").glob("*.c")):
        if source.name in REPLACED_PLATFORM_SOURCES:
            continue
        add_object(source, platform_cflags, [*implicit_common, platform_semantics_header])
    # the C runtime's file functions with Xbox paths, as on Windows
    add_object(WINDOWS_DIR / "src" / "windows_crt.c", platform_cflags,
               [*implicit_common, platform_semantics_header])
    # the dedicated server's few SDL functions (no SDL library on the
    # console): files, and the audio stream x360_audio.c plays
    for source in sorted((PORT_DIR / "src").glob("*.c")):
        if source.name.startswith("x360_"):
            add_object(source, console_cflags, [stamp])
        else:
            add_object(source, platform_cflags, [*implicit_common, platform_semantics_header])
    for source in hud_assets_build(n, "xbox360", gen_dir / "hud_hires_assets.c"):
        add_object(source, platform_cflags, [*implicit_common, platform_semantics_header])
    platform_objects = objects[platform_objects_start:]

    add_object(TOML_DIR / "tomlc17.c", third_party_cflags, [])
    # the menus' XML parser, as on Windows (its hash salt from rand_s), over
    # the console's SDK
    for name in ("xmlparse.c", "xmlrole.c", "xmltok.c", "random_rand_s.c"):
        add_object(EXPAT_DIR / name, " ".join([*BASE_FLAGS, *FREESTANDING_FLAGS, "-std=gnu11", "-w", f"-I{PORT_DIR / 'include' / 'sdk'}",
                                               f"-I{EXPAT_DIR}", *sdk_includes]), [])
    add_object(KCP_DIR / "ikcp.c", third_party_cflags, [])
    add_object(QRCODEGEN_DIR / "qrcodegen.c", third_party_cflags, [])
    for name in ("monocypher.c", "monocypher-ed25519.c"):
        add_object(MONOCYPHER_DIR / name, third_party_cflags, [])
    for name in ZLIB_SOURCES:
        add_object(ZLIB_DIR / name, f"{third_party_cflags} {' '.join(ZLIB_DEFINES)}", [])
    for source in musl_math_sources():
        add_object(source, f"{third_party_cflags} -I{MUSL_MATH_DIR}/include "
                           f"-include {MUSL_MATH_DIR}/include/libm.h", [])

    builtins_obj = obj_dir / "oxdk360_builtins.o"
    objects.append(builtins_obj)
    n.build(outputs=builtins_obj, rule="xbox360_cc", inputs=builtins,
            variables={"cflags": " ".join([*BASE_FLAGS, *FREESTANDING_FLAGS, *sdk_includes])})

    # each part on its own, to see what compiles
    n.build(outputs="xbox360-game-objects", rule="phony", inputs=game_objects)
    n.build(outputs="xbox360-platform-objects", rule="phony", inputs=platform_objects)

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
