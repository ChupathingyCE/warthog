# Xbox 360

`ninja xbox360` builds the game for the Xbox 360: `build/xbox360/default.xex`. It is the Windows build's arrangement (port/windows/README.md) on the console's PowerPC: the game's sources and the desktop platform layer (`port/linux/src`), with the console's own units in `src`. It starts on a development kit and logs its start-up, then stops early in the game's own start (being worked on). Nothing is drawn yet and there is no sound. docs/consoles.md has the status of every console.

How it is put together:

- The game and `port/linux/src` see the original Xbox's SDK declarations (`port/include/xdk`), as on every other port. `src/x360_*.c` see the console's own SDK. Functions both define are renamed for the game's side (`include/halo_xbox360_api_names.h`).
- Pointers are 32 bits, but the Xbox's addresses are the console's executable's, so the game runs with the 64-bit builds' pointer mode over a relocatable window (`HALO_64BIT` and `HALO_XBOX_WINDOW`, `source/cseries/xbox_address.h`): Xbox addresses 0x78000000 to 0x88000000 (the heap, then 128 MB of contiguous memory) are one 256 MB block wherever the console has it, and an Xbox address is one add away from its pointer (`src/x360_memory.c`).
- `src/x360_posix.c`, `x360_files.c`, `x360_net.c` and `x360_system.c` give the platform layer the POSIX, file, socket and SDL calls it makes, on the console's kernel, XNet and C runtime. Paths are made the console's: backslashes, below `game:\`.
- Thread-local variables are emulated (`-femulated-tls`, `src/x360_tls.c`): the console's TLS is Microsoft's compiler's.
- `src/xbox360_platform.c` and `src/xbox360_input.c` stand in for `sdl_platform.c` and `xinput_sdl.c`, as the dedicated server's units do; the controllers are the console's (`src/x360_input_pad.c`).

## Requirements

- [OXDK360](https://github.com/MrMilenko/OXDK360) in `~/OXDK360` (`--oxdk360 DIR`), with `tools/cxex` built.
- Its patched clang (the Xenon calling convention) in `~/llvm-xenon` (`--llvm-xenon DIR`).
- Your own extracted Xbox 360 SDK, the folder holding `lib/xbox/xboxkrnl.lib` (`--xdk360 DIR` or `XDK_DIR`). Without it there is no target, and `ninja xbox360` says what is missing. Nothing from the SDK is in this repository, and none of it may be committed.

## Build

```sh
python3 configure.py --xdk360 ~/xdk360-extract/sdk/XDK
ninja xbox360
```

Every build stamps its first log line with the version, commit and build time, so a test can tell which build ran (a failed upload leaves the old XEX in place).

## Run

Put `default.xex` in a folder of its own on the console (`Hdd1:\Games\Warthog360\`), with the Xbox disc's `maps` folder beside it (the NTSC maps, 01.10.12.2276: at least `ui.map`). The log is on the debug channel (XBDM) and in `game:\port.txt` (the console's layer) and `game:\debug.txt` (the game's). Settings are `game:\config.toml`, written at the first start.

## Notes for the port

From the owner's earlier OXDK360 ports, and what phase 0 is built around:

- Paths: backslashes only, and device names longer than a letter (`game:`). A forward slash fails silently on writes, and `path[1] == ':'` checks miss the device.
- `game:` is writable when launched from Aurora; `Hdd:` and `Hdd1:` may not be mounted.
- Exit with `XLaunchNewImage("", 0)`. Tearing Direct3D or sound down mid-present can hang the console.
- OXDK360's clang returns 64-bit values in a register pair where the SDK uses one register: an SDK call returning a 64-bit value (file positions, `_time64`) comes back wrong. Build with `-D_USE_32BIT_TIME_T=1`.
- Variadic doubles format as garbage: `%f` through the SDK's `printf` family is broken. The game's debug and HUD text use it widely.
- `_byteswap_ushort`, `_ulong` and `_uint64` are declared by the SDK and defined nowhere: the port provides them as functions (not macros).
- No C++ exceptions, no VMX128 (XNA math is the scalar fallback), no `vsnprintf` (`_vsnprintf`).
- Direct3D 9 on Xenos has no fixed function and no `UpdateTexture`; textures are tiled (linear uploads use the `D3DFMT_LIN_*` formats). Shaders are compiled offline with the SDK's `fxc` under Wine (`/Zpr`, `row_major`).
- Big-endian: every binary loader byte-swaps after its reads, and unaligned `*(int *)` reads fault; use memcpy readers. Pointer and integer type mismatches are real bugs here: don't silence them.

## Compile census

The game units the original Xbox build compiles (478: `source/`, `port/linux/game`'s shared netcode, `port/xbox/game`, `d3d_intimacy.cpp`), compiled for Xenon with the patched clang, no link, against the Xbox build's own declarations (`port/include/xdk` and the original Xbox SDK's C runtime headers, `_WIN32` defined):

| Configuration | Compiles |
|---|---|
| as the Xbox build (`HALO_XBOX_CONSOLE`) | 102 of 478: `fast_ftol`'s x86 `__asm` in `cseries.h` stops 366 |
| the same, `fast_ftol`'s asm gated on `_M_IX86` | 461 |
| the native builds' code paths (no `HALO_XBOX_CONSOLE`) | **465** |

What fails without `HALO_XBOX_CONSOLE`: 10 libtiff units (K&R prototypes for an unknown platform, as on the Wii U; tools only), `real_math.c` (x87 `CW_DEFAULT`), `shell_xbox.c` (SEH `__try`), `transport_endpoint_set_winsock.c` (the prefix must define `FD_SETSIZE` first). With `HALO_XBOX_CONSOLE`, add x86 `__asm` in `profile.c`, `stack_walk_windows.c`, `decals.c`, `hud_draw.c` (`naked`) and `bink_playback.c`.

Compiling is the easy part: byte order and alignment show up at run time, not here. Warnings worth a look on this target: 165 implicit declarations (`snprintf`, `vsnprintf`, `debug_malloc`), and a handful of pointer mismatches, one a real big-endian bug (`ui_widget_event_handler_functions.c` passes a `word *` as `long *`).
