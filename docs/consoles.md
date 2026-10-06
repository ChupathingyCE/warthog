# Other consoles

Warthog is ChupathingyCE's console line. The original Xbox is the one that
plays: it boots to the menus on a devkit and on modified retail consoles,
gets the game list and joins a PC host's game as a client (README, "Status").
Everything else here is scaffolding: a folder, a build generator and a
packaging script per console, each building a stub that logs a line and
exits. None of the game is compiled for these consoles yet, and none of
them has run anything on hardware.

| Console | Folder | `ninja` target | Toolchain | Status |
|---|---|---|---|---|
| Original Xbox | `port/xbox` | `xbox` | clang, lld-link, [OXDK](https://github.com/MrMilenko/OXDK), the user's 5933 XDK | plays: menus, game list, joins PC hosts |
| Xbox 360 | `port/xbox360` | `xbox360` | [OXDK360](https://github.com/MrMilenko/OXDK360), llvm-xenon, the user's 360 SDK (`XDK_DIR`) | planned; stub, not built here yet |
| Wii U | `port/wiiu` | `wiiu` | devkitPPC, wut | planned; stub builds, game not built |
| Switch | `port/switch` | `switch` | devkitA64, libnx | thelinkin3000's port, being brought in; the stub here until it is |

A target exists only when `configure.py` finds its toolchain (the Xbox 360's
needs the user's SDK as well), so a desktop checkout configures as before.

## What each console needs before it plays

The plan's order: the original Xbox first; the Xbox 360 next, with
the Wii U alongside (both big-endian, so the byte-order work is shared);
the Switch from thelinkin3000's port, being brought in. For any of them:

1. **The game's sources built for it.** Today only the original Xbox compiles
   `source/`. The others need a platform layer for what `port/linux` gives
   the desktop builds: files, threads, timing, input, sound and a renderer.
   `port/linux/src/d3d8_gl.c` (the game's Direct3D 8 calls over OpenGL) is the
   likely start where there is OpenGL or GLES; the Xbox 360 would go through
   its own Direct3D 9.
2. **Byte order (Xbox 360, Wii U).** Maps, saved games and network messages
   are little-endian Xbox data. The plan in the README ("Xbox 360"): swap tag
   fields by their definitions as each tag loads, and network fields in the
   message codec, which already encodes field by field. Not a big-endian
   aware loader everywhere. `port/console/include/console_endian.h` has the
   swaps and the unaligned readers for that work; nothing uses it yet.
3. **Alignment (PowerPC).** Misaligned multi-byte reads from byte buffers
   trap or are slow: the cache file and network readers' `*(long *)` reads
   need an audit.
4. **Memory and limits.** The original Xbox keeps the Xbox's limits (16
   players and machines, 2,048 objects, a 22 MB texture cache; README,
   "Memory"). Consoles with more memory could take the desktop builds'
   capacities, which makes cross-play simpler, but that is a choice to make
   per console once it runs, not a given: each one's Delta platform key
   (`port/linux/include/delta.h`, once Delta Peer is in ChupathingyCE) says what it
   hosts and joins.
5. **Cross-play.** The network protocol stays the PC builds' (network
   version and layout); a console adapts to it, never the other way
   (docs/cross-play.md).

## Shared console code (`port/console`)

Headers only, not yet included by any build that runs the game:

- `console_common.h`: the console's name and type, a controller state.
- `console_endian.h`: byte swaps, little-endian readers and writers that
  are safe at any alignment.
- `console_input.h`: the controller interface each stub implements (all
  stubs report no controller).

## Packages

A console's package (the Wii U's `.wuhb`) holds the stub only, never the
maps: a package is one installable file, easily passed around, and the maps
are the user's own. They stay a folder beside it.

## Testing (once a console runs the game)

| Console | Emulator | Hardware |
|---|---|---|
| Original Xbox | xemu (128 MB, NAT) | devkit, or a modified retail console |
| Xbox 360 | Xenia | RGH or JTAG console, through a launcher such as Aurora |
| Wii U | Cemu | Aroma |
| Switch | Ryujinx | Atmosphere's Homebrew Menu |
