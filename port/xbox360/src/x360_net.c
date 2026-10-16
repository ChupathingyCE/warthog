/*
X360_NET.C

The socket half of port/linux/src/posix.h on the console's Winsock (XNet):
what posix_net.c gives the desktop builds and win32_net.c the Windows
build. The game's Winsock and XNet are the desktop platform layer's
(port/linux/src/xnet.c), which calls these.

XNet starts insecure (XNET_STARTUP_BYPASS_SECURITY), as the original Xbox
build's does with bypass_security.txt: plain UDP and TCP with the PCs, the
game list and the internet, which a secure XNet refuses. Never an address
in the log (only its kind).
*/

#include <xtl.h>
#include <winsockx.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "posix.h"
#include "x360_port.h"

static volatile LONG network_state; /* 0 not started, 1 starting, 2 up, 3 failed */

static const char *address_kind(unsigned long address)
{
	if ((address >> 24) == 10 || (address >> 20) == 0xac1 || (address >> 16) == 0xc0a8)
		return "private";
	if ((address >> 16) == 0xa9fe)
		return "link-local";
	return "public";
}

int x360_network_start(void)
{
	XNetStartupParams parameters;
	WSADATA winsock;
	XNADDR address;
	DWORD status = XNET_GET_XNADDR_PENDING;
	DWORD link;
	int waited, result;

	if (network_state == 2)
		return 1;
	if (InterlockedCompareExchange(&network_state, 1, 0) != 0)
	{
		while (network_state == 1)
			Sleep(10);
		return network_state == 2;
	}
	memset(&parameters, 0, sizeof(parameters));
	parameters.cfgSizeOfStruct = sizeof(parameters);
	parameters.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
	result = XNetStartup(&parameters);
	if (result != 0)
	{
		x360_log("network: XNetStartup failed (%d)", result);
		network_state = 3;
		return 0;
	}
	result = WSAStartup(MAKEWORD(2, 2), &winsock);
	if (result != 0)
	{
		x360_log("network: WSAStartup failed (%d)", result);
		network_state = 3;
		return 0;
	}
	link = XNetGetEthernetLinkStatus();
	for (waited = 0; waited < 8000; waited += 100)
	{
		memset(&address, 0, sizeof(address));
		status = XNetGetTitleXnAddr(&address);
		if (status != XNET_GET_XNADDR_PENDING)
			break;
		Sleep(100);
	}
	if (status == XNET_GET_XNADDR_PENDING || status == XNET_GET_XNADDR_NONE || !address.ina.s_addr)
		x360_log("network: link %s, no address yet", (link & XNET_ETHERNET_LINK_ACTIVE) ? "up" : "down");
	else
		x360_log("network: link %s%s, a %s address (%d ms)", (link & XNET_ETHERNET_LINK_ACTIVE) ? "up" : "down",
			(link & XNET_ETHERNET_LINK_WIRELESS) ? " (wireless)" : "",
			address_kind((unsigned long)ntohl(address.ina.s_addr)), waited);
	network_state = 2;
	return 1;
}

/* ---------- the console's own address for 127.0.0.1

The game reaches itself at 127.0.0.1 (a host's own client joins its game
there: split screen and hosting). XNet has no loopback, so traffic to
127.0.0.1 goes to the console's own address, and traffic from that address
is reported as from 127.0.0.1, as xnet.c does for network.address on the
desktop. */

#define LOOPBACK_ADDRESS 0x7F000001UL

static unsigned long own_address(void)
{
	XNADDR address;

	memset(&address, 0, sizeof(address));
	if (XNetGetTitleXnAddr(&address) & XNET_GET_XNADDR_NONE)
		return 0;
	return address.ina.s_addr;
}

/* a destination of 127.0.0.1: the console's own address in storage */
static const void *outgoing(const void *address, int address_length, struct sockaddr_in *storage)
{
	const struct sockaddr_in *in = address;
	unsigned long own;

	if (!in || address_length < (int)sizeof(*in) || in->sin_family != AF_INET ||
		in->sin_addr.s_addr != htonl(LOOPBACK_ADDRESS) || !(own = own_address()))
	{
		return address;
	}
	memcpy(storage, in, sizeof(*storage));
	storage->sin_addr.s_addr = own;
	return storage;
}

/* a source of the console's own address: 127.0.0.1 */
static void incoming(void *address, const int *address_length)
{
	struct sockaddr_in *in = address;
	unsigned long own;

	if (in && address_length && *address_length >= (int)sizeof(*in) && in->sin_family == AF_INET &&
		(own = own_address()) != 0 && in->sin_addr.s_addr == own)
	{
		in->sin_addr.s_addr = htonl(LOOPBACK_ADDRESS);
	}
}

int posix_socket_last_error(void)
{
	return WSAGetLastError();
}

int posix_socket(int family, int type, int protocol)
{
	SOCKET handle;

	if (!x360_network_start())
	{
		WSASetLastError(WSAENETDOWN);
		return -1;
	}
	handle = socket(family, type, protocol);
	if (handle == INVALID_SOCKET)
		return -1;
	if (type == SOCK_DGRAM)
	{
		/* (a datagram socket may broadcast: system link's searches) */
		BOOL on = TRUE;

		setsockopt(handle, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on));
	}
	return (int)handle;
}

int posix_socket_close(int handle)
{
	return closesocket((SOCKET)handle) == 0 ? 0 : -1;
}

int posix_socket_bind(int handle, const void *address, int address_length)
{
	return bind((SOCKET)handle, address, address_length) == 0 ? 0 : -1;
}

int posix_socket_connect(int handle, const void *address, int address_length)
{
	struct sockaddr_in storage;

	address = outgoing(address, address_length, &storage);
	return connect((SOCKET)handle, address, address_length) == 0 ? 0 : -1;
}

int posix_socket_listen(int handle, int backlog)
{
	return listen((SOCKET)handle, backlog) == 0 ? 0 : -1;
}

int posix_socket_accept(int handle, void *address, int *address_length)
{
	SOCKET accepted = accept((SOCKET)handle, address, address_length);

	if (accepted == INVALID_SOCKET)
		return -1;
	incoming(address, address_length);
	return (int)accepted;
}

int posix_socket_send(int handle, const void *buffer, int length, int flags)
{
	int result = send((SOCKET)handle, buffer, length, flags);

	return result == SOCKET_ERROR ? -1 : result;
}

int posix_socket_sendto(int handle, const void *buffer, int length, int flags, const void *address,
	int address_length)
{
	struct sockaddr_in storage;
	int result;

	address = outgoing(address, address_length, &storage);
	result = sendto((SOCKET)handle, buffer, length, flags, address, address_length);

	return result == SOCKET_ERROR ? -1 : result;
}

int posix_socket_recv(int handle, void *buffer, int length, int flags)
{
	int result = recv((SOCKET)handle, buffer, length, flags);

	return result == SOCKET_ERROR ? -1 : result;
}

int posix_socket_recvfrom(int handle, void *buffer, int length, int flags, void *address, int *address_length)
{
	int result = recvfrom((SOCKET)handle, buffer, length, flags, address, address_length);

	if (result == SOCKET_ERROR)
		return -1;
	incoming(address, address_length);
	return result;
}

int posix_socket_shutdown(int handle, int how)
{
	return shutdown((SOCKET)handle, how) == 0 ? 0 : -1;
}

int posix_socket_set_nonblocking(int handle, int nonblocking)
{
	u_long value = nonblocking ? 1 : 0;

	return ioctlsocket((SOCKET)handle, FIONBIO, &value) == 0 ? 0 : -1;
}

int posix_socket_bytes_available(int handle, posix_ulong *count)
{
	u_long value = 0;

	if (ioctlsocket((SOCKET)handle, FIONREAD, &value) != 0)
		return -1;
	*count = value;
	return 0;
}

int posix_socket_set_nodelay(int handle)
{
	BOOL on = TRUE;

	return setsockopt((SOCKET)handle, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on)) == 0 ? 0 : -1;
}

int posix_socket_setsockopt(int handle, int level, int name, const void *value, int length)
{
	return setsockopt((SOCKET)handle, level, name, value, length) == 0 ? 0 : -1;
}

int posix_socket_getsockopt(int handle, int level, int name, void *value, int *length)
{
	return getsockopt((SOCKET)handle, level, name, value, length) == 0 ? 0 : -1;
}

int posix_socket_getsockname(int handle, void *address, int *address_length)
{
	return getsockname((SOCKET)handle, address, address_length) == 0 ? 0 : -1;
}

int posix_socket_getpeername(int handle, void *address, int *address_length)
{
	if (getpeername((SOCKET)handle, address, address_length) != 0)
		return -1;
	incoming(address, address_length);
	return 0;
}

static void fill_set(fd_set *set, const int *handles, int count)
{
	int index;

	FD_ZERO(set);
	for (index = 0; index < count && index < FD_SETSIZE; index++)
		FD_SET((SOCKET)handles[index], set);
}

static void keep_ready(fd_set *set, int *handles, int *count)
{
	int index, kept = 0;

	for (index = 0; index < *count; index++)
	{
		if (FD_ISSET((SOCKET)handles[index], set))
			handles[kept++] = handles[index];
	}
	*count = kept;
}

int posix_socket_select(int *read, int *read_count, int *write, int *write_count, int *error, int *error_count,
	posix_long timeout_seconds, posix_long timeout_microseconds, int infinite)
{
	fd_set read_set, write_set, error_set;
	struct timeval timeout;
	int result;

	fill_set(&read_set, read, read ? *read_count : 0);
	fill_set(&write_set, write, write ? *write_count : 0);
	fill_set(&error_set, error, error ? *error_count : 0);
	timeout.tv_sec = timeout_seconds;
	timeout.tv_usec = timeout_microseconds;
	if (!read_set.fd_count && !write_set.fd_count && !error_set.fd_count)
	{
		/* (Winsock fails a select of nothing: a wait) */
		if (!infinite)
			Sleep((DWORD)(timeout_seconds * 1000 + timeout_microseconds / 1000));
		return 0;
	}
	result = select(0, read ? &read_set : NULL, write ? &write_set : NULL, error ? &error_set : NULL,
		infinite ? NULL : &timeout);
	if (result == SOCKET_ERROR)
		return -1;
	if (read)
		keep_ready(&read_set, read, read_count);
	if (write)
		keep_ready(&write_set, write, write_count);
	if (error)
		keep_ready(&error_set, error, error_count);
	return result;
}

posix_ulong posix_local_ipv4_address(void)
{
	XNADDR address;

	if (!x360_network_start())
		return 0;
	memset(&address, 0, sizeof(address));
	if (XNetGetTitleXnAddr(&address) & (XNET_GET_XNADDR_NONE))
		return 0;
	return address.ina.s_addr;
}

posix_ulong posix_resolve_ipv4(const char *host)
{
	unsigned long address = inet_addr(host);
	XNDNS *lookup = NULL;
	WSAEVENT event;
	posix_ulong result = 0;

	if (address != INADDR_NONE)
		return address;
	if (!x360_network_start())
		return 0;
	event = WSACreateEvent();
	if (XNetDnsLookup(host, event, &lookup) != 0 || !lookup)
	{
		WSACloseEvent(event);
		return 0;
	}
	WaitForSingleObject(event, 10000);
	if (lookup->iStatus == 0 && lookup->cina > 0)
		result = lookup->aina[0].s_addr;
	XNetDnsRelease(lookup);
	WSACloseEvent(event);
	return result;
}

/* no UPnP on the console (yet) */
int posix_upnp_forward_udp(unsigned short port, unsigned short preferred_port, posix_ulong *external_address,
	unsigned short *external_port, char *error, int error_size)
{
	(void)port;
	(void)preferred_port;
	(void)external_address;
	(void)external_port;
	if (error_size > 0)
	{
		_snprintf(error, error_size - 1, "no UPnP on the Xbox 360");
		error[error_size - 1] = '\0';
	}
	return 0;
}

void posix_upnp_stop_forwarding_udp(unsigned short external_port)
{
	(void)external_port;
}
