# Building Warthog for the original Xbox

This is how the original Xbox build works today. It is built on macOS with
[OXDK](https://github.com/MrMilenko/OXDK), clang and lld-link. It should
build on Linux the same way, but that is untested. There is no Windows
build path yet; see "Help wanted: Windows tooling" in the README.

Nothing from Microsoft's SDKs is in this repository, and none of it may be
committed. You supply your own copies of the libraries and headers, and
the build reads them from where you keep them.

## What to install

| What | Where it goes | Tested with |
| --- | --- | --- |
| Python 3 | on `PATH` | 3.9 (macOS's) |
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

Two options of `configure.py` change what the console's build holds, and
are for development: `--console-debug-tools` keeps the debug decomp's
developer tools in (the profiler, the AI, render and collision debug, the
stack walker), which the build leaves out by default, and `--console-players
32` (or 64) makes the network tables hold more than the Xbox's 16 players, for
tests a stock console's memory can't hold.

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
the rest) are in the README, "Settings on the console".
xemu (128 MB, NAT) can run the build too: README, "Testing".
