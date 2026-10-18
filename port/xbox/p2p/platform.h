/*
PLATFORM.H (the console's)

Internet play's sources (port/linux/src/p2p.c, p2p_signal.c, p2p_crypto.c)
and Delta Peer's (delta_wire.c, delta_peer.c, delta_peer_game.c) are the desktop builds' own, compiled for the console as they are
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
/* and logs what becomes of each datagram a peer sends the game */
#define P2P_TRACE_DATAGRAMS 1
/* XNet lets one thread at a time use a socket (another thread's call fails
with WSAEINPROGRESS), and its select holds every socket it waits on for the
whole wait: the game's datagrams for peers go out from the tunnel's thread */
#define P2P_SEND_FROM_THREAD 1

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

/* Delta Peer's socket (port/linux/src/delta_peer_game.c), named as the
desktop builds' Winsock layer (xnet.c) names its calls: the game's hooks
(port/xbox/src/xbox_winsock_hooks.c), so that its datagrams go where the
game's go, the tunnel's peers' included */
SOCKET WSAAPI halo_xbox_socket(int family, int type, int protocol);
int WSAAPI halo_xbox_closesocket(SOCKET socket);
int WSAAPI halo_xbox_bind(SOCKET socket, const struct sockaddr *address, int address_length);
int WSAAPI halo_xbox_recvfrom(SOCKET socket, char *buffer, int length, int flags, struct sockaddr *address,
	int *address_length);
int WSAAPI halo_xbox_sendto(SOCKET socket, const char *buffer, int length, int flags, const struct sockaddr *address,
	int address_length);
#define halo_ws_socket halo_xbox_socket
#define halo_ws_closesocket halo_xbox_closesocket
#define halo_ws_bind halo_xbox_bind
#define halo_ws_recvfrom halo_xbox_recvfrom
#define halo_ws_sendto halo_xbox_sendto
#define halo_ws_ioctlsocket ioctlsocket
#define halo_ws_getsockname getsockname
#define halo_ws_htons htons
#define halo_ws_ntohs ntohs

#endif
