// Config.h - settings loaded from cRPC.ini next to the executable.
#ifndef CRPC_CONFIG_H
#define CRPC_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

#include "Presence.h"

namespace crpc {

// Memory layout of the Continuum 0.40 client, taken from plushmonkey's Marvin
// bot (https://github.com/plushmonkey/Marvin, platform/ContinuumGameProxy.cpp).
// Every value can be overridden in the [Memory] section of cRPC.ini; setting an
// optional offset (bounty, flags, status, map, server folder) to 0 disables
// the feature that uses it.
struct MemoryOffsets {
	// Game executable (loaded at 0x400000)
	std::uint32_t gamePointer = 0xC1AFC;    // [exe + gamePointer] -> game struct (0 when not in a zone)
	std::uint32_t playerData = 0x127EC;     // game + playerData -> player data block
	std::uint32_t playerList = 0x884;       // player data + playerList -> Player*[]
	std::uint32_t playerCount = 0x1884;     // player data + playerCount -> u16 count
	std::uint32_t mapName = 0x6C4;          // [player data + mapName] + 1 -> char[16] (0 until the map is loaded)
	std::uint32_t serverFolder = 0x5A3C;    // [player data + serverFolder] + serverFolderName -> "zones\<zone>"
	std::uint32_t serverFolderName = 0x10D;

	// Player struct
	std::uint32_t playerId = 0x18;
	std::uint32_t playerBounty1 = 0x20;     // bounty = [b1] + [b2]
	std::uint32_t playerBounty2 = 0x24;
	std::uint32_t playerFlags1 = 0x30;      // flags carried = [f1] + [f2]
	std::uint32_t playerFlags2 = 0x34;
	std::uint32_t playerFreq = 0x58;
	std::uint32_t playerShip = 0x5C;
	std::uint32_t playerStatus = 0x60;      // stealth 0x01, cloak 0x02, xradar 0x04, ... safety 0x20
	std::uint32_t playerName = 0x6D;        // char[23]
	std::uint32_t safetyMask = 0x20;        // status bit for "in safety zone" (SubSpace protocol)

	// menu040.dll (profile selected in the launcher = our player name)
	std::uint32_t profileIndex = 0x47FA0;   // u16 index of the selected profile
	std::uint32_t profileArray = 0x47A38;   // [menu + profileArray] -> profile[]
	std::uint32_t profileName = 0x15;       // offset of the name in a profile
	std::uint32_t profileSize = 2860;       // sizeof(profile)
};

struct Config {
	// [General]
	std::int64_t clientId = 611578814073405465;
	std::wstring gameExecutable = L"Continuum_main.exe";
	bool launchGame = true;             // false: only attach to an already running client
	unsigned pollIntervalMs = 1000;     // how often the game state is sampled
	unsigned startupTimeoutSec = 60;    // give up if the game never shows up
	bool debugLog = false;              // write cRPC.log

	// [Memory]
	bool memoryReading = true;
	MemoryOffsets offsets;

	// [Presence] and [Buttons]
	PresenceSettings presence;

	// Problems found while loading (logged once the log file is open)
	std::vector<std::string> warnings;

	// Loads values from `path`, keeping the defaults above for anything missing.
	void Load(const std::wstring& path);
	// Writes a fully commented default ini to `path` if it does not exist.
	static void WriteDefault(const std::wstring& path);
};

// Directory of the running executable, with a trailing backslash.
std::wstring ExeDirectory();

} // namespace crpc

#endif // CRPC_CONFIG_H
