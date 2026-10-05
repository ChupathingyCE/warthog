/*
HALO_XBOX_PREFIX.H

Force-included ahead of every game unit of the Xbox build (clang, through
OXDK; tools/xbox_build.py). The game compiles against the January SDK's
declarations (port/include/xdk), with only the C runtime's headers from the
XDK.
*/

#ifndef __HALO_XBOX_PREFIX_H
#define __HALO_XBOX_PREFIX_H

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
#define sendto halo_xbox_sendto
#define getpeername halo_xbox_getpeername
#define XNetXnAddrToInAddr halo_xbox_XNetXnAddrToInAddr

/* the shared sources' limits, as the native builds' prefix includes them:
the console's own (HALO_XBOX_CONSOLE) */
#include "../../linux/include/halo_port_limits.h"
/* the desktop builds' mouse in the menus, which the console has none of
(port/xbox/src/xbox_port.c) */
#include "../../linux/include/halo_ui_pointer.h"

#endif
