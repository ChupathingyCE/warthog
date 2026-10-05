/*
XBOX_WINSOCK_HOOKS.C

The game's Winsock calls on the console, through internet play's tunnel
(port/xbox/src/xbox_p2p.c) as the desktop builds' go through theirs
(port/linux/src/xnet.c, whose logic this follows): halo_xbox_prefix.h
renames the calls the game makes to these, which call XNet's own.

- A peer of the tunnel has a virtual address in 100.64.0.0/10, which
  XNetXnAddrToInAddr gives for its XNADDR (whose abEnet is its identifier).
- The game's datagrams to a peer go onto the tunnel at once
  (p2p_send_datagram); its broadcasts (the system link search) go to every
  peer too. Its connections to a peer go to the tunnel's stand-in for it
  (p2p_outgoing), and what comes from a stand-in comes from the peer
  (p2p_incoming).
- The tunnel is told the game's sockets' ports, which alone peers reach
  (p2p_socket_port), and when one closes.

Without internet play (no D:\bypass_security.txt, or the tunnel off), every
p2p_ call answers "not a peer's", and these are XNet's calls as they were.
Nothing here logs an address.
*/

#include <xtl.h>
#include <string.h>

#include "../../linux/src/p2p.h"

/* (the socket types the game made, by socket: XNet's SO_TYPE is not relied on) */
enum
{
	MAXIMUM_TRACKED_SOCKETS = 64,
};

static struct
{
	SOCKET socket;
	int type;
} tracked[MAXIMUM_TRACKED_SOCKETS];

static int is_virtual(unsigned long network_address)
{
	return (ntohl(network_address) & 0xFFC00000UL) == 0x64400000UL;
}

static void track(SOCKET socket, int type)
{
	int index;

	for (index = 0; index < MAXIMUM_TRACKED_SOCKETS; index++)
	{
		if (!tracked[index].socket || tracked[index].socket == INVALID_SOCKET)
		{
			tracked[index].socket = socket;
			tracked[index].type = type;
			return;
		}
	}
}

static int socket_type(SOCKET socket)
{
	int index;
	int type = SOCK_DGRAM;
	int length = sizeof(type);

	for (index = 0; index < MAXIMUM_TRACKED_SOCKETS; index++)
	{
		if (tracked[index].socket == socket)
			return tracked[index].type;
	}
	getsockopt(socket, SOL_SOCKET, SO_TYPE, (char *)&type, &length);
	return type;
}

static void untrack(SOCKET socket)
{
	int index;

	for (index = 0; index < MAXIMUM_TRACKED_SOCKETS; index++)
	{
		if (tracked[index].socket == socket)
			tracked[index].socket = 0;
	}
}

/* the local port the socket is bound to (network byte order), or 0 */
static unsigned short socket_port(SOCKET socket)
{
	struct sockaddr_in bound;
	int length = sizeof(bound);

	if (getsockname(socket, (struct sockaddr *)&bound, &length) != 0 || bound.sin_family != AF_INET)
		return 0;
	return bound.sin_port;
}

/* tells internet play the local port the game's socket has */
static void note_socket_port(SOCKET socket, int listening)
{
	unsigned short port = socket_port(socket);

	if (port)
		p2p_socket_port((int)socket, socket_type(socket) == SOCK_STREAM, listening, port);
}

/* a destination of a peer's goes to its stand-in (1); -1 (WSAEHOSTUNREACH)
if it is a peer's that cannot be reached now; 0 if it is not a peer's */
static int outgoing(int stream, int connecting, const struct sockaddr **address, int address_length,
	struct sockaddr_in *storage)
{
	unsigned long ip;
	unsigned short port;
	int result;

	if (!*address || (*address)->sa_family != AF_INET || address_length < (int)sizeof(*storage))
		return 0;
	ip = ((const struct sockaddr_in *)*address)->sin_addr.s_addr;
	port = ((const struct sockaddr_in *)*address)->sin_port;
	result = p2p_outgoing(stream, connecting, &ip, &port);
	if (result < 0)
		WSASetLastError(WSAEHOSTUNREACH);
	if (result <= 0)
		return result;
	memcpy(storage, *address, sizeof(*storage));
	storage->sin_addr.s_addr = ip;
	storage->sin_port = port;
	*address = (const struct sockaddr *)storage;
	return 1;
}

/* traffic from a stand-in comes from its peer */
static void incoming(int stream, struct sockaddr *address, const int *address_length)
{
	if (address && address_length && *address_length >= (int)sizeof(struct sockaddr_in) &&
		address->sa_family == AF_INET)
	{
		struct sockaddr_in *in = (struct sockaddr_in *)address;
		unsigned long ip = in->sin_addr.s_addr;
		unsigned short port = in->sin_port;
		XNADDR xnaddr;

		/* (XNet may give a loopback datagram this console's own address) */
		if (XNetGetTitleXnAddr(&xnaddr) != XNET_GET_XNADDR_PENDING && xnaddr.ina.s_addr && ip == xnaddr.ina.s_addr)
			ip = htonl(INADDR_LOOPBACK);
		if (p2p_incoming(stream, &ip, &port))
		{
			in->sin_addr.s_addr = ip;
			in->sin_port = port;
		}
	}
}

SOCKET WSAAPI halo_xbox_socket(int family, int type, int protocol)
{
	SOCKET result = socket(family, type, protocol);

	if (result != INVALID_SOCKET)
		track(result, type);
	return result;
}

int WSAAPI halo_xbox_closesocket(SOCKET socket)
{
	int stream = socket_type(socket) == SOCK_STREAM;

	p2p_socket_closed((int)socket, stream ? 0 : socket_port(socket));
	untrack(socket);
	return closesocket(socket);
}

int WSAAPI halo_xbox_bind(SOCKET socket, const struct sockaddr *address, int address_length)
{
	int result = bind(socket, address, address_length);

	if (result == 0)
		note_socket_port(socket, 0);
	return result;
}

int WSAAPI halo_xbox_connect(SOCKET socket, const struct sockaddr *address, int address_length)
{
	struct sockaddr_in target;
	int result;
	int error;

	if (address && address->sa_family == AF_INET && address_length >= (int)sizeof(target) &&
		is_virtual(((const struct sockaddr_in *)address)->sin_addr.s_addr) &&
		outgoing(socket_type(socket) == SOCK_STREAM, (int)socket, &address, address_length, &target) < 0)
	{
		return SOCKET_ERROR;
	}
	result = connect(socket, address, address_length);
	error = result != 0 ? WSAGetLastError() : 0;
	note_socket_port(socket, 0);
	if (result != 0)
		WSASetLastError(error);
	return result;
}

int WSAAPI halo_xbox_listen(SOCKET socket, int backlog)
{
	int result = listen(socket, backlog);

	if (result == 0)
		note_socket_port(socket, 1);
	return result;
}

SOCKET WSAAPI halo_xbox_accept(SOCKET socket, struct sockaddr *address, int *address_length)
{
	SOCKET result = accept(socket, address, address_length);

	if (result != INVALID_SOCKET)
	{
		track(result, SOCK_STREAM);
		incoming(1, address, address_length);
	}
	return result;
}

int WSAAPI halo_xbox_recvfrom(SOCKET socket, char *buffer, int length, int flags, struct sockaddr *address,
	int *address_length)
{
	int result = recvfrom(socket, buffer, length, flags, address, address_length);

	if (result != SOCKET_ERROR)
		incoming(0, address, address_length);
	return result;
}

int WSAAPI halo_xbox_getpeername(SOCKET socket, struct sockaddr *address, int *address_length)
{
	int result = getpeername(socket, address, address_length);

	if (result == 0)
		incoming(socket_type(socket) == SOCK_STREAM, address, address_length);
	return result;
}

int WSAAPI halo_xbox_sendto(SOCKET socket, const char *buffer, int length, int flags, const struct sockaddr *address,
	int address_length)
{
	struct sockaddr_in target;
	unsigned short source_port = socket_port(socket);
	int result;
	int error;

	if (address && address->sa_family == AF_INET && address_length >= (int)sizeof(target) &&
		((const struct sockaddr_in *)address)->sin_addr.s_addr == INADDR_BROADCAST)
	{
		unsigned long targets[16];
		unsigned short ports[16];
		int count;
		int index;

		/* the LAN's broadcast, then every peer's: onto the tunnel at once,
		else through their stand-ins */
		result = sendto(socket, buffer, length, flags, address, address_length);
		error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
		if (!source_port)
		{
			source_port = socket_port(socket);
			if (source_port)
				note_socket_port(socket, 0);
		}
		if (source_port)
		{
			if (p2p_broadcast_datagram(source_port, ((const struct sockaddr_in *)address)->sin_port, buffer, length) > 0 &&
				result == SOCKET_ERROR)
			{
				result = length;
			}
		}
		else
		{
			memcpy(&target, address, sizeof(target));
			count = p2p_broadcast_targets(((const struct sockaddr_in *)address)->sin_port, targets, ports, 16);
			for (index = 0; index < count; index++)
			{
				target.sin_addr.s_addr = targets[index];
				target.sin_port = ports[index];
				if (sendto(socket, buffer, length, flags, (const struct sockaddr *)&target, sizeof(target)) !=
					SOCKET_ERROR && result == SOCKET_ERROR)
				{
					result = length;
				}
			}
		}
		if (result == SOCKET_ERROR)
			WSASetLastError(error);
		return result;
	}
	if (address && address->sa_family == AF_INET && address_length >= (int)sizeof(target) &&
		is_virtual(((const struct sockaddr_in *)address)->sin_addr.s_addr))
	{
		switch (source_port ? p2p_send_datagram(source_port, ((const struct sockaddr_in *)address)->sin_addr.s_addr,
			((const struct sockaddr_in *)address)->sin_port, buffer, length) : 0)
		{
		case 1:
			return length;
		case -1:
			WSASetLastError(WSAEHOSTUNREACH);
			return SOCKET_ERROR;
		}
		if (outgoing(0, -1, &address, address_length, &target) < 0)
			return SOCKET_ERROR;
	}
	result = sendto(socket, buffer, length, flags, address, address_length);
	error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
	if (!source_port)
		note_socket_port(socket, 0);
	if (result == SOCKET_ERROR)
		WSASetLastError(error);
	return result;
}

/* a peer's XNADDR (its abEnet the tunnel's identifier) gives its virtual
address; any other, XNet's */
INT WSAAPI halo_xbox_XNetXnAddrToInAddr(const XNADDR *address, const XNKID *key_identifier, IN_ADDR *result)
{
	unsigned long peer;

	if (address && result && p2p_peer_address(address->abEnet, &peer))
	{
		result->s_addr = peer;
		return 0;
	}
	return XNetXnAddrToInAddr(address, key_identifier, result);
}

/* Winsock started (after XNet: transport_endpoint_set_winsock.c): internet
play starts too, before the game makes any socket, so that it knows them
all (on with D:\bypass_security.txt: xbox_p2p.c's network.online). Its
stand-ins are this console's own, at 127.0.0.1 */
int WSAAPI halo_xbox_WSAStartup(WORD version_requested, LPWSADATA data)
{
	int result = WSAStartup(version_requested, data);

	if (result == 0)
		p2p_initialize(htonl(INADDR_LOOPBACK));
	return result;
}
