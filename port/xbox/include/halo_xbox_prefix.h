/*
HALO_XBOX_PREFIX.H

Force-included ahead of every game unit of the Xbox build (clang, through
OXDK; port/xbox/build.py). The game compiles against the January SDK's
declarations (port/include/xdk), with only the C runtime's headers from the
XDK.
*/

#ifndef __HALO_XBOX_PREFIX_H
#define __HALO_XBOX_PREFIX_H

/* The debug decomp's developer tools (the profiler, the AI, render and
collision debug, the stack walker, libtiff and the bitmap tools) are not in
the console's build unless it is configured with --console-debug-tools
(HALO_CONSOLE_DEBUG_TOOLS). Their modules keep their code under
#ifndef HALO_CONSOLE_NO_DEBUG_TOOLS and answer their calls with stubs;
console_debug_tools_unavailable (port/xbox/game/console_debug_tools.c) says so
for the console commands and scripts that reach them. */
#ifndef HALO_CONSOLE_DEBUG_TOOLS
#define HALO_CONSOLE_NO_DEBUG_TOOLS
void console_debug_tools_unavailable(char const *name);
#endif

/* The consoles have no telnet script console (source/networking/
telnet_console.h) */
#define HALO_CONSOLE_NO_TELNET

/* and no automated network tests (port/linux/game/network_test.c) */
#define HALO_CONSOLE_NO_NETWORK_TEST

/* A console's game never exits: what would quit on the desktop (the audio
probe's end: port/linux/game/audio_probe.c) goes back to the main menu */
#define HALO_CONSOLE_NO_EXIT

/* and no high-res text or HUD: the maps' own fonts and bitmaps, as retail */
#define HALO_CONSOLE_NO_HIRES

/* and no players' names above their heads (hud.c): retail names the player
under the reticle only */
#define HALO_CONSOLE_NO_PLAYER_NAMES

/* and no tag schema or validator (port/linux/game/tag_validate.c: 170 KB of
the image, and already off on a 64 MB console, which has no room for its
claims): the maps are the Xbox's own */
#define HALO_CONSOLE_NO_TAG_VALIDATE

/* and, for now, none of the desktop's other extras that a console leaves out
(port/linux/include/halo_port_features.h): Discord and the updater, the
object functions drawn between ticks (384 KB), the Controls screen and Delta
Profile in the game */
#define HALO_CONSOLE_NO_DISCORD
#define HALO_CONSOLE_NO_UPDATER
#define HALO_CONSOLE_NO_SMOOTH_FUNCTIONS
#define HALO_CONSOLE_NO_CONTROLS
#define HALO_CONSOLE_NO_DELTA_PROFILE

/* The frame timing log, console.perf_log (port/linux/include/
halo_perf_log.h, port/xbox/game/xbox_perf_log.c) */
#define HALO_PERF_LOG

/* clang predeclares the MSVC _Interlocked* intrinsics with prototypes that
conflict with the SDK's; the game never calls them (xdk_win32.h) */
#define _InterlockedCompareExchange halo_xbox_InterlockedCompareExchange
#define _InterlockedDecrement halo_xbox_InterlockedDecrement
#define _InterlockedExchange halo_xbox_InterlockedExchange
#define _InterlockedExchangeAdd halo_xbox_InterlockedExchangeAdd
#define _InterlockedIncrement halo_xbox_InterlockedIncrement

/* rasterizer_xbox_debug.h declares rasterizer_debug_drawing_begin with a
second parameter the definition ignores; MSVC tolerates the two declarations
meeting, clang does not (as halo_linux_source_fixups.h) */
#define rasterizer_debug_drawing_begin(opaque, ...) (rasterizer_debug_drawing_begin)(opaque)

/* The DirectSound calls whose January form (mix bins as a mask) the SDK
library no longer takes: port/xbox/src/xbox_dsound.c translates them */
#define DirectSoundCreateBuffer halo_xbox_DirectSoundCreateBuffer
#define IDirectSound_CreateSoundBuffer halo_xbox_IDirectSound_CreateSoundBuffer
#define IDirectSound_CreateSoundStream halo_xbox_IDirectSound_CreateSoundStream
#define IDirectSound_DownloadEffectsImage halo_xbox_IDirectSound_DownloadEffectsImage
#define IDirectSound_SetMixBinHeadroom halo_xbox_IDirectSound_SetMixBinHeadroom
#define IDirectSoundStream_SetMixBins halo_xbox_IDirectSoundStream_SetMixBins
#define IDirectSoundStream_SetMixBinVolumes halo_xbox_IDirectSoundStream_SetMixBinVolumes

/* The game's Winsock calls that internet play's tunnel must see (port/xbox/
src/xbox_winsock_hooks.c, as the desktop builds' port/linux/src/xnet.c):
renamed in the game's units, the SDK's declarations of them included */
#define WSAStartup halo_xbox_WSAStartup
#define socket halo_xbox_socket
#define closesocket halo_xbox_closesocket
#define bind halo_xbox_bind
#define connect halo_xbox_connect
#define listen halo_xbox_listen
#define accept halo_xbox_accept
#define recvfrom halo_xbox_recvfrom
#define recv halo_xbox_recv
#define sendto halo_xbox_sendto
#define getpeername halo_xbox_getpeername
#define select halo_xbox_select
#define XNetXnAddrToInAddr halo_xbox_XNetXnAddrToInAddr
#define XNetGetTitleXnAddr halo_xbox_XNetGetTitleXnAddr
#define XNetRegisterKey halo_xbox_XNetRegisterKey
#define XNetUnregisterKey halo_xbox_XNetUnregisterKey

/* the shared sources' limits, as the native builds' prefix includes them:
the console's own (HALO_XBOX_CONSOLE) */
#include "port/linux/include/halo_port_limits.h"
/* the desktop builds' mouse in the menus, which the console has none of
(port/xbox/src/xbox_port.c) */
#include "port/linux/include/halo_ui_pointer.h"

#endif
