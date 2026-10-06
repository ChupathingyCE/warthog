/*
HALO_SWITCH_PREFIX.H

Force-included ahead of compilation units for the Nintendo Switch build.
Target: ARM Cortex-A57 (AArch64) 64-bit little-endian with devkitA64 and libnx.
*/

#ifndef __HALO_SWITCH_PREFIX_H
#define __HALO_SWITCH_PREFIX_H

#ifndef __SWITCH__
#define __SWITCH__ 1
#endif

#define HALO_CONSOLE 1
#define HALO_SWITCH_CONSOLE 1

/* Nintendo Switch is 64-bit little-endian, matching the host PC endianness */
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 128
#define HALO_PORT_MAXIMUM_NETWORK_MACHINES 128
#define HALO_PORT_FD_SETSIZE 256

/* Console platform utilities */
#include "../../console/include/console_endian.h"
#include "../../console/include/console_common.h"
#include "../../console/include/console_input.h"

/* Disable mouse UI cursor in console menus */
#define halo_ui_pointer_active() (0)

#endif /* __HALO_SWITCH_PREFIX_H */
