/*
XBOX_IDENTITY.C

Which build this is, and the network versions it plays with, as the
desktop builds' build_identity.c and delta.c answer them. The console has
no version of its own yet (no release) and no Delta legacy table (it
fetches none and keeps no cache), so its numbers are the built-in ones of
halo_port_limits.h: it announces HALO_PORT_NETWORK_VERSION and joins
hosts of HALO_PORT_NETWORK_VERSION_MINIMUM through _MAXIMUM.
*/

#ifdef HALO_XBOX_CONSOLE

#include "cseries/cseries.h"

#include "halo_port_limits.h"

#include <stdio.h>

/* ---------- build_identity.h */

/* "Warthog (original Xbox, network version 28, built Oct 13 2026)" */
char const *build_identity(
	void)
{
	static char identity[96];

	if (!identity[0])
	{
		snprintf(identity, sizeof(identity), "Warthog (original Xbox, network version %d, built %s)",
			(int)HALO_PORT_NETWORK_VERSION, __DATE__);
	}

	return identity;
}

/* ---------- the legacy table's numbers (halo_port_limits.h) */

int delta_legacy_announce(
	void)
{
	return HALO_PORT_NETWORK_VERSION;
}

int delta_legacy_minimum(
	void)
{
	return HALO_PORT_NETWORK_VERSION_MINIMUM;
}

int delta_legacy_maximum(
	void)
{
	return HALO_PORT_NETWORK_VERSION_MAXIMUM;
}

#endif
