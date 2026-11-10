// Config.cpp - cRPC.ini loading.
#include "DiscordBridge.h"
#include "Config.h"

#include <cwchar>

namespace crpc {

namespace {

const wchar_t* const kDefaultIni =
	L"; Continuum Rich Presence (cRPC) settings\r\n"
	L"; Delete this file to restore the defaults.\r\n"
	L"\r\n"
	L"[General]\r\n"
	L"; Executable that cRPC starts (the original Continuum.exe, renamed)\r\n"
	L"GameExecutable=Continuum_main.exe\r\n"
	L"; 0 = do not start the game, just attach to a running Continuum client\r\n"
	L"LaunchGame=1\r\n"
	L"; How often the game state is sampled, in milliseconds\r\n"
	L"PollIntervalMs=1000\r\n"
	L"; Seconds to wait for the game to appear before exiting\r\n"
	L"StartupTimeoutSec=60\r\n"
	L"; 1 = write diagnostics (including memory reads) to cRPC.log\r\n"
	L"Debug=0\r\n"
	L"\r\n"
	L"[Memory]\r\n"
	L"; Read ship, frequency, bounty, etc. directly from the game's memory.\r\n"
	L"; 0 = fall back to the registry (last used ship only).\r\n"
	L"Enabled=1\r\n"
	L"; Offsets for Continuum 0.40 can be overridden here in hex (e.g. Ship=0x5C).\r\n"
	L"; Defaults come from the Marvin bot (github.com/plushmonkey/Marvin).\r\n"
	L"; Setting an optional offset (Bounty1, Flags1, Status, MapName, ServerFolder) to 0 disables it.\r\n"
	L"\r\n"
	L"[Presence]\r\n"
	L"; 0 = show \"In a zone\" instead of the zone name\r\n"
	L"ShowZone=1\r\n"
	L"; 1 = show your player name (off by default for privacy)\r\n"
	L"ShowPlayerName=0\r\n"
	L"ShowMap=1\r\n"
	L"ShowFrequency=1\r\n"
	L"ShowBounty=1\r\n"
	L"ShowFlags=1\r\n"
	L"ShowSafeZone=1\r\n"
	L"ShowArenaPopulation=1\r\n"
	L"; Show \"(teammates of players)\" next to your status\r\n"
	L"ShowPartySize=1\r\n"
	L"; 1 = restart the elapsed timer whenever you switch ships\r\n"
	L"ResetTimerOnShipChange=0\r\n"
	L"; 1 = show an elapsed timer while in the zone list\r\n"
	L"ShowLauncherTimer=0\r\n"
	L"; If the game's memory cannot be read: 1 = follow the ship-select keys\r\n"
	L"; (Esc then 1-8 or S, only while the game has focus) to show your ship\r\n"
	L"GuessShipFromKeys=1\r\n"
	L"; Status text shown in member lists: 0 = \"Playing Subspace Continuum\",\r\n"
	L"; 1 = your status line (e.g. \"Playing Warbird\"), 2 = the zone name\r\n"
	L"StatusDisplay=0\r\n"
	L"\r\n"
	L"[Buttons]\r\n"
	L"; Up to two link buttons shown to other people on your profile.\r\n"
	L"; Label: 1-32 characters, URL: must start with http:// or https://\r\n"
	L"; Leave a label empty to remove the button.\r\n"
	L"Button1Label=Play Subspace Continuum\r\n"
	L"Button1Url=https://store.steampowered.com/app/352700/Subspace_Continuum/\r\n"
	L"Button2Label=\r\n"
	L"Button2Url=\r\n";

std::wstring ReadString(const std::wstring& path, const wchar_t* section, const wchar_t* key, const std::wstring& def)
{
	wchar_t buf[1024];
	GetPrivateProfileStringW(section, key, def.c_str(), buf, _countof(buf), path.c_str());
	return buf;
}

// Accepts decimal or 0x-prefixed hex.
std::uint64_t ReadNumber(const std::wstring& path, const wchar_t* section, const wchar_t* key, std::uint64_t def)
{
	std::wstring s = ReadString(path, section, key, L"");
	if (s.empty())
		return def;
	wchar_t* end = nullptr;
	unsigned long long v = wcstoull(s.c_str(), &end, 0);
	if (end == s.c_str())
		return def;
	return v;
}

bool ReadBool(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool def)
{
	return ReadNumber(path, section, key, def ? 1 : 0) != 0;
}

void ReadOffset(const std::wstring& path, const wchar_t* key, std::uint32_t& value)
{
	value = static_cast<std::uint32_t>(ReadNumber(path, L"Memory", key, value));
}

} // namespace

std::wstring ExeDirectory()
{
	wchar_t path[MAX_PATH];
	DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
	std::wstring dir(path, len);
	size_t slash = dir.find_last_of(L"\\/");
	return slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
}

void Config::WriteDefault(const std::wstring& path)
{
	HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE)
		return; // already exists (or not writable)

	std::string utf8 = WideToUtf8(kDefaultIni);
	DWORD written = 0;
	WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
	CloseHandle(f);
}

void Config::Load(const std::wstring& path)
{
	const wchar_t* g = L"General";
	clientId = static_cast<std::int64_t>(ReadNumber(path, g, L"ClientId", static_cast<std::uint64_t>(clientId)));
	gameExecutable = ReadString(path, g, L"GameExecutable", gameExecutable);
	launchGame = ReadBool(path, g, L"LaunchGame", launchGame);
	pollIntervalMs = static_cast<unsigned>(ReadNumber(path, g, L"PollIntervalMs", pollIntervalMs));
	startupTimeoutSec = static_cast<unsigned>(ReadNumber(path, g, L"StartupTimeoutSec", startupTimeoutSec));
	debugLog = ReadBool(path, g, L"Debug", debugLog);

	if (pollIntervalMs < 250)
		pollIntervalMs = 250;

	memoryReading = ReadBool(path, L"Memory", L"Enabled", memoryReading);
	ReadOffset(path, L"GamePointer", offsets.gamePointer);
	ReadOffset(path, L"PlayerData", offsets.playerData);
	ReadOffset(path, L"PlayerList", offsets.playerList);
	ReadOffset(path, L"PlayerCount", offsets.playerCount);
	ReadOffset(path, L"MapName", offsets.mapName);
	ReadOffset(path, L"ServerFolder", offsets.serverFolder);
	ReadOffset(path, L"ServerFolderName", offsets.serverFolderName);
	ReadOffset(path, L"Id", offsets.playerId);
	ReadOffset(path, L"Bounty1", offsets.playerBounty1);
	ReadOffset(path, L"Bounty2", offsets.playerBounty2);
	ReadOffset(path, L"Flags1", offsets.playerFlags1);
	ReadOffset(path, L"Flags2", offsets.playerFlags2);
	ReadOffset(path, L"Freq", offsets.playerFreq);
	ReadOffset(path, L"Ship", offsets.playerShip);
	ReadOffset(path, L"Status", offsets.playerStatus);
	ReadOffset(path, L"Name", offsets.playerName);
	ReadOffset(path, L"SafetyMask", offsets.safetyMask);
	ReadOffset(path, L"ProfileIndex", offsets.profileIndex);
	ReadOffset(path, L"ProfileArray", offsets.profileArray);
	ReadOffset(path, L"ProfileName", offsets.profileName);
	ReadOffset(path, L"ProfileSize", offsets.profileSize);

	const wchar_t* p = L"Presence";
	PresenceSettings& s = presence;
	s.showZone = ReadBool(path, p, L"ShowZone", s.showZone);
	s.showPlayerName = ReadBool(path, p, L"ShowPlayerName", s.showPlayerName);
	s.showMap = ReadBool(path, p, L"ShowMap", s.showMap);
	s.showFrequency = ReadBool(path, p, L"ShowFrequency", s.showFrequency);
	s.showBounty = ReadBool(path, p, L"ShowBounty", s.showBounty);
	s.showFlags = ReadBool(path, p, L"ShowFlags", s.showFlags);
	s.showSafeZone = ReadBool(path, p, L"ShowSafeZone", s.showSafeZone);
	s.showArenaPopulation = ReadBool(path, p, L"ShowArenaPopulation", s.showArenaPopulation);
	s.showPartySize = ReadBool(path, p, L"ShowPartySize", s.showPartySize);
	s.resetTimerOnShipChange = ReadBool(path, p, L"ResetTimerOnShipChange", s.resetTimerOnShipChange);
	s.showLauncherTimer = ReadBool(path, p, L"ShowLauncherTimer", s.showLauncherTimer);
	s.guessShipFromKeys = ReadBool(path, p, L"GuessShipFromKeys", s.guessShipFromKeys);
	s.statusDisplay = static_cast<int>(ReadNumber(path, p, L"StatusDisplay", static_cast<std::uint64_t>(s.statusDisplay)));
	if (s.statusDisplay < kStatusName || s.statusDisplay > kStatusDetails)
		s.statusDisplay = kStatusName;

	s.buttons.clear();
	const wchar_t* b = L"Buttons";
	const PresenceButton defaults[2] = {
		{ "Play Subspace Continuum", "https://store.steampowered.com/app/352700/Subspace_Continuum/" },
		{ "", "" },
	};
	for (int i = 0; i < 2; ++i)
	{
		const std::wstring n = std::to_wstring(i + 1);
		PresenceButton button;
		button.label = WideToUtf8(ReadString(path, b, (L"Button" + n + L"Label").c_str(), Utf8ToWide(defaults[i].label)));
		button.url = WideToUtf8(ReadString(path, b, (L"Button" + n + L"Url").c_str(), Utf8ToWide(defaults[i].url)));
		if (button.label.empty())
			continue;
		if (IsValidButton(button))
			s.buttons.push_back(button);
		else
			warnings.push_back("ignoring Button" + std::to_string(i + 1) +
				" (label must be 1-32 characters, url must start with http:// or https://)");
	}
}

} // namespace crpc
