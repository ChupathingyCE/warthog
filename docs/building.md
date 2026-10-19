# Building Warthog for the original Xbox

This is how the original Xbox build works today. It is built on macOS with
[OXDK](https://github.com/MrMilenko/OXDK), clang and lld-link. It should
build on Linux the same way, but that is untested. There is no Windows
build path yet; see "Help wanted: Windows tooling" in the README.

Nothing from Microsoft's SDKs is in this repository, and none of it may be
committed. You supply your own copies of the libraries and headers, and
the build reads them from where you keep them.

The quick way is `python3 tools/easy_build.py`: it checks everything below,
says what is missing and how to get it, asks a few questions (build type,
players, developer tools, packaging) and runs these steps. `--check` only
checks, and `--yes` with `--d3d8 PATH`, `--oxdk DIR`, `--players N`,
`--maps DIR` and `--out DIR` runs without questions. It remembers your
answers in `.easy_build.json` (ignored by git). The rest of this page is the
same thing by hand.

## What to install

| What | Where it goes | Tested with |
| --- | --- | --- |
| Python 3 | on `PATH` (3.7 or newer) | 3.9 (macOS's) |
| [ninja](https://ninja-build.org/) | on `PATH` | 1.13 |
| LLVM: clang and lld-link | macOS: `brew install llvm lld ninja`. The build looks in `LLVM_DIR`, then Homebrew's `llvm` folder, then `PATH` | Homebrew clang 22.1, LLD 22.1 |
| [OXDK](https://github.com/MrMilenko/OXDK), with its `cxbe` built | `~/OXDK` (or `--oxdk DIR`, or `OXDK_DIR`) | |
| The Xbox SDK's libraries and headers (the 5933 SDK) | OXDK's `xbox/xdk/lib` and `xbox/xdk/include`, set up as OXDK's README says | 5933 |
| An Aug 2001 Direct3D library, `d3d8ntpr.lib` (the `aug01` i386 build) | anywhere; you give its path to `configure.py` | |

The SDK comes as a Windows installer that installs into Microsoft Visual
Studio .NET. You install it (and a Visual Studio that it accepts) on a
Windows computer or virtual machine, then copy its libraries and headers
into OXDK. Why the build needs a separate Direct3D library from 2001 is in
`port/xbox/README.md`, "Direct3D".

## Build

```sh
python3 configure.py --xbox-d3d8 /path/to/d3d8ntpr.lib
ninja xbox
```

The result is `build/xbox/default.xbe`. `configure.py` records its
options, so later runs of `ninja` keep them. If it can't find OXDK or the
Direct3D library, there is no `xbox` target. The Linux, Windows and macOS
targets in `configure.py` are ChupathingyCE's and build as they do there.

## Host tests

```sh
port/xbox/tests/run.sh --xbox
```

This runs the console's parsers and rules on your computer under
AddressSanitizer and UndefinedBehaviorSanitizer: the game list, the
settings record's fold, `console.join`, the console's `config.toml`, the
memory class, XLink's addressing, the log's tags, co-op names and the package script's devkit flag. `--xbox` adds the Xbox build's own checks (it needs
`configure.py` to have been run with `--xbox-d3d8`). `--fuzz SECONDS` adds a
libFuzzer run (LLVM's clang), and `--fetch` fetches the real game list over
your computer's network.

## Package

```sh
python3 tools/xbox_package.py --maps /path/to/your/maps --out ~/Downloads/Warthog-xbox
```

This lays out one folder for the console's hard disk: `default.xbe`, your
own `maps/` (`*.map` and `loading.tga`, from your own disc; hard links
where it can, so they take no more space) and a `config.toml` with
`bypass_security = true`, which online play and play with the PC builds
need. `--secure` writes `false`, for System Link between consoles only.
`--game-list-server HOST[:PORT]` writes `game_list_server`, for a console
whose DNS can't find the list's host. A `config.toml` already in the folder
keeps its other settings; the game adds the rest when it starts. The folder is for your own console:
don't share it, because it holds your maps.

`--devkit-64mb` sets the flag in `default.xbe` that limits a development
kit's memory to 64 MB, so a 128 MB devkit runs the game with a stock
console's memory (a stock console ignores it). Stock 64 MB is the target, so
test with it on a devkit.

### Options for the build

`configure.py` options for the original Xbox build, all console-neutral in
name so other consoles can take them up:

| Option | What it does |
| --- | --- |
| `--xbox-d3d8 LIBRARY` | your `d3d8ntpr.lib`; without it there is no `xbox` target |
| `--oxdk DIR` | OXDK's checkout (default `OXDK_DIR`, or `~/OXDK`) |
| `--release` | assertions are not checked; a failed one is noted in `debug.txt` and the game goes on |
| `--console-players {16,32,64}` | the players the network tables hold. 16 is the Xbox's. 32 and 64 are test builds a stock 64 MB console does not hold; they say so on screen at the main menu |
| `--console-debug-tools` | keeps the debug decomp's developer tools (profiler, AI, render and collision debug, stack walker, libtiff and the bitmap tools), about 0.9 MB, which the build leaves out by default |

`xbox_package.py --devkit-64mb` is the packaging side of the 64 MB target:
see "Package" above.

### Other consoles

The Xbox 360 (`ninja xbox360`, `--oxdk360`, `--llvm-xenon`), Wii U (`ninja
wiiu`, `--devkitppc`, `--wut`) and Switch (`ninja switch`, `--devkita64`,
`--libnx`) targets are scaffolding: each builds a stub that logs a line and
exits, and none of the game is compiled for them yet. The Xbox 360 needs
your own 360 SDK as well (docs/consoles.md). A target appears only when
`configure.py` finds its toolchain.

## Onto the console

The XBE is built for retail mode. It runs on a modified retail console or
on a development kit.

1. Copy the folder to the console's hard disk, for example to
   `E:\Games\Warthog\`. On a modified console, use the FTP server of its
   dashboard. On a development kit, the XDK's Windows tools (Xbox
   Neighborhood, `xbcp`) copy it too (untested with this build).
2. Start `default.xbe` from the dashboard.
3. The game writes its log to `D:\debug.txt` (`D:\` is the XBE's folder).
   On a development kit, the debug monitor (XBDM) shows its `halo:` lines
   too.

The settings in `D:\config.toml` (`bypass_security`, `join`, `trace` and
the rest) are in the README, "Settings on the console", and attach
`debug.txt` to bug reports. xemu (128 MB, NAT) can run the build too:
README, "Testing". Questions go to the #warthog-support forum on the
Discord.
