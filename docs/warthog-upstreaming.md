# Moving Warthog's switches into ChupathingyCE (a list, not done yet)

Changes flow one way: ChupathingyCE to Warthog. Each rebase of Warthog
onto ChupathingyCE replays every console switch Warthog keeps in shared
files, and each one is a chance to conflict. If ChupathingyCE carried them
itself (compiled out of every desktop build, as `HALO_XBOX_CONSOLE` is
never defined there), Warthog would shrink to `port/xbox` (and later a
submodule) and a rebase would cost almost nothing.

This is the list as of the rebase onto ChupathingyCE with OpenCE
build-138 (network version 20): Warthog's own changes to shared files,
60 shared files, about 1,900 added lines. "Switch" is the define the
change hides behind; "size" is the diff's lines; "risk" is the risk to the
desktop builds if ChupathingyCE took it.

## Wave 1: no risk to the desktop (the console's own code, compiled out)

These are Bungie's Xbox branches that OpenCE's cleanup (4adc3a87) removed,
put back as `#ifdef HALO_XBOX_CONSOLE` with the desktop's side
`#ifndef`. With the define unset each file reads as ChupathingyCE's does.
Taking them is mechanical; a desktop build of every target before and after
(and `tools/port_neutrality_check.py` for the 32-bit builds) proves it.

| File | Switch | Size | What |
|---|---|---|---|
| `source/rasterizer/xbox/rasterizer_xbox.c` | `HALO_XBOX_CONSOLE` | 34 | fixed 640x480 targets, the frame rate throttle |
| `source/rasterizer/xbox/rasterizer_xbox_environment_fog.c` | `HALO_XBOX_CONSOLE` | 27 | the Xbox's fog path |
| `source/rasterizer/xbox/rasterizer_xbox_screen_effect.c` | `HALO_XBOX_CONSOLE` | 14 | screen effect bounds |
| `source/rasterizer/rasterizer.h`, `rasterizer_lights.c` | `HALO_XBOX_CONSOLE` | 17 | |
| `source/render/render.c`, `render.h`, `render_objects.c`, `render_particles.c` | `HALO_XBOX_CONSOLE` | 49 | the post-rasterize step, bounds |
| `source/effects/*.c` (contrails, decals, effects, particles, particle systems, weather, player effects) | `HALO_XBOX_CONSOLE` | 99 | the Xbox's pools |
| `source/interface/hud*.c`, `first_person_weapons.c`, `motion_sensor.c`, `terminal.c` | `HALO_XBOX_CONSOLE` | 74 | the HUD at 640x480, without the port's high-res text |
| `source/input/input_xbox.c` | `HALO_XBOX_CONSOLE` | 24 | the controllers through XInput |
| `source/sound/game_sound.c`, `sound_manager.c` | `HALO_XBOX_CONSOLE` | 14 | |
| `source/cseries/errors.c`, `profile.c`, `stack_walk_windows.c`, `cseries.h` | `HALO_XBOX_CONSOLE` | 69 | `D:\debug.txt`, profiling, the stack walk |
| `source/saved games/game_state.c`, `game_state_xbox.c` | `HALO_XBOX_CONSOLE` | 54 | the game state at 0x80061000 |
| `source/cache/cache_files_windows.c`, `physical_memory_map.c` | `HALO_XBOX_CONSOLE` | 27 | the file cache's reads, physical memory, the runtime texture cache size |
| `source/cache/xbox_texture_cache.c` | `HALO_XBOX_CONSOLE` | 36 | the texture cache sized at run time (Warthog's memory class) |
| `source/bink/bink_playback.c`, `structures/cluster_partitions.c`, `physics/collision_debug.c` | `HALO_XBOX_CONSOLE` | 18 | |
| `source/bungie_net/network/transport_endpoint_winsock.c` | `HALO_XBOX_CONSOLE` | 12 | the Xbox's socket buffers (16 KB, 32 pending connections) |
| `port/include/xdk/xdk_pdb.h`, `xdk_winsock.h` | `HALO_XBOX_CONSOLE` | 14 | `XNetStartupParams` at the 5933 XDK's 12 bytes; the XNADDR status bits |
| `port/linux/include/halo_port_capacity.h` | `HALO_XBOX_CONSOLE` | 165 | the console's block of Xbox sizes (game state, AI, objects, effects) and the texture cache's largest size |

Risk: none to the desktop builds; the main cost is review time. The one
judgment call is `halo_port_capacity.h`, which grows a console block; it
already has the `HALO_XBOX_CONSOLE` tests for the texture cache and the
structure limits, so the block belongs beside them.

## Wave 2: shared features the console uses, already shaped for the desktop

| File | Switch | Size | What | Risk |
|---|---|---|---|---|
| `port/linux/game/network_game_layout.c`, `.h` | none (a plain C module) | 305 | the settings record and game type states between 128 and 16 slots | none: not linked into a desktop build unless asked |
| `port/linux/include/halo_port_limits.h` | `HALO_XBOX_CONSOLE`, `HALO_CONSOLE_LIMITS` | 22 | the console's 16 and 16, the wire's 128 and 128 | low: the `#else` is today's |
| `configure.py`, `tools/linux_build.py` | `--console-limits` | 16 | a desktop build with the console's limits, to test cross-play on one computer | low: an option off by default; ChupathingyCE's CI could build it to catch layout changes before Warthog does |
| `source/game/game_engine.c`, `game_engine_slayer.c` | `HALO_PORT_CONSOLE_LIMITS` | 75 | Slayer's state folded from 128 slots | low with `--console-limits` built in CI |
| `port/linux/game/network_objects.c` | `HALO_PORT_CONSOLE_LIMITS` | 56 | logs how high a PC host's object indices go | none |
| `source/networking/network_client_message_handler.c` | `HALO_PORT_CONSOLE_LIMITS`, `HALO_XBOX_CONSOLE` | 184 | the settings fold, the record's layout asserts, leaving co-op games, the tunnel's advertisements and keys | medium: the busiest file in every OpenCE merge; worth it most, because it conflicts most |
| `source/networking/network_client_manager.c` | `HALO_PORT_CONSOLE_LIMITS`, `HALO_XBOX_CONSOLE` | 117 | leaving games past 16 machines, `D:\join.txt`'s direct join, ONLINE GAMES' invite join | medium, as above |
| `source/networking/network_server_manager.c` | `HALO_XBOX_CONSOLE` | 5 | the console never sets up co-op | none |
| `source/interface/ui_widget.c` | `HALO_XBOX_CONSOLE` | 84 | the mouse, touch and debug targets left out; ONLINE GAMES for the console | medium: 19 switches in a file the PC menus change often |
| `source/interface/ui_widget_event_handler_functions.c`, `ui_widget_game_data_input_functions.c`, `source/cache/cache_files.c` | `HALO_GAME_BROWSER \|\| HALO_XBOX_CONSOLE` | 6 | ONLINE GAMES' item and tags | none |
| `source/main/main.c` | `HALO_XBOX_CONSOLE` | 161 | the console's main loop calls: watchdog, game list, log flush, memory message; bring-up stage markers | low; a single `xbox_main_loop_hook()` call in place of the inline blocks would shrink it to a few lines |
| `source/bungie_net/network/transport_endpoint_set_winsock.c` | `HALO_XBOX_CONSOLE` | 109 | XNet's insecure start, the DHCP wait, the title address and XLink lines | low: XNet only exists on the console |
| `port/linux/src/p2p.c` | `P2P_JOINER_ONLY`, `P2P_TRACE_DATAGRAMS` | 36 | the tunnel sized for a joiner; datagram traces | low; drop `P2P_TRACE_DATAGRAMS` once the console's join is proven |

## What makes the most difference

1. `network_client_message_handler.c`, `network_client_manager.c` and
   `ui_widget.c`: three files carry about a fifth of the lines and most of
   this rebase's conflicts' risk. Moving their switches first saves the
   most work per rebase.
2. Wave 1 in one pull request: large but mechanical.
3. A `--console-limits` build in ChupathingyCE's CI, so a change to the
   settings record or Slayer's state shows up there, not in Warthog.

Once these are in, Warthog's own tree is `port/xbox`, the console's
`tools/xbox_*.py`, `port/console` and the other consoles' scaffolding, and
its docs, the shape the ChupathingyCE organization planned (chupathingyce as a
submodule).
