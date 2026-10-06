# Contributing to Warthog

Warthog is [ChupathingyCE](https://github.com/ChupathingyCE/chupathingyce)'s
console line: the original Xbox today, other consoles later
(docs/consoles.md). Fixes to the shared game code belong in ChupathingyCE
first; Warthog takes its main regularly (README, "Keeping in step with
ChupathingyCE").

## How changes flow

Code moves one way: ChupathingyCE -> Warthog. Warthog is rebased onto
ChupathingyCE's main, so a fix to the shared game, network or desktop code
goes to ChupathingyCE first and arrives here with the next rebase. Pull
requests here are for console-specific changes only: `port/<console>/`, the
console build tools, and shared-code changes behind the console's define
(`HALO_XBOX_CONSOLE`) that leave the desktop builds unchanged.

Never commit, attach or link SDK files (Microsoft's, Sony's, Nintendo's:
headers, libraries, tools, d3d8ntpr.lib, xnet) or game data (maps, sounds,
movies, ui.map, disc images, keys). The SDK and the game are always the
user's own.

## Pull requests

- **Open them against `ChupathingyCE/warthog`'s `main`.** With the GitHub
  CLI, run `gh repo set-default ChupathingyCE/warthog` once in your clone.
- One change per pull request, with how you tested it: a devkit, a modified
  retail console or xemu, and which build.
- Console code stays in `port/<console>/`, and changes to shared code stay
  behind the console's define (`HALO_XBOX_CONSOLE`), so the desktop builds
  are unchanged.
- The game protocol stays the PC builds': the console adapts to their
  network version and layout, never the other way (docs/cross-play.md).
- Nothing logs an IP address whole unless it is a LAN one: go through
  `log_address`.
- Commit messages, pull requests and issues are public, and they're posted
  to the ChupathingyCE Discord.

## Bugs

Use the bug report form, or #warthog-support on the
[Discord](https://discord.gg/4BUm2FwuCB). Attach `D:\debug.txt`; for a
join that fails, run once with an empty `D:\trace.txt` beside
`default.xbe` (it shows other hosts only as tags, never their Ethernet
addresses).

## SDK and game files

Warthog never provides SDK or game files. Don't commit, attach or link
Microsoft's (or any console maker's) SDK headers, libraries or tools, maps,
disc images or other copyrighted game data.
