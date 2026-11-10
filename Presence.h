// Presence.h - platform independent model of the game state and the Discord
// rich presence that is generated from it.
//
// Nothing in this file depends on Windows or on the Discord SDK so that the
// presence logic can be unit tested on any platform (see tests/).
#ifndef CRPC_PRESENCE_H
#define CRPC_PRESENCE_H

#include <cstdint>
#include <string>

namespace crpc {

// Ship numbers as used by the Continuum client (and the registry "Ship" value)
enum Ship : int {
	kShipUnknown = -1,
	kWarbird = 0,
	kJavelin = 1,
	kSpider = 2,
	kLeviathan = 3,
	kTerrier = 4,
	kWeasel = 5,
	kLancaster = 6,
	kShark = 7,
	kSpectator = 8,
};

// Which part of the client the player is looking at.
enum class Screen {
	Launcher,   // zone list / profile menu (menu040.dll)
	InZone,     // the actual game window
	Chat,       // the windowed "chat" mode used while tabbed out of a zone
};

// Everything we know about the game at one point in time.
struct GameSnapshot {
	Screen screen = Screen::Launcher;
	bool focused = false;       // a window of the game process is in the foreground

	// Registry backed values (always available)
	std::string zone;           // zone name (memory server folder, else registry)
	std::string skin;           // launcher skin

	// Memory backed values (only meaningful when memoryValid is true)
	bool memoryValid = false;   // the local player was located in the game's memory
	std::string playerName;
	std::string mapFile;        // e.g. "trench.lvl", empty when unknown
	int ship = kShipUnknown;
	int freq = -1;
	int bounty = -1;            // -1 = unknown
	int flags = -1;             // flags carried, -1 = unknown
	bool inSafeZone = false;    // sitting in a safety zone
	int arenaPlayers = 0;       // everyone in the arena including spectators
	int arenaPlaying = 0;       // players that are in a ship
	int teamPlayers = 0;        // players on our frequency (including us)
};

// User facing options, loaded from cRPC.ini
struct PresenceSettings {
	bool showZone = true;           // false hides the zone name ("In a zone")
	bool showPlayerName = false;    // privacy: off by default
	bool showMap = true;
	bool showFrequency = true;
	bool showBounty = true;
	bool showFlags = true;
	bool showSafeZone = true;
	bool showArenaPopulation = true;
	bool showPartySize = true;      // Discord's "(x of y)" next to the state line
	bool resetTimerOnShipChange = false;
	bool showLauncherTimer = false;
};

// Discord's limit for most activity strings is 128 bytes including the NUL.
constexpr std::size_t kMaxActivityString = 127;

// The rich presence we want Discord to display.
struct PresenceModel {
	std::string details;
	std::string state;
	std::string largeImage;
	std::string largeText;
	std::string smallImage;
	std::string smallText;
	std::string partyId;
	int partySize = 0;
	int partyMax = 0;
	std::int64_t startTimestamp = 0;   // unix seconds, 0 = no timer
	bool instance = false;             // true while actually inside a zone

	bool operator==(const PresenceModel& o) const;
	bool operator!=(const PresenceModel& o) const { return !(*this == o); }
};

// Turns snapshots into presence models and keeps track of the elapsed timer.
class PresenceBuilder {
public:
	PresenceModel Build(const GameSnapshot& snap, const PresenceSettings& settings, std::int64_t now);

private:
	std::string phaseKey_;
	std::int64_t phaseStart_ = 0;

	std::int64_t PhaseStart(const std::string& key, std::int64_t now);
};

// Helpers (exposed for tests)
const char* ShipName(int ship);
const char* ShipImageKey(int ship);
// Truncates to at most maxBytes without splitting a UTF-8 code point.
std::string TruncateUtf8(const std::string& s, std::size_t maxBytes = kMaxActivityString);

} // namespace crpc

#endif // CRPC_PRESENCE_H
