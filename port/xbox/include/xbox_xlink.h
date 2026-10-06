/*
XBOX_XLINK.H

XLink Kai's addressing for consoles (docs/xlink.md): a console whose MAC
address is AA:BB:CC:DD:EE:FF takes 10.252.EE.FF with the mask 255.255.0.0,
and Kai carries its system link across its tunnel. The address is XLink's
rule (its IP calculator, https://teamxlink.co.uk/ipcalculator/), the same
for the Xbox, 360, One and Series.

Only the rule is here; the console checks its own address against it at
the start (transport_endpoint_set_winsock.c). Setting the address is the
dashboard's (docs/xlink.md, "XLink mode").
*/

#ifndef __XBOX_XLINK_H
#define __XBOX_XLINK_H

/* XLink Kai's address and mask for a MAC address (6 bytes), in host byte
order (10.252.EE.FF is 0x0AFCEEFF): 1, or 0 if the last two bytes give no
host's address in the /16 (both 0, or both 255) */
int xbox_xlink_address(const unsigned char *mac, unsigned long *address, unsigned long *mask);

/* whether an address (host byte order) is XLink's for this MAC */
int xbox_xlink_matches(const unsigned char *mac, unsigned long address);

#endif
