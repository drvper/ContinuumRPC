# <p align="center"><b>Continuum Rich Presence (cRPC)</b> [![CodeFactor](https://www.codefactor.io/repository/github/purge-dev/clinicord/badge)](https://www.codefactor.io/repository/github/purge-dev/clinicord) [![GitHub stars](https://img.shields.io/github/stars/purge-dev/ContinuumRPC)](https://github.com/purge-dev/ContinuumRPC/stargazers) [![GitHub issues](https://img.shields.io/github/issues/purge-dev/ContinuumRPC)](https://github.com/purge-dev/ContinuumRPC/issues) </p>

<p align="center"><img src="https://github.com/purge-dev/ContinuumRPC/blob/master/web_assets/rpc.png"></p>

## What is it?
Continuum RPC (cRPC) is a plugin used alongside the normal Continuum game executable that provides a Discord rich presence status when using the Continuum client.

## In which game is this used?
<p align="center"><a href="https://store.steampowered.com/app/352700/Subspace_Continuum/"><img src="https://shared.cloudflare.steamstatic.com/store_item_assets/steam/apps/352700/header.jpg"</img></a></p>

## How do I use it?
### New/Steam Users
* Simply download the game on [Steam](https://store.steampowered.com/app/352700/Subspace_Continuum) and enable the **Beta** release.
### Legacy Users
* Download the latest official release from [here](https://github.com/purge-dev/ContinuumRPC/releases).
* Unzip the contents into your main Continuum folder, overwriting your Continuum.exe (you can make a backup if you wish).
* Run the game as usual. Any existing shortcuts/launch options you made will still work, and all game platforms on **Windows** are supported. This may also work in **MacOS** and **Linux** using [Wine](https://www.winehq.org/) (untested).

## Features
cRPC reads the game state directly from the Continuum client's memory (read-only, nothing is ever written to the game), so your status is accurate and updates within about a second:

| In game | Shown on Discord |
| --- | --- |
| Zone list | *Selecting Zone* with your skin, or *Idle* when the launcher is in the background |
| Playing | Zone name, your ship (icon and name), frequency, bounty, flags carried, and whether you are in a safety zone |
| Team size | Discord's party counter, e.g. *(9 of 24)*: teammates on your frequency out of everyone in the arena |
| Arena | Map name and population (e.g. *Map: trench.lvl · 24 players (18 playing)*) in the large image tooltip |
| Spectating | *Spectating* |
| Tabbed out | *Chatting* in the windowed chat, or *Idle* while you are away, without resetting your timer |
| Elapsed time | Time spent in the current zone (optionally restarts when you switch ships) |

Other details:
* Presence clears as soon as the game is closed, and cRPC reconnects automatically if Discord is started or restarted after the game.
* Discord's "Play" button launches the game (through Steam for Steam installs).
* If memory reading is unavailable (or disabled), cRPC falls back to window titles and the last ship saved in the registry.

## Configuration
A `cRPC.ini` file is created next to `Continuum.exe` on first run. Every feature can be toggled there, for example:

```ini
[Presence]
ShowPlayerName=0   ; your player name is hidden by default
ShowZone=1         ; 0 shows "In a zone" instead of the zone name
ShowPartySize=1    ; the "(x of y)" team counter
ResetTimerOnShipChange=0
```

Set `Debug=1` under `[General]` to write a `cRPC.log` with everything cRPC reads from the game. This is the first thing to check if your status looks wrong.

### Memory offsets
The memory layout of Continuum 0.40 comes from [Marvin](https://github.com/plushmonkey/Marvin) (`platform/ContinuumGameProxy.cpp`). All offsets live in `Config.h` and can be overridden under `[Memory]` in `cRPC.ini` (e.g. `Ship=0x5C`), so a client update can be handled without recompiling.

| Value | Location |
| --- | --- |
| Game struct | `[exe + 0xC1AFC]` (null outside of a zone) |
| Player data | `game + 0x127EC` |
| In a zone | `[playerData + 0x6C4]` (map) is non-null |
| Map name | `[playerData + 0x6C4] + 1` |
| Zone name | `[playerData + 0x5A3C] + 0x10D` (server folder, `zones\<zone>`) |
| Player list / count | `playerData + 0x884` / `playerData + 0x1884` |
| Player | id `0x18`, bounty `0x20 + 0x24`, flags `0x30 + 0x34`, freq `0x58`, ship `0x5C`, status `0x60`, name `0x6D` |
| Your name | `menu040.dll`: `[menu + 0x47A38] + 0x15 + [menu + 0x47FA0] * 2860` |

## Building
Open `DiscordBridge.sln` in Visual Studio 2019+ and build **Release | Win32** (Continuum is a 32-bit game). The output is `Continuum.exe`, with `discord_game_sdk.dll` copied next to it.

### Tests
* Presence logic (any platform):
  ```bash
  g++ -std=c++17 -I. tests/presence_tests.cpp Presence.cpp -o presence_tests && ./presence_tests
  ```
* End-to-end memory reading against a fake Continuum client (Linux with MinGW and Wine):
  ```bash
  tests/integration/run.sh
  ```
