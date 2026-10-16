/*
HALO_XBOX360_PREFIX.H

Force-included ahead of every game unit and every Xbox-facing platform unit
of the Xbox 360 build (tools/xbox360_build.py); the units that talk to the
console itself (port/xbox360/src/x360_*.c) never see it. The build is the
Windows build's arrangement (port/windows/README.md) on the console's
PowerPC: the game and the desktop platform layer (port/linux/src) see the
Xbox SDK declarations of port/include/xdk, the console's own SDK is only
for x360_*.c, and the functions both define are renamed here.

The target is 32-bit and big-endian. Pointers are 32 bits wide, as on the
Xbox, but the Xbox's addresses are the console's executable's, so the game
runs with the 64-bit builds' pointer mode (HALO_64BIT: pointer fields of
Xbox data are Xbox addresses) over a relocatable window (HALO_XBOX_WINDOW,
source/cseries/xbox_address.h).
*/

#ifndef __HALO_XBOX360_PREFIX_H
#define __HALO_XBOX360_PREFIX_H

#if !defined(_M_PPCBE) || !defined(HALO_64BIT) || !defined(HALO_XBOX_WINDOW)
#error the Xbox 360 build is PowerPC (_M_PPCBE) with the relocatable Xbox window (HALO_64BIT, HALO_XBOX_WINDOW)
#endif

#define HALO_XBOX360 1
#define HALO_XBOX360_CONSOLE 1
#define HALO_BIG_ENDIAN 1

/* ---------- pointers inside Xbox data */

#include "../../../source/cseries/xbox_address.h"

/* ---------- the C runtime

The console's C runtime headers come first, before the MSVC inline
semantics below redefine __inline (they spell their own inline helpers
with it). */

#define _USE_MATH_DEFINES
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS
/* the MSVC 7 signatures the game uses: swprintf without a buffer size,
two-argument wcstok */
#define _CRT_NON_CONFORMING_SWPRINTFS
#define _CRT_NON_CONFORMING_WCSTOK
/* the ctype functions as functions (the game declares isspace itself) */
#define _CTYPE_DISABLE_MACROS
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <float.h>
#include <ctype.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>
#include <malloc.h>

/* ---------- MSVC inline semantics (as the Linux build: halo_linux_prefix.h)

The console's compiler is clang for PowerPC ELF, which has no COMDAT C
inline functions: every unit gets its own copy, and the generated
halo_msvc_semantics.h makes those with external linkage weak. */

#define __inline static __inline__
#define _inline static __inline__
#define __forceinline static __inline__ __attribute__((always_inline))

#define DECLSPEC_SELECTANY __attribute__((weak))

/* ---------- MSVC intrinsics (the platform layer's, as on Linux) */

#define _InterlockedCompareExchange halo_linux_InterlockedCompareExchange
#define _InterlockedDecrement halo_linux_InterlockedDecrement
#define _InterlockedExchange halo_linux_InterlockedExchange
#define _InterlockedExchangeAdd halo_linux_InterlockedExchangeAdd
#define _InterlockedIncrement halo_linux_InterlockedIncrement

/* ---------- structured exception handling (compiled out, as on Linux) */

#define __try if (1)
#define __except(filter) else if (0)
#define __finally
#define __leave

/* ---------- multiplayer session limits: the native builds' */

#include "../../linux/include/halo_port_limits.h"

#define FD_SETSIZE HALO_PORT_FD_SETSIZE

/* ---------- Xbox functions named like the console's own */

#include "halo_xbox360_api_names.h"

/* ---------- Winsock and source fixups shared with the Linux build */

#ifndef HALO_LINUX_PLATFORM_LAYER
/* the game's own strnlen (source/cseries/cseries.c) */
#define strnlen halo_game_strnlen
#include "../../linux/include/halo_linux_winsock_names.h"
#include "../../linux/include/halo_linux_source_fixups.h"
#endif

#endif /* __HALO_XBOX360_PREFIX_H */
