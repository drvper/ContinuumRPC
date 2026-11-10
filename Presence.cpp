// Presence.cpp - builds the Discord rich presence from a game snapshot.
// Platform independent; see Presence.h.
#include "Presence.h"

#include <vector>

namespace crpc {

namespace {

// Asset keys uploaded to the Discord application (611578814073405465). New
// art cannot be added from code, so every state below reuses these keys.
constexpr const char* kLargeImage = "large-ss";
constexpr const char* kImageSpectate = "idle";
constexpr const char* kImageIdle = "idle2";
constexpr const char* kImageChat = "chat";
constexpr const char* kImageLauncher = "launcher";

std::string Join(const std::vector<std::string>& parts, const char* sep = " \xC2\xB7 ") // " · "
{
	std::string out;
	for (const auto& p : parts)
	{
		if (p.empty())
			continue;
		if (!out.empty())
			out += sep;
		out += p;
	}
	return out;
}

bool IsShip(int ship) { return ship >= kWarbird && ship <= kShark; }

} // namespace

bool PresenceModel::operator==(const PresenceModel& o) const
{
	return details == o.details && state == o.state && largeImage == o.largeImage &&
		largeText == o.largeText && smallImage == o.smallImage && smallText == o.smallText &&
		partyId == o.partyId && partySize == o.partySize && partyMax == o.partyMax &&
		startTimestamp == o.startTimestamp && instance == o.instance &&
		statusDisplay == o.statusDisplay && buttons == o.buttons;
}

const char* ShipName(int ship)
{
	static const char* const kNames[] = {
		"Warbird", "Javelin", "Spider", "Leviathan", "Terrier", "Weasel", "Lancaster", "Shark", "Spectator"
	};
	if (ship < kWarbird || ship > kSpectator)
		return "";
	return kNames[ship];
}

const char* ShipImageKey(int ship)
{
	static const char* const kKeys[] = {
		"playing", "jav", "spid", "lev", "terr", "weas", "lanc", "sha", kImageSpectate
	};
	if (ship < kWarbird || ship > kSpectator)
		return "";
	return kKeys[ship];
}

std::string TruncateUtf8(const std::string& s, std::size_t maxBytes)
{
	if (s.size() <= maxBytes)
		return s;
	std::size_t cut = maxBytes;
	// back up over continuation bytes (10xxxxxx) so we never split a code point
	while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
		--cut;
	return s.substr(0, cut);
}

std::size_t Utf8Length(const std::string& s)
{
	std::size_t n = 0;
	for (unsigned char c : s)
		if ((c & 0xC0) != 0x80)
			++n;
	return n;
}

bool IsValidButton(const PresenceButton& b)
{
	std::size_t label = Utf8Length(b.label);
	bool http = b.url.compare(0, 7, "http://") == 0 || b.url.compare(0, 8, "https://") == 0;
	return label >= 1 && label <= 32 && http && b.url.size() <= 512;
}

std::int64_t PresenceBuilder::PhaseStart(const std::string& key, std::int64_t now)
{
	if (key != phaseKey_)
	{
		phaseKey_ = key;
		phaseStart_ = now;
	}
	return phaseStart_;
}

PresenceModel PresenceBuilder::Build(const GameSnapshot& snap, const PresenceSettings& cfg, std::int64_t now)
{
	PresenceModel m;
	m.largeImage = kLargeImage;
	m.largeText = "Subspace Continuum";

	const std::string zone = cfg.showZone ? (snap.zone.empty() ? "In a zone" : snap.zone) : "In a zone";
	const bool known = snap.memoryValid;

	if (snap.screen == Screen::Launcher)
	{
		m.details = "Selecting Zone";
		if (cfg.showPlayerName && !snap.playerName.empty())
			m.largeText = "Subspace Continuum \xC2\xB7 Profile: " + snap.playerName;

		if (snap.focused)
		{
			m.state = snap.skin.empty() ? "" : "Skin: " + snap.skin;
			m.smallImage = kImageLauncher;
			m.smallText = "Game Launcher";
		}
		else
		{
			m.state = "Idle";
			m.smallImage = kImageIdle;
			m.smallText = "Idle";
		}
		std::int64_t start = PhaseStart("launcher", now);
		m.startTimestamp = cfg.showLauncherTimer ? start : 0;
	}
	else
	{
		m.instance = true;
		m.details = zone;

		// Large image tooltip: player name, map and population
		std::vector<std::string> info;
		if (cfg.showPlayerName && known && !snap.playerName.empty())
			info.push_back(snap.playerName);
		if (cfg.showMap && known && !snap.mapFile.empty())
			info.push_back("Map: " + snap.mapFile);
		if (cfg.showArenaPopulation && known && snap.arenaPlayers > 0)
		{
			std::string pop = std::to_string(snap.arenaPlayers) + (snap.arenaPlayers == 1 ? " player" : " players");
			if (snap.arenaPlaying != snap.arenaPlayers)
				pop += " (" + std::to_string(snap.arenaPlaying) + " playing)";
			info.push_back(pop);
		}
		if (!info.empty())
			m.largeText = Join(info);

		const int ship = snap.ship;
		const std::string shipName = ShipName(ship);

		if (snap.screen == Screen::Chat)
		{
			m.state = "Chatting";
			m.smallImage = kImageChat;
			m.smallText = IsShip(ship) ? "Chatting \xC2\xB7 " + shipName : "Chatting in Game";
		}
		else if (!snap.focused)
		{
			m.state = "Idle";
			m.smallImage = kImageIdle;
			m.smallText = IsShip(ship) ? "Away \xC2\xB7 " + shipName : "Waiting for Game";
		}
		else if (ship == kSpectator)
		{
			m.state = "Spectating";
			m.smallImage = kImageSpectate;
			m.smallText = "Spectator Mode";
		}
		else if (IsShip(ship))
		{
			std::vector<std::string> parts;
			parts.push_back(shipName);
			if (known && cfg.showFrequency && snap.freq >= 0)
				parts.push_back("Freq " + std::to_string(snap.freq));
			if (known && cfg.showBounty && snap.bounty > 0)
				parts.push_back("Bounty " + std::to_string(snap.bounty));
			if (known && cfg.showFlags && snap.flags > 0)
				parts.push_back(std::to_string(snap.flags) + (snap.flags == 1 ? " flag" : " flags"));
			if (known && cfg.showSafeZone && snap.inSafeZone)
				parts.push_back("In Safe");
			m.state = Join(parts);

			m.smallImage = ShipImageKey(ship);
			if (cfg.showPlayerName && known && !snap.playerName.empty())
				m.smallText = snap.playerName + " in a " + shipName;
			else
				m.smallText = "Playing as " + shipName;
		}
		else
		{
			// in a zone but the ship could not be determined
			m.state = "Playing";
			m.smallImage = "";
			m.smallText = "";
		}

		// "(x of y)": teammates on our frequency out of everyone in the arena
		if (cfg.showPartySize && known && IsShip(ship) && snap.teamPlayers > 0 && snap.arenaPlayers >= snap.teamPlayers)
		{
			m.partyId = "crpc:" + snap.zone + ":" + snap.mapFile + ":" + std::to_string(snap.freq);
			m.partySize = snap.teamPlayers;
			m.partyMax = snap.arenaPlayers;
		}

		std::string phase = "zone:" + snap.zone;
		if (cfg.resetTimerOnShipChange)
			phase += ":" + std::to_string(ship);
		m.startTimestamp = PhaseStart(phase, now);
	}

	m.statusDisplay = cfg.statusDisplay;
	for (const PresenceButton& b : cfg.buttons)
		if (m.buttons.size() < 2 && IsValidButton(b))
			m.buttons.push_back(b);

	m.details = TruncateUtf8(m.details);
	m.state = TruncateUtf8(m.state);
	m.largeText = TruncateUtf8(m.largeText);
	m.smallText = TruncateUtf8(m.smallText);
	m.partyId = TruncateUtf8(m.partyId);
	return m;
}

} // namespace crpc
