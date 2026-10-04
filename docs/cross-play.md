# Cross-play: the console and the PC builds on one network

Warthog is to play system link with the PC builds (network version 11) on
one LAN, then internet games. The PCs' protocol is fixed: the console
matches them, never the other way round. This is what stands between them,
field by field, and the plan. (File and line references are this
repository's, October 4.)

## What already matches

- **Versions.** `HALO_PORT_NETWORK_GAME_MESSAGE_VERSION` (2) and
  `HALO_PORT_NETWORK_VERSION` (11, minimum and maximum 11) are the same in
  both builds. So the console finds the PCs' games and tries to join them,
  and they find the console's. Version gating doesn't keep them apart: the
  layout checks below do.
- **Message and packet limits.** `HALO_PORT_MAXIMUM_NETWORK_MESSAGE_SIZE`
  (0x1000), `HALO_PORT_NETWORK_PACKET_SIZE` (0x1100),
  `HALO_PORT_NETWORK_GAME_SETTINGS_FRAGMENT_SIZE` (0xE00), the datagram
  size (1,200) and the message header's 0xFFF are outside
  `HALO_XBOX_CONSOLE`, so every buffer they size is the same.
- **Array fields.** The packet codec writes an array's count in one byte
  for any maximum up to 255 (`data_encoding.c:158`), so 16 and 128 give
  the same bytes; only the accepted range differs.
- **The per-tick update** (`message_server_game_update`, sized by the
  player limit) is sent with a count of 0 by the distributed netcode's
  hosts (`network_server_manager.c:2342-2375`): the same bytes in both.
- **Join and lobby messages** (join request, add/remove/switch player,
  settings request) and **the advertisement** (0x114 bytes) are fixed
  size. Only the indices in them depend on the limits.
- **Objects' names on the wire.** An object goes as its full 32-bit datum
  index (salt and slot), never bit-packed by the array's size
  (`network_objects.c:246`, `network_distributed.c:290-292`,
  `network_damage.c:235-278`). The layout is the same at 2,048 and 8,192;
  only the values differ.

## What doesn't

### 1. The game settings record (`struct network_game`)

One declaration (`network_game_manager.h:45-68`), checked against
`halo_port_limits.h` in seven units. Its machine and player arrays are
**slot-indexed**: `machines[i]` is the machine in connection slot `i`
(`machine_index` `i`, else NONE), `players[i]` the player in slot `i`.

| | console (16/16) | PCs (128/128) |
|---|---|---|
| `player_count` | 0x554 | 0x2314 |
| `players` | 0x556 | 0x2316 |
| `random_seed` | 0x758 | 0x3318 |
| PC options | 0x760 | 0x3320 |
| size | 0x780 (1,920) | 0x3340 (13,120) |

It goes in 0xE00-byte pieces (`message_server_game_settings_update`):
four from a PC, one from the console. A client refuses any piece whose
`total_size` isn't its own `sizeof` ("a different game layout",
`network_client_message_handler.c:925-967`), so each side refuses the
other's game cleanly.

### 2. Player and machine slots

- **Machines.** A host numbers a machine by its connection slot
  (`network_server_manager.c:4029`), and pending connections, game list
  probes and its own loopback take slots too, so a PC host's fifth machine
  can be slot 16 or higher. A console refuses that cleanly ("bad
  machine_index", `network_client_manager.c:1821-1863`).
- **Players.** A player's slot (`player_list_index`) **is its player datum's
  index on every machine** (`network_game_manager.c:110-122`). The
  distributed netcode names players by it, one byte each
  (`distributed_player_to_byte`, `network_distributed.c:1075`). A PC host
  gives the first free slot not held by a player who quit mid-game
  (`:362-397`), so a game of 16 or fewer can still use slots past 16. The
  console drops any such byte (bounds checks at `network_distributed.c:1069`,
  `:1691`, `:2251`, `network_damage.c:1644`, `player_queues_new.c:1170`) and
  refuses the player (`network_game_add_player`): that player silently
  doesn't exist there.
- **Teams.** In free for all a player's team is his slot
  (`game_allegiance.c:205`), and the score arrays are indexed by it.

### 3. The game state message

`_distributed_message_game_state` (`network_distributed.c:3027-3060`,
`game_engine.c:9104-9160`) copies each game type's globals, which hold
arrays of one entry a player slot, and each reader wants its exact size:

| game type | console | PCs |
|---|---|---|
| Slayer | 176 | 1,408 |
| King | 428 | 1,436 |
| Oddball | 212 | 1,108 |
| Race | 352 | 2,704 |
| CTF | 0x38 | 0x38 |

So the two never take each other's scores or the end of a game.

### 4. Object slots

Allocation is kept in step by forcing the host's index: the host takes the
first free slot from 0 (`data.c:296-325`); a client makes the object at
exactly that datum, salt included (`network_objects.c:1880-1955`,
`objects.c:3288`, `data.c:264-293`), and looks remote indices up directly
(`objects_client_has[absolute] == object_index`, `:579`, `:1975`). A
client's own objects (projectiles, effects) take the upper half
(`LOCAL_OBJECTS_FIRST_INDEX`, `:130`: 1,024 on the console, 4,096 on the
PCs) and **never go on the wire**: clients only ever name the host's
objects (hit reports, vehicle predictions, units in player predictions).

On a console, a PC host object at 2,048 or above is refused
(`network_objects.c:1888`), counted as a failure, and the console asks for
the whole object list again every 11 to 60 seconds; the object, and
anything naming it, never exists there (an invisible player, missing
weapons). One at 1,024 or above evicts the console's own projectiles and
effects. A PC host with 16 players or fewer on the Xbox maps allocates low
and mostly stays under 2,048, but nothing guarantees it; above 16 players
its garbage limits scale up (`objects.c:3843-3849`).

Map placement must also come out the same everywhere (placed objects sit
at the same datum on every machine, `network_objects.c:1903-1915`): the
console's object pool (1 MB against the PCs' 8 MB) must not run out during
placement. The Xbox maps fit, as they shipped.

## The two ways, and the choice

**(a) The console runs with the PCs' limits.** 128 player and machine
slots and 8,192 object slots, with 16 kept as the game's own player
maximum. Everything above then matches byte for byte with no translation.
The cost is the game state, which is full (3,165,260 of its 3,166,208
bytes) and can't grow in place: the tag cache, to which the maps are
linked, starts where it ends.

- 128 player slots: the players table is game state, 128 × 0xD4 bytes,
  about 24 KB more. The rest that scales with players is outside the game
  state: the settings record copies (about 11 KB more each), the game types'
  globals, the netcode's arrays.
- 8,192 object slots: the object header array, its pool and the cluster
  references grow by more than the console has. vsod99's memory probe
  found the route on a 128 MB console (a devkit, or a modified retail
  one): the game state as ordinary virtual memory at 0x40000000, with the
  tag, texture and sound caches kept in the low 64 MB.

**(b) The console speaks the PCs' wire format with its own limits.** A
translation layer, valid only where no peer computes anything different.

- The settings record: folding to 16 slots and widening to 128 is exact
  while every used slot is under 16, a byte shuffle (built, below).
- The game state message: re-laid per game type, exact under the same
  condition.
- Objects: the netcode is host authoritative, and no peer computes
  anything from another machine's local index. So a console can keep a
  remote-to-local table (8,192 entries, 32 KB; local-to-remote, 8 KB) and
  translate at the decode and encode points: object creation and deletion,
  object states, inventories, unit states (unit and vehicle), damage
  events and hit reports, vehicle and player predictions, and the CTF and
  oddball state. The PCs see the same bytes. What it doesn't change is
  capacity: 2,048 objects and the 1 MB pool.
- Player slots: the slot is the player datum's index on every machine and
  is in nearly every distributed message, so a translation means rewriting
  every variable-length entry both ways, and FFA teams and score arrays
  with it. It breaks the "never change what peers compute" rule the moment
  a mapping is missed.

**The choice:**

1. **Players and machines: (a).** The console's index space becomes 128 and
   128, its game's maximum stays 16. That's about 24 KB of game state, and
   it removes every translation for the settings record, the game state
   message and every player byte. The 24 KB comes from the 128 MB layout
   first (devkits have 128 MB). For a stock 64 MB console, the
   room has to be found in the game state's own pools by measurement, not
   by guessing. Until then, (b)'s settings fold lets a console join a PC
   game whose used slots are all under 16.
2. **Objects: (b).** The translation table, console only, outside the game
   state (40 KB of ordinary memory). It keeps the PCs' bytes and the
   console's Xbox object capacity. A console in a game means 16 players at
   most. The console must not evict its own objects for a forced create;
   it collects them first, then refuses.
3. **Hosting.** A console host widens its record to the PCs' 13,120 bytes
   and sends it in their four pieces, and allocates host objects below
   2,048 (its own limit), which every PC can take.

## What is built (this pass)

- `port/xbox/src/xbox_network_game_layout.c`: the settings record between
  any two slot counts, byte for byte. A fold refuses any used machine or
  player slot, or index a used entry names, past the narrower layout (no
  guess), and holds the gametype's maximum to its slots. A widen fills the
  new slots as the game empties one. Tested on the host
  (`port/xbox/tests/network_game_layout_test.c`: the sizes the game
  asserts, an exact round trip, every refusal). The game doesn't call it
  yet: joining a PC game also needs the game state message and players
  past slot 16, so a half-translated join would only fail later and less
  clearly.

## Next, in order

1. Measure the console's game state by pool (the game logs its
   allocations) and the 128 MB layout's room; then 128 player and machine
   slots on the console, its game's maximum 16.
2. The object translation table, at the decode and encode points listed
   above, with a counter of refused creates in `debug.txt`.
3. Two machines on one LAN: a PC build hosting Blood Gulch, the devkit
   joining over system link (`bypass_security.txt` on), then the reverse.
4. Internet joins through the PCs' p2p tunnel under
   `transport_endpoint_winsock.c` (the README's "Online Games on the Xbox").
