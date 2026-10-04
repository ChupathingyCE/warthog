# Original Xbox

`ninja xbox` builds the game for the console that it was written for:
`build/xbox/default.xbe`. The build uses clang and lld-link as
[OXDK](https://github.com/MrMilenko/OXDK) does, on macOS or Linux. It is the
game only: the platform layer of the other ports (`port/linux`) is not in it,
because the game's Xbox code talks to the Xbox SDK directly.

## Requirements

- Python, [ninja](https://ninja-build.org/) and LLVM (clang and lld-link).
  On macOS: `brew install llvm ninja`. The build looks for LLVM in `LLVM_DIR`
  and then in Homebrew's folder.
- [OXDK](https://github.com/MrMilenko/OXDK) in `~/OXDK` (or give
  `--oxdk DIR`, or set `OXDK_DIR`), with its `cxbe` built.
- The Xbox SDK's libraries and headers in OXDK's `xbox/xdk/lib` and
  `xbox/xdk/include`, as the OXDK instructions tell. This repository does not
  supply them. The SDK comes as a Windows installer that installs into
  Microsoft Visual Studio .NET; you must install it (and a Visual Studio that
  it accepts) on a Windows computer or virtual machine, and then copy the
  files. The build was made with the 5933 SDK.
- An Aug 2001 build of the Direct3D library, `d3d8ntpr.lib`. This repository
  does not supply it. Refer to "Direct3D".
- The North American (NTSC) disc's maps. Refer to "Game data".

## Build

```sh
python3 configure.py --xbox-d3d8 /path/to/d3d8ntpr.lib
ninja xbox
```

`configure.py` records the options, so later runs of ninja keep them. If
OXDK or the Direct3D library is not found, there is no `xbox` target.

## Game data

The game is build 01.01.14.2342, but it plays the maps of the North American
(NTSC) release, 01.10.12.2276, as the other ports and the dedicated servers
do: playing online with them needs the same maps. The maps of the PAL
release (01.01.14.2342) work too, played as the NTSC maps are
(`port/linux/game/pal_tags.c`). Use `maps/*.map` and `maps/loading.tga`
from the disc.

`python3 tools/xbox_package.py --maps DIR --out DIR` lays the build out.
Put the files on the console's hard disk in one folder, for example:

```
E:\Games\HaloBeta\default.xbe
E:\Games\HaloBeta\maps\ui.map
E:\Games\HaloBeta\maps\loading.tga
E:\Games\HaloBeta\maps\a10.map ...
```

`D:\` is the folder of the XBE when the game runs, and the game writes its
log to `D:\debug.txt`. Saved games and profiles go to the utility drive
(`Z:\`), as on the disc. The game is a debug build: an assertion stops it
and shows the error on the screen and in `debug.txt`.

## Direct3D

The game compiles against the January 2002 SDK's declarations
(`port/include/xdk`), as the other ports do, and links with a later SDK's
libraries. Direct3D is the exception. Later SDKs renumbered the render
states and texture stage states that the game sets, so the game needs a
Direct3D of its own time. The reconstructed `libs/d3d8` needs the January
SDK's headers, which are not available. The build therefore links
`d3d8ntpr.lib`, the Aug 2001 Direct3D that the kernel's boot animation
used. Its state numbers are the January SDK's, and its device has the
layout that `source/main/d3d_intimacy.cpp` reads. `port/xbox/src/
xbox_support.cpp` supplies the memory functions that the kernel gave it.

## Differences from the SDK

`port/xbox/include/halo_xbox_prefix.h` is included before each game unit.
The files in `port/xbox/src` supply what the later SDK does not:

- `xbox_dsound.c`: the January SDK gave a voice's mix bins as a mask. Later
  SDKs take a list. The file translates the game's calls. The game's reverb
  image is for the January DirectSound, so it is not loaded: there is no
  reverb.
- `xbox_d3dx.c`: the four D3DX functions that the game calls. The SDK's
  D3DX library needs the SDK's own Direct3D.
- `xbox_support.cpp`: `fast_ftol_C` (not yet in the reconstruction), and the debug monitor's module functions. A title that
  imports `xbdm.dll` does not start on a retail console, so the game finds
  no modules.

`tools/xbox_sources.py` makes the pooled globals and the Bink stub from the
Linux port's files. `port/linux/game/msvc_comdat.c` supplies the header
inline functions that units call through a prototype.

## The game list

At the start, once the network is up, a thread of the game's own gets the
game list from `http://warthog.milenko.org/v1/console/games` (plain HTTP:
the console has no TLS) and the game logs it, to the debug monitor as
`halo: game list:` lines and to `debug.txt` as `game list:` lines. Nothing
waits on it. The menus do not show it yet.

- `src/xbox_game_list_fetch.c`: HTTP/1.0 over XNet's Winsock. The host is
  looked up with `XNetDnsLookup`; if it does not resolve, `D:\game_list.txt`
  names another, one line: a host or an IPv4 address, `:port` optional
  (the request still says `Host: warthog.milenko.org` for an address).
  The response is read to the server's close or its Content-Length, at
  most 64 KB, each wait bounded (15 seconds in all); three tries.
- `src/xbox_game_list_parse.c`: the response (status line, headers, body)
  and the list, with a bound on every read: lines of 1,024 bytes (headers)
  and 255 (the list), 64 headers, 64 games, each field its size; anything
  cut short is an error. The format is the site's list for consoles (format 1).
- `src/xbox_game_list.c`: the thread and the logging. No address is logged.
- `tests/run.sh`: the parsers on the host under AddressSanitizer and
  UndefinedBehaviorSanitizer (known inputs, every truncation, a mutation
  fuzzer), `--fuzz SECONDS` with libFuzzer, `--fetch` the real list over
  the host's sockets.

XNet speaks only to other consoles unless it starts insecure, so the list,
like play with the PCs, needs an empty `D:\bypass_security.txt`.

## Status

The October 2 build started on a modified retail console and on a
development kit: the menus, the menu music and profiles. This code is
ChupathingyCE main's of October 4 (network version 11) and links; it is not
yet booted. Known:

- There is no Bink video (the movies are skipped) and no reverb.
- The netcode is the PC builds' (distributed), but the console cannot yet
  play with them: its session (16 machines) and object array (2,048) give a
  different game settings record and object indices than theirs (128
  machines, 8,192 objects). The top level README's "Cross-play" has the plan.
- The menus are the Xbox's; the PC menus and Online Games are not built.
  The game list is fetched and logged only ("The game list").
- Campaign and multiplayer levels are not yet tested.
