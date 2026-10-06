/*
HALO_XBOX360_PREFIX.H

Force-included ahead of compilation units for the Xbox 360 (Xenon) build.
Target: PowerPC Xenon 32-bit big-endian with OXDK360 / llvm-xenon.
*/

#ifndef __HALO_XBOX360_PREFIX_H
#define __HALO_XBOX360_PREFIX_H

#ifndef _XBOX
#define _XBOX 1
#endif
#ifndef _XENON
#define _XENON 1
#endif
#ifndef XBOX
#define XBOX 1
#endif

#define HALO_CONSOLE 1
#define HALO_XBOX360_CONSOLE 1
#define HALO_BIG_ENDIAN 1

/* Xbox 360 has 512 MB of unified memory: session limits match desktop PCs (128 players) */
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 128
#define HALO_PORT_MAXIMUM_NETWORK_MACHINES 128
#define HALO_PORT_FD_SETSIZE 256

/* Console endianness and safe memory reading utilities */
#include "../../console/include/console_endian.h"
#include "../../console/include/console_common.h"
#include "../../console/include/console_input.h"

/* Disable mouse UI cursor in console menus */
#define halo_ui_pointer_active() (0)

#endif /* __HALO_XBOX360_PREFIX_H */
