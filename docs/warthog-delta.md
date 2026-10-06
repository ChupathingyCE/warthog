# Warthog as a Delta machine (plan)

Warthog is to be a full Delta machine, not only a client of the legacy
protocol. Delta's code lands in ChupathingyCE first (it is not in ChupathingyCE's
main yet) and reaches
Warthog by the usual one-way flow. This page is what the original Xbox
needs for each part, what is hard there, and the order to do it in. Nothing
here is built on the console yet. Delta itself is described in
ChupathingyCE's `docs/delta.md`.

What the console already has that Delta can use:

- the tunnel (`p2p.c`, `p2p_signal.c`, `p2p_crypto.c`, KCP and Monocypher,
  Ed25519 included), built for the console as a joiner;
- XNet's Winsock in both modes: secure system link, and insecure
  (`D:\bypass_security.txt`) for the internet and the PCs;
- a plain-HTTP client with bounded, fuzzed parsers (the game list);
- the memory class (`port/xbox/src/xbox_memory.c`).

## Delta Peer

**The codec as it is.** `delta_wire.c` and `.h` are plain C89, fixed-size,
with no allocation, written so the console can use them: they build in the
console's support units unchanged. `delta_peer.c` (about 860 lines) keeps
a `struct delta_peer` sized for 128 machines: about 25 KB, static. That
stays at 128 even on the console, since a console that opted in may join a
game of 128, and the roster names every machine.

**The socket.** A UDP socket on Delta's port, 5160 (the game's 5150 plus
10), opened through the game's own Winsock calls, which on the console go
through `xbox_winsock_hooks.c`:

- *Insecure mode (the internet, the PCs):* the same path as the game's
  datagrams. The tunnel learns the socket's port (`p2p_socket_port`), a
  datagram to a peer's virtual address goes onto the tunnel
  (`p2p_send_datagram`), and one from a peer arrives in memory
  (`xbox_winsock_deliver`). The hooks already do this for every game
  socket, so Delta Peer on the internet needs no new path, as on the PCs.
- *Secure system link (console to console):* XNet encrypts and only talks
  to consoles whose keys are registered; a second UDP port works there as
  the game's does (XNet's VDP/UDP to the same registered host). Between a
  console and a PC on a LAN, insecure mode is required anyway.
- *The hard part:* XNet's socket count (`cfgSockMaxSockets`, 64 in the
  console's start parameters) and its receive queues are shared with the
  game, the tunnel's stand-ins and the game list; one more socket fits.

**The platform key.** `_delta_platform_xbox` (6) with:

| Field | The console's |
|---|---|
| host_players, join_players | 16, 16 (the policy's row) |
| join_players_opt_in | 128 |
| memory_class | from `xbox_memory_delta_class()` |
| flags | 0x01 when the player opted in (`D:\platform_limits_off.txt`, until there is a settings screen) |

**Memory classes: a proposal for `delta.h`.** Today: 1 up to 64 MB, 2 up to
512 MB, 3 up to 2 GB, 4 more. A 128 MB Xbox and an Xbox 360 then share
class 2, and a host cannot tell a stock Xbox's memory from a 128 MB one's
by anything finer. Delta Peer has not shipped, so the encoding can still
change without breaking anyone. Proposed: **the class is the memory's size
as a power of two in megabytes, rounded up** (log2), 0 unknown:

| Class | Memory | Machines |
|---|---|---|
| 6 | up to 64 MB | a stock Xbox |
| 7 | up to 128 MB | a devkit, the common upgrade |
| 8 | up to 256 MB | the bigger Xbox mods |
| 9 | up to 512 MB | Xbox 360 |
| 10, 11 | up to 1, 2 GB | Wii U (1 GB for games), phones |
| 12 and up | 4 GB and more | Switch, PCs |

It stays one byte, compares as a number (bigger is more), and needs no new
row when a console arrives. `DELTA_PLATFORM_POLICY`'s memory column becomes
6 for `xbox`. This must land in ChupathingyCE before Delta Peer's first
release; after that it would need a key version 2.

**The platform policy, honored on the console's side.** The host enforces
the room limit (`delta_peer_room_limit`), but the policy also says a
console's own build should not join a game already above its limit:
Warthog checks the advertised maximum against its key's `join_players`
(or `join_players_opt_in` when opted in) before joining, and says why. The
fold already leaves a game that grows past 16 (docs/cross-play.md).

**Co-op: unavailable on the `xbox` platform.** The decision for now:
the original Xbox plays no network co-op (Warthog compiles OpenCE's co-op
out: `port/xbox/game/xbox_coop.c`). So Delta hosts know without trying:

- add a policy column, `coop` (0 or 1), to `DELTA_PLATFORM_POLICY`, 0 for
  `xbox` (and for 360, Wii U and Switch until their ports say otherwise),
  1 for everyone else;
- a host with a Delta machine whose row says `coop = 0` in its lobby does
  not offer co-op (Server Setup's co-op choice greyed with the reason), and
  a host already running co-op refuses that machine's join with its own
  rejection reason, not "game full";
- Delta List marks co-op games, so the console's list leaves them out by
  the flag (today it guesses from the gametype name and a campaign map:
  `game_list_is_coop`);
- the signed legacy table's `platform_policy` can turn it on for a
  platform later without a release, within the build's bounds.

Without Delta (an OpenCE host), the console protects itself: it leaves a
game whose settings turn co-op, with a message, and lists no co-op games.

**Rate limits.** Delta Peer's own (a host takes 10 messages a second from a
machine, a client 50 from its host) are fine for the console. Its datagrams
are at most 1,200 bytes, under XNet's and the tunnel's limits. The console
reads at most 64 a frame, as the PCs do, from the main loop's network
idle.

**Code and memory on 64 MB.** The codec and peer are about 1,250 lines of C
(perhaps 20 KB of code), the state about 25 KB static, and nothing in the
game state (which is full). Comfortable on a stock Xbox.

## The signed legacy table

**Verifying.** The table is signed with Ed25519. The console already links
Monocypher's Ed25519 (`monocypher-ed25519.c`, for the tunnel), so it needs
no new crypto. One verification of a 16 KB document is a SHA-512 over it
and a few scalar multiplications: on the Xbox's 733 MHz Pentium III,
estimated at a few milliseconds, done once at start and when a newer table
arrives, on the fetch thread, never in a frame.

**The parser.** `delta.c` (Delta's legacy table) holds a strict JSON
reader with no allocation of its own; the signed table itself is held in a
malloc'd buffer of at most about 16 KB, which is fine.

**Where it caches.** `D:\delta_legacy.signed`, beside `default.xbe`, read
at start and checked like any other copy. The built-in numbers stay the
floor: play never waits on a table.

**Fetching.** Over plain HTTP from the console list's host
(`http://warthog.milenko.org/v1/delta/legacy` and `.sig`), and only because
the table is signed: a changed copy fails the signature and is dropped. The
GitHub fallback (`raw.githubusercontent.com`) is HTTPS only, so the console
cannot use it; Delta Peer's relay (the newer side sends its signed table in
the handshake) covers a console that can't reach the site.

**What it means on the console:** the table can widen the network
versions Warthog announces and joins without a new console build, the
thing that took a rebase this time.

## Delta List

The console's game list is plain HTTP today: its parsers are bounded and
fuzzed, but anyone on the path can change the list. Proposal: **a signed
list.**

- The site signs each list it serves the console with a key of the Delta
  family: a list key of its own, separate from the legacy table's (so a
  leaked site key cannot sign a legacy table). Its public half is built in,
  in `delta_key.h` beside the table's, with room for rotation.
- The response is the list as today, plus a line at the end:
  `sig <128 hex digits>`, the Ed25519 signature over every byte before it,
  and a `serial` and `issued` time in the header line, so an old list can't
  be replayed for long (the console drops a list older than, say, ten
  minutes by its own clock, or by the serial when the clock is unset).
- The console verifies before parsing anything but the size: no TLS
  needed. An unsigned or failing list is shown as "the list couldn't be
  checked" rather than used.
- New fields stay at the end of a line, as the console list's format
  promises: the Delta flag, the network version and accepted range, a
  co-op mark.

This also hardens the invite join: an invite from a signed list names its
host's key hash, which the tunnel checks.

## Delta Stats

What the console can report, as the PCs do for games they join: the
scoreboard as the console saw it (names, kills, deaths, scores, medals,
weapons) at game end, with its player ID.

- Transport: the PCs post reports over HTTPS. The console has no TLS, so
  reports would need a plain-HTTP endpoint, and a report is not public
  data: it should be signed by the console's player key (Ed25519, which it
  can do) so the site can tie it to the player without TLS. Its content is
  no secret (it is the scoreboard), so integrity, not confidentiality, is
  what is needed.
- Never IP addresses; the player ID as the PCs send it.
- The host's report (the authoritative one) is a PC's or a [D] server's in
  almost every game the console joins, so this is an addition, not a gap.

## Delta Link

Profiles and linking on a console:

- The player ID: the console needs a stable per-console identity. A random
  secret made on first start and kept on the hard disk (`E:\` or the
  game's save area), never the Xbox's serial or MAC.
- Link Profile: the PCs show a code and a QR code; the console can show the
  code the same way (the QR code too: `qrcodegen` is plain C), and the
  player enters it on halo.milenko.org/connect from a phone. The site's
  link calls are HTTPS today; the console needs plain-HTTP versions of the
  two it uses (start a link, poll it), with the response signed by the site
  and the console's request signed by its player key.
- An encrypted backup of the identity, as the site keeps for PC accounts,
  is optional on the console.

## Plan

| Step | Size | Depends on |
|---|---|---|
| 1. The memory class encoding (log2) and the `coop` policy column in `delta.h` | small | ChupathingyCE, before Delta Peer's first release |
| 2. Delta Peer on the console: `delta_wire.c` and `delta_peer.c` built, a socket on 5160 through the hooks, the platform key from `xbox_memory.c`, the policy's join check | medium | Delta Peer merged in ChupathingyCE, then a rebase |
| 3. The signed legacy table: verify, `D:\` cache, plain-HTTP fetch, relay over Delta Peer | medium | Delta's legacy table in ChupathingyCE; the site serving the two files over plain HTTP on the console host |
| 4. The signed console list | medium | a list key in `delta_key.h`; the site signing the console list |
| 5. Link Profile on the console | medium | a per-console identity; plain-HTTP, signed link calls on the site |
| 6. Stats from the console | medium | step 5 (the player key); a signed plain-HTTP report endpoint |
| 7. Chat, server messages, votes (other capabilities) | large | Delta Peer's capabilities built on the PCs first |

What must land in ChupathingyCE first: Delta Peer and the legacy table
(merged from their branches), the `delta.h` changes of step 1, and, on the
site (halo.milenko.org), the plain-HTTP console endpoints with signed responses. The
console's side then needs only `port/xbox` code and a rebase.
