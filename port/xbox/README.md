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
E:\Games\Warthog\default.xbe
E:\Games\Warthog\maps\ui.map
E:\Games\Warthog\maps\loading.tga
E:\Games\Warthog\maps\a10.map ...
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
- `xbox_support.cpp`: `fast_ftol_C` (not yet in the reconstruction), and
  the debug monitor's module functions. A title that imports `xbdm.dll`
  does not start on a retail console, so the game finds no modules.
- `xbox_winsock_hooks.c`: the game's 12-byte `XNADDR` (the 2002 SDK's, and
  the wire's) to and from the 5933 SDK's 36-byte one, and
  `XNetStartupParams` at the 5933 library's 12 bytes (`port/include/xdk`).
  The other SDK structures the game hands the libraries have the same size
  in both; `XNetGetConfigStatus`'s is in no header, so the game's copy has
  room to spare.

`tools/xbox_sources.py` makes the pooled globals and the Bink stub from the
Linux port's files. `port/linux/game/msvc_comdat.c` supplies the header
inline functions that units call through a prototype.

## The game list

At the start, once the network is up, a thread of the game's own gets the
game list from `http://warthog.milenko.org/v1/console/games` (plain HTTP:
the console has no TLS) and the game logs it, to the debug monitor as
`halo: game list:` lines and to `debug.txt` as `game list:` lines. Nothing
waits on it. Multiplayer's ONLINE GAMES shows it (`game/xbox_browser_screen.c`).

- `src/xbox_game_list_fetch.c`: HTTP/1.0 over XNet's Winsock. The host is
  looked up with `XNetDnsLookup`; if it does not resolve,
  `console.game_list_server` names another: a host or an IPv4 address, `:port` optional
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
like play with the PCs, needs `console.bypass_security = true`.

## config.toml

The console's settings are in `D:\config.toml`, beside `default.xbe`: the
desktop builds' file (`port/linux/src/port_config.c`, its TOML parser and
its table of settings), with the settings marked for the console
(`_platform_xbox`) and no environment variables. On the first start
without it the game writes it with every console setting commented out at
its default, each with its help; a setting is chosen by taking out its
`# ` and changing the value. The file is read when the game starts.
`src/xbox_config.c` is the console's side (`xbox_config.h`), and
`tools/xbox_package.py` writes the file with `console.bypass_security`
(and `console.game_list_server` with `--game-list-server`).

The console's own, in `[console]`:

| Setting | Default | What it does |
|---|---|---|
| `bypass_security` | `false` | XNet starts insecure: the internet, the game list and play with PCs need it (`tools/xbox_package.py` writes `true` unless `--secure`); internet play (`network.online`) is off without it |
| `game_list_server` | `""` | the list's server (host or address, `:port` optional) when `warthog.milenko.org` does not resolve |
| `join` | `""` | a host's LAN address (System Link joins it directly) or an invite (ONLINE GAMES' Y joins it) |
| `trace` | `false` | the bring-up traces: the main loop each second, every hop of an internet join, the advertisements seen, other hosts only as `enet#` tags (salted per run), never their Ethernet addresses; and XLink Kai's address for the console |
| `large_caches` | `false` | on a 128 MB console, a 44 MB texture cache (64 MB on a 256 MB one); on a 64 MB console, a message and the Xbox's 22 MB |
| `simulate_64mb` | `false` | a 128 MB console (a devkit) acts as a stock 64 MB one: its memory above 64 MB is taken at the start and kept, and it is in the 64 MB class |
| `log_flush` | `"buffered"` | when `debug.txt` is written: `"buffered"` keeps the game's lines in memory and writes them once a second, when 8 KB wait, and at once for an assert, an exit or a crash; `"every_line"` writes each line as it comes (slow), for a crash hunt |

And the desktop builds' that apply on the console: `display.direct_camera`,
`audio.music_volume`, `audio.effects_volume`, `game.console_log`,
`network.online`, `network.tunnel_port`, `network.brokers_file`,
`network.stun_servers`, `debug.telnet_console`, `debug.telnet_console_port`
and the `debug.network_test` settings (port/linux/README.md has them).
Another setting the shared code reads is empty, 0 or false on the console,
as before it had the file.

The switch files that came before it are still read for this release, each
logged as deprecated in `debug.txt` ("D:\memory_64.txt is deprecated, use
console.simulate_64mb in config.toml"): `bypass_security.txt`,
`memory_64.txt`, `large_caches.txt` and `trace.txt` (an empty file turns
the setting on), and `join.txt` and `game_list.txt` (one line, used when
the setting is empty).

## Memory

`src/xbox_memory.c` reads the console's memory at the start
(`GlobalMemoryStatus`, else the kernel's `MmQueryStatistics`) and logs it
with its class: 64 MB (a stock Xbox), 128 MB (a devkit, or the common
upgrade) or 256 MB (the bigger mods). The XBE does not limit itself to 64 MB
(`cxbe -LIMIT64MB:no`), so a console whose kernel knows the upper 64 MB
reports it.

The class sets only what the console can safely change at run time: the
texture cache (`console.large_caches`, above). The game's limits (players,
machines, objects, the game state) stay the Xbox's on every console, so all
of them play the same game; docs/cross-play.md has why the bigger game
state needs more than a bigger console. `xbox_memory_require` is how a
feature that needs 128 MB says so: a message at the main menu, and the
game goes on without it. `xbox_memory_delta_class` is the hook for Delta
Peer's platform key (`memory_class`, log2 of the megabytes: 6, 7 or 8)
once Delta is built for the console.

The network tables hold 16 players (`HALO_XBOX_PLAYERS`, as the Xbox's): the
players, the machines and the internet tunnel's peers. A host's game with
more machines or players is left as a full game, never overflowed. A test
build with 32 or 64 (`configure.py --xbox-players 32`) is more than a stock
console's memory holds, and says so on screen when the main menu comes up.

A stock 64 MB console has about what the Xbox game had: its caches (game
state, tags, textures, sound: 51 MB) are the Xbox's, so the image and the
heap must stay near the Xbox game's. The debug build the port comes from
kept debug records the console does not (the AI's per-actor debug records
and path copies, 10 MB of heap; the profiler's 256-frame history; the
oversized surface bit vector); the map checks' 2.75 MB of claims are made
on the heap on a console of 128 MB or more only (a 64 MB console loads
maps unchecked, as the Xbox game did). If what is free at the start still
cannot hold the Xbox's 22 MB texture cache and leave 8 MB for Direct3D and
the heap, the cache is made smaller, down to 19 MB (the attract movie
borrows 16 MB of it), rather than the start failing.

Every step of the start is in `debug.txt` before the game's own log opens
(`boot:` lines, the file closed after each): the memory and the class, the
image's size, each cache as it is made, the first Direct3D device, each heap
allocation of 256 KB or more and any that fails, and the free memory at the
main loop; later, `memory: N KB free (a new low)` as a map loads or a match
peaks. `console.simulate_64mb` (above) runs that path on a devkit.

The debug decomp's developer tools are not in the console's build unless
it is configured with `--xbox-debug-tools` (`HALO_XBOX_DEBUG_TOOLS`): the
profiler, the AI debug displays, the render and collision debug, the stack
walker, the texture cache listing, libtiff and the bitmap tools. Their code
is kept, not removed, and what the game calls in them is a stub in the
console's build; a console command or script that reaches one prints
"not in this build". About 0.9 MB of the image.

## XLink Kai

At the start `debug.txt` says whether the console's address is XLink
Kai's for its MAC: yes or no. Kai's address itself (`10.252.EE.FF`, mask
`255.255.0.0`, from the MAC's last two bytes) is logged only with
`console.trace`. The "XLink mode" that would set it is a design
(docs/xlink.md).

## Status

The build booted on a development kit on October 4: the menus at a steady
30 fps, profiles, DHCP, the game list and ONLINE GAMES. Music played from
October 13 (until then the console had no `audio.music_volume` or
`audio.effects_volume` setting, so both read as 0; they are now 1.0): the menu music and the campaign's music and effects play on a
development kit. The same day it joined a [D] dedicated server through
ONLINE GAMES.
The October 2 build also started on a modified retail console. The code is
now ChupathingyCE's at network version 28 (it joins hosts of 11 through
30), merged October 13 and not yet booted on a console since.
Network co-op is not built for the console (`game/xbox_coop.c`): co-op
games are left out of its lists, and a game that turns co-op is left with
a message. Known:

- There is no Bink video (the movies are skipped) and no reverb.
- The netcode is the PC builds' (distributed). The console joins a PC
  host's game as a client: it folds the PCs' 128-slot game settings record
  and Slayer's state into its 16 slots, and leaves a game that grows past
  them as a full one (docs/cross-play.md, "What is built"). It joins an
  internet game by its invite from ONLINE GAMES (A on a listed game, Y for
  the invite in `console.join`), through the desktop builds' tunnel
  (`port/linux/src/p2p*.c`, built as they are; `src/xbox_p2p.c` and
  `src/xbox_winsock_hooks.c` are its platform), with
  `console.bypass_security`; that join is built but not yet seen working on
  a console. On a LAN it finds a PC host in System Link, or joins the host
  whose IPv4 address is in `console.join` (`src/xbox_direct_join.c`). It
  does not yet host the PCs, and its object array (2,048) is the Xbox's:
  `debug.txt` logs how high a PC host's object indices go ("cross-play:"
  lines).
- The menus are the Xbox's; the PC menus are not built.
- Campaign and multiplayer levels are not yet tested.
