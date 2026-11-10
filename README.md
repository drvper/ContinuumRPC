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
* Rename your original `Continuum.exe` to `Continuum_main.exe`, then unzip `Continuum.exe` and `cRPC.ini` into your main Continuum folder. If you are upgrading from an older cRPC, just overwrite `Continuum.exe` (and delete the no longer needed `discord_game_sdk.dll`). See `INSTALL.txt` in the zip for details.
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
* cRPC talks to the Discord app directly over its local RPC pipe (`\\.\pipe\discord-ipc-N`), so no Discord SDK DLL is needed.
* Presence clears as soon as the game is closed, and cRPC reconnects automatically if Discord is started or restarted after the game.
* Up to two link buttons on your profile for other people to click (by default *Play Subspace Continuum*, linking to the Steam page).
* Optionally show your ship or the zone instead of the game name in server member lists (`StatusDisplay`).
* If memory reading is unavailable (or disabled), cRPC falls back to window titles and the last ship saved in the registry.

## Configuration
A `cRPC.ini` file is created next to `Continuum.exe` on first run. Every feature can be toggled there, for example:

```ini
[Presence]
ShowPlayerName=0   ; your player name is hidden by default
ShowZone=1         ; 0 shows "In a zone" instead of the zone name
ShowPartySize=1    ; the "(x of y)" team counter
ResetTimerOnShipChange=0
StatusDisplay=0    ; 1 = show your ship, 2 = show the zone in member lists

[Buttons]
Button1Label=Play Subspace Continuum
Button1Url=https://store.steampowered.com/app/352700/Subspace_Continuum/
; e.g. your zone's website or Discord server
Button2Label=
Button2Url=
```

Set `Debug=1` under `[General]` to write a `cRPC.log` with everything cRPC reads from the game and exchanges with Discord. This is the first thing to check if your status looks wrong. If cRPC ever crashes, it writes `cRPC-crash.log` with the location of the crash.

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
Open `DiscordBridge.sln` in Visual Studio 2019 or newer and build **Release | x86** (Continuum is a 32-bit game). The output is a single self-contained `Release\Continuum.exe` (the C runtime is linked statically), and the default `cRPC.ini` is copied next to it. There are no third-party dependencies.

Every push is built by GitHub Actions (`.github/workflows/build.yml`), which uploads a ready-to-ship `cRPC` artifact (`Continuum.exe`, `cRPC.ini`, `INSTALL.txt`, `LICENSE`).

### Tests
* Presence logic and the Discord protocol (any platform):
  ```bash
  g++ -std=c++17 -I. tests/presence_tests.cpp Presence.cpp -o presence_tests && ./presence_tests
  g++ -std=c++17 -I. tests/rpc_tests.cpp DiscordRpc.cpp Presence.cpp -o rpc_tests && ./rpc_tests
  ```
* End-to-end test against a fake Continuum client and a fake Discord client (Linux with MinGW, Wine and Xvfb):
  ```bash
  tests/integration/run.sh
  ```

## Project layout
| File | Purpose |
| --- | --- |
| `DiscordBridge.cpp` | Entry point: launches the game and runs the update loop |
| `Continuum.cpp` | Finds the game process, its windows and registry settings |
| `GameMemory.cpp` | Reads the game state from the client's memory |
| `Presence.cpp` | Turns the game state into the text and images shown on Discord |
| `DiscordRpc.cpp` / `Activity.cpp` | Discord's local RPC protocol and the pipe connection |
| `Config.cpp` | Loads `cRPC.ini` |
