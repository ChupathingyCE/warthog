/*
HALO_WIIU_PREFIX.H

Force-included ahead of compilation units for the Nintendo Wii U build.
Target: PowerPC Espresso (750CL) 32-bit big-endian with devkitPPC and wut.
*/

#ifndef __HALO_WIIU_PREFIX_H
#define __HALO_WIIU_PREFIX_H

#ifndef __WIIU__
#define __WIIU__ 1
#endif
#ifndef __WUT__
#define __WUT__ 1
#endif

#define HALO_CONSOLE 1
#define HALO_WIIU_CONSOLE 1
#define HALO_BIG_ENDIAN 1

/* Nintendo Wii U has 1 GB of memory available for applications */
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 128
#define HALO_PORT_MAXIMUM_NETWORK_MACHINES 128
#define HALO_PORT_FD_SETSIZE 256

/* Console endianness and safe memory reading utilities */
#include "../../console/include/console_endian.h"
#include "../../console/include/console_common.h"
#include "../../console/include/console_input.h"

/* Disable mouse UI cursor in console menus */
#define halo_ui_pointer_active() (0)

#endif /* __HALO_WIIU_PREFIX_H */
