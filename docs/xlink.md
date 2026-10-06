# XLink mode (design)

[XLink Kai](https://www.teamxlink.co.uk/) carries consoles' system link over
the internet. CrunchBite (XLink Kai's developer, on the ChupathingyCE
Discord) described the addressing that lets Kai pass a console's traffic:

- take the console's MAC address, `AA:BB:CC:DD:EE:FF`;
- give the console the address `10.252.EE.FF` with the mask `255.255.0.0`
  (its last two bytes, in decimal: `10.252.238.255` here);
- Kai then carries it across its tunnel. The rule is the same for the Xbox,
  360, One and Series; XLink's [IP calculator](https://teamxlink.co.uk/ipcalculator/)
  works it out.

That would let a Warthog console play system link with other consoles on
Kai, Xbox 360 players among them once a 360 build exists, without the
internet play tunnel the PC builds use.

## What is built

Only the rule and a check (the "small hooks"):

- `port/xbox/src/xbox_xlink.c`: `xbox_xlink_address` gives the address and
  mask for a MAC, `xbox_xlink_matches` says whether an address is it.
  Tested on the host (`port/xbox/tests/run.sh`, `xlink_test`).
- At the start, `transport_initialize` (in
  `source/bungie_net/network/transport_endpoint_set_winsock.c`) writes one
  line to `debug.txt`: whether the console's address is Kai's for its MAC,
  and what Kai's would be. The MAC itself is never logged; the address is
  a private one, which the logging rules allow (`log_address.h`).

So today a player who wants Kai sets the static address in the dashboard's
network settings (address `10.252.EE.FF`, mask `255.255.0.0`, the gateway
and DNS of their network as before), and the log confirms it.

## XLink mode (not built)

A setting, off by default, that has the game take Kai's address itself:

1. **The setting.** The console has no config file yet, so it would follow
   the other console switches: an empty `D:\xlink.txt` beside `default.xbe`
   (later, the settings screen planned outside the game).
2. **Assigning the address.** The title cannot hand XNet an address through
   `XNetStartupParams`; the dashboard does it with XNet's configuration
   calls (`XNetLoadConfigParams`, `XNetConfig`, `XNetSaveConfigParams`).
   The 5933 `xnet.lib` exports them, but the SDK's headers leave them and
   their parameter structure out. That is exactly the case the
   `XNetStartupParams` and `XNADDR` bugs taught us to measure, not guess:
   the structure's size and layout must be read
   from the library (and checked with `xnetd.lib`, whose asserts name a
   wrong size) before the game calls it. Until then XLink mode stays a
   design.
3. **Only for the run.** The mode would apply the address for the session
   (`XNetConfig` without saving), never write the dashboard's saved
   settings, so turning it off, or a crash, leaves the console as it was.
4. **Coexisting with DHCP and internet play.** Kai's address has no gateway
   or DNS that reach the internet unless the player's network routes
   `10.252.0.0/16`, so XLink mode and internet play (the game list, the
   tunnel, `D:\bypass_security.txt`) are either/or: with `D:\xlink.txt`,
   the game list and ONLINE GAMES' joins say "XLink mode: system link
   only". Without it, DHCP or the dashboard's settings stay in charge, as
   now.
5. **Secure system link.** Kai carries the console's own traffic, so XLink
   mode is for console-to-console system link and should run with XNet's
   security on (no `bypass_security.txt`), as retail system link did.
   Playing PCs stays the tunnel's job.

## What it would enable, and what to check

- System link between Warthog consoles anywhere on Kai, and with retail
  Halo on original Xboxes if the protocol versions match (they do not
  today: Warthog speaks the PC builds' network version).
- With an Xbox 360 build, 360 players on Kai.
- To test on hardware: Kai's own client sees the console with the static
  address set by hand; two consoles on Kai find each other's game in
  System Link; and the edge MACs (last two bytes `00 00` or `FF FF`, which
  give the /16's network or broadcast address) are XLink's to answer,
  so `xbox_xlink_address` refuses them for now.
