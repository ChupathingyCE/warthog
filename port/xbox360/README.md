# Xbox 360

Phase 0: `ninja xbox360` builds a boot test (`build/xbox360/default.xex`), not the game. It writes a log of the console's memory and platform facts that the port's plan depends on, then returns to the launcher. None of the game is compiled for this console yet. docs/consoles.md has the status of every console, and docs/wiiu.md the big-endian work the Wii U shares with this one.

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

## Install and run

On an RGH or JTAG console with Aurora:

1. FTP `build/xbox360/default.xex` to a folder of its own, such as `Hdd1:\Games\Warthog360\`.
2. In Aurora, add that folder as a scan path (or rescan), then launch "default.xex".
3. The screen turns gray while the test runs, then green. Press any button, or wait 10 seconds, and it returns to Aurora.
4. FTP `debug.txt` back from the same folder.

## The log

`game:\debug.txt`, beside the XEX (also on the debug channel, for XBDM). It reports:

- version, commit, build time;
- system version, video mode, AV pack;
- physical and virtual memory, total and free; the largest `malloc` and `XPhysicalAlloc` that succeed (to 1 MB); whether a 128 MB contiguous block (the Xbox's memory, which the game's addresses are laid out for) can be had with 4 KB, 64 KB and 16 MB pages, and where;
- which storage paths exist (`game:`, `Hdd1:`, `Hdd:`, `Usb0:`);
- controllers in each port;
- network: link state, whether the console has an address and of which kind (private, link-local, public). Never the address.

## Phase 0 checklist

Send back:

1. `debug.txt`, whole.
2. Whether the screen turned gray and then green.
3. Whether a button press returned to Aurora, and whether the timeout did.
4. The console (model, RGH or JTAG, dashboard version) and how it was connected (wired or wireless).

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
