/*
PLATFORM.H (the console's)

Internet play's sources (port/linux/src/p2p.c, p2p_signal.c, p2p_crypto.c)
are the desktop builds' own, compiled for the console as they are
(tools/xbox_build.py copies them into the build folder, so that their
quoted includes find this folder's headers first). This stands in for the
Linux platform layer's platform.h: the SDK's declarations, and the little
of the layer they call (port/xbox/src/xbox_p2p.c).
*/

#ifndef __HALO_XBOX_P2P_PLATFORM_H
#define __HALO_XBOX_P2P_PLATFORM_H

/* the console joins, never hosts: room in p2p.c for its host's stand-ins
and streams, not a 128-machine game's */
#define P2P_JOINER_ONLY 1

/* (the tunnel's select lists: its sockets, the brokers' and the stand-ins') */
#define FD_SETSIZE 256

#include <xtl.h>
#include <stddef.h>
#include <pthread.h>

/* (the console's C runtime has _snprintf; port/xbox/src/xbox_port.c gives
the C99 one) */
int snprintf(char *buffer, size_t size, const char *format, ...);

/* to D:\debug.txt and the debug monitor; never an address (log_address.h) */
void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
/* where the hand-off socket's file would be (unused on the console) */
const char *platform_data_root(void);

#endif
