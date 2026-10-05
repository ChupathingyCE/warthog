<h1 align="center">Warthog</h1>

<p align="center"><b>ChupathingyCE's Halo: Combat Evolved for the original Xbox, and later the Xbox 360.</b></p>

Warthog is the Halo CE decompilation built back for the console it was
written for, on the same code as [ChupathingyCE](https://github.com/ChupathingyCE/chupathingyce)
(the desktop and Android builds). The goal: boot on a real Xbox and play
online with OpenCE and ChupathingyCE players, 16 players or fewer, on the
traditional maps.

Warthog stays Halo as it shipped on the Xbox: its memory, caches and limits
are the console's. Only the platform code differs from ChupathingyCE.
Game logic and netcode are ChupathingyCE's (the distributed netcode the PC
builds play), with the console's own code back in place under
`HALO_XBOX_CONSOLE` where upstream's cleanup (4adc3a87) removed it:
the rasterizer's fixed 640x480 targets, frame timing, input, the file
cache, physical memory, sound and effect pools, and the socket transport.

## Status

| | |
|---|---|
| Original Xbox build (`ninja xbox`) | builds on macOS with OXDK, `build/xbox/default.xbe` |
| Boots | the menus at a steady 30 fps on a devkit (October 4); XNet gets its address by DHCP and the game list comes over the internet |
| Code | ChupathingyCE main of October 4, network version 11 |
| System link with PC builds | the console joins a PC host's game (a dedicated server too) and plays it, tested on the host with the console's limits; not yet on the console: see "Cross-play" |
| Online Games (the game list) | Multiplayer, ONLINE GAMES: the list on a screen of its own, read only; no joining yet: see "Online Games on the Xbox" |
| Xbox 360 | planned: see "Xbox 360" |

## Prerequisites

Nothing from Microsoft's SDKs is in this repository, and none of it may be
committed. You supply it, as with [OXDK](https://github.com/MrMilenko/OXDK):

- Python 3, [ninja](https://ninja-build.org/) and LLVM 22 or later (clang
  and lld-link). On macOS: `brew install llvm ninja`. The build looks in
  `LLVM_DIR`, then in Homebrew's folder.
- [OXDK](https://github.com/MrMilenko/OXDK) in `~/OXDK` (or `--oxdk DIR`,
  or `OXDK_DIR`), with its `cxbe` built.
- The Xbox SDK's libraries and headers, in OXDK's `xbox/xdk/lib` and
  `xbox/xdk/include`, set up as OXDK's instructions say.
- An Aug 2001 Direct3D library, `d3d8ntpr.lib` (`port/xbox/README.md`,
  "Direct3D").
- The North American (NTSC, 01.10.12.2276) disc's maps, which every
  ChupathingyCE build and dedicated server plays.

## Build

```sh
python3 configure.py --xbox-d3d8 /path/to/d3d8ntpr.lib
ninja xbox
python3 tools/xbox_package.py --maps /path/to/maps --out ~/Downloads/Warthog-xbox
```

`xbox_package.py` lays out `default.xbe` and `maps/` as one folder for the
console's hard disk (hard links where it can, so the maps take no more
space), with an empty `bypass_security.txt` (XNet's insecure mode, which
the internet and the PCs need; `--secure` leaves it out) and, with
`--game-list-server HOST`, a `game_list.txt` for a console whose DNS can't
find the list's host. The Linux, Windows and macOS targets in `configure.py` are
ChupathingyCE's; Warthog only builds `xbox`.

## Testing

**Devkit or modified retail console.** Copy the folder to the hard disk,
e.g. `E:\Games\Warthog\` (FTP, or the devkit's tools), and launch
`default.xbe`. The game logs `halo:` lines to the debug monitor (XBDM)
and writes `D:\debug.txt`. It is a debug build: an assertion stops it and
shows the error on screen and in `debug.txt`. A bring-up watchdog prints
the main loop's progress once a second.

**xemu.** vsod99's fork of OpenCE (`vsod99/halo-ce-universal`, branch
`xbox-backport`, CC0) has a development loop, `tools/xbox_dev.py`: it packs
an ISO, boots it in xemu at 128 MB with NAT and streams the serial log.
It builds with nxdk, but its ISO and xemu steps work for any XBE. xemu
needs the user's own MCPX boot ROM, flash image and hard disk image.

**Xbox 360.** Xenia first (unsigned XEX files run there), then an RGH or
JTAG console through a launcher such as Aurora.

## Cross-play

The goal is system link and internet games with the PC builds at network
version 11. Two things stand in the way today, and both are capacities the
network carries, not code:

- **Session size.** The console builds 16 players on 16 machines
  (`halo_port_limits.h`); the PCs build 128 and 128. The game settings
  record (`struct network_game`) is sent whole, and its size follows from
  those limits (13,120 bytes at 128), so the two cannot read each other's.
  The console needs the PCs' layout, with the 16 player limit kept as the
  game's own maximum.
- **Object indices.** The distributed netcode names objects by their index
  in the object array, and a client puts its own objects in the array's
  upper half. The console's array has 2,048 entries, the PCs' 8,192.

Both need a bigger game state, and the Xbox's is full: its pools fill all
but 948 bytes of the 0x305000 bytes at 0x80061000, and maps are linked to
the tag cache right after it. vsod99's memory probe found the route: the
game state as ordinary virtual memory (at 0x40000000) on a 128 MB console,
with the tag cache and texture and sound caches kept contiguous in the low
64 MB. Only the index space has to match the PCs; the pools behind it can
stay the console's.

The field-by-field analysis, the choice (the PCs' 128 player and machine
slots on the console; object slots translated at the console's netcode)
and what is built so far are in [docs/cross-play.md](docs/cross-play.md).

What works first, without a bigger game state: the console as a client of
a PC host. It folds the PCs' 128-slot game settings record and Slayer's
state into its own 16 slots as they arrive, and leaves a game that grows
past them as it leaves a full one. A copy of a desktop build with the
console's limits (`configure.py --console-limits`) plays whole games
against a PC dedicated server on one computer that way. On the console it
finds a PC host's game in System Link, or joins the host in
`D:\join.txt` directly (one line, the host's IPv4 address).

The console's network stack also speaks the Xbox's secure system link
unless XNet starts with `XNET_STARTUP_BYPASS_SECURITY`. The game has its
own switch for that (`D:\bypass_security.txt`). The PCs speak plain UDP,
so cross-play needs it on.

## Online Games on the Xbox

ChupathingyCE's Online Games list (`port/linux/src/browser.c`) and internet
play (`p2p.c`, `p2p_signal.c`, `p2p_lobby.c`, KCP, STUN) are the desktop
platform layer's, on POSIX sockets, threads and Mbed TLS. The console has
none of that layer; its game talks to XNet directly. The pieces:

1. **The list (first milestone, done).** The console has no TLS, so the
   site serves it a list of its own over plain HTTP, read only:
   `http://warthog.milenko.org/v1/console/games`, ASCII, a tab-separated
   line a game. `port/xbox/src/xbox_game_list*.c`: an HTTP/1.0 client on
   XNet's Winsock (`XNetDnsLookup`, or `D:\game_list.txt`), on a thread of
   its own, and bounded parsers for the response and the list, fuzzed on
   the host (`port/xbox/tests/run.sh`). At the start the game fetches the
   list and logs it. Announcing and reports, which carry the player key,
   stay on the desktop builds' TLS.
2. **The screen (done, read only).** The Multiplayer menu's ONLINE GAMES
   item is the desktop builds' (`interface/ui_widget.c`: a copy of System
   Link's item, under `HALO_XBOX_CONSOLE` as well as `HALO_GAME_BROWSER`).
   Their screen (`port/linux/game/browser_screen.c`) draws through their
   platform layer (SDL, its overlay and fonts) and joins through the p2p
   tunnel, so the console has its own, `port/xbox/game/xbox_browser_screen.c`:
   drawn like the virtual keyboard, in the menus' own fonts and button
   icons, over the menus. Each game's name, map (the menus' names), type,
   players and region; the selected game's details and map picture below.
   D-pad or stick to pick (left and right turn the page), X refreshes, B
   goes back; A says "Joining from the Xbox is coming." The list is fetched
   on its own thread and the main loop takes a whole copy (about 14 KB) for
   the screen; the 64 KB response buffer lives only while a fetch runs.
3. **Joining.** Internet games are reached through the PCs' p2p tunnel
   (STUN, signaling, KCP), which hands the game stand-in addresses. On the
   console that layer would sit under `transport_endpoint_winsock.c`, which
   is where the game's sockets are. This waits on "Cross-play", since
   until then the console cannot read a v11 host's settings.
4. **Invites.** No clipboard: the list itself is the invite (choose a game
   and join), plus Link Profile (the
   desktop builds' QR code, which links the game to a profile from another
   device).

## Xbox 360

Not started in this pass. The plan, from Milenko's 360 ports (QSS-M,
doomretro-x360) and OXDK360:

- **Toolchain:** OXDK360's patched clang (`~/llvm-xenon`) and `cxex`, with
  the user's own Xbox 360 SDK, not committed, the same line as OXDK.
- **Endianness first.** Maps, saved games and network messages are
  little-endian Xbox data. Swap at load (tag data swapped by its field
  definitions as each tag loads), and swap network fields in the message codec, which already
  encodes field by field. Not a big-endian aware loader everywhere.
- **Alignment.** PowerPC traps on misaligned multi-byte reads from byte
  buffers; audit the cache file and network readers for `*(long *)` reads.
- **Memory:** 512 MB, so the PCs' capacities fit and cross-play needs no
  special layout.
- **Renderer:** Xenos through the SDK's Direct3D 9, starting from the
  desktop builds' renderer (`d3d8_gl.c` maps the game's Direct3D 8 calls),
  not the original Xbox's push buffer code.
- **First milestone:** a XEX that logs a line in Xenia.

## Keeping in step with ChupathingyCE

Warthog takes ChupathingyCE main by pull request: each merge on main's
first parent is replayed here as one commit (`git cherry-pick -m 1`),
which keeps main's own conflict resolutions, and names the pull request,
its commits and their authors (`Co-authored-by` trailers for contributors).
Only pull requests that change nothing but the README are left out. Then
`ninja xbox` must build. Making `chupathingyce` a submodule, with only
`port/xbox` and the console's gates here, is the later plan once the gates
live in ChupathingyCE's own shared code.

## Licensing

The code is the decompilation's and ChupathingyCE's (CC0, with the
third-party notices in `port/third_party`). Never commit Microsoft SDK
files, game files or maps.
