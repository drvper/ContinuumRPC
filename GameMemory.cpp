// GameMemory.cpp - see GameMemory.h
#include "GameMemory.h"

#include <tlhelp32.h>

#include <algorithm>
#include <cstring>

namespace crpc {

namespace {

constexpr std::uint32_t kDefaultImageBase = 0x400000;  // Continuum is not relocatable
constexpr std::size_t kMaxPlayers = 1024;               // size of the Player*[] array (0x1000 bytes)
constexpr std::size_t kNameLength = 23;
constexpr int kMaxFreq = 9999;

std::uint32_t U32At(const std::vector<unsigned char>& buf, std::uint32_t offset)
{
	std::uint32_t v = 0;
	if (offset + sizeof(v) <= buf.size())
		std::memcpy(&v, buf.data() + offset, sizeof(v));
	return v;
}

std::string NameAt(const std::vector<unsigned char>& buf, std::uint32_t offset)
{
	if (offset >= buf.size())
		return std::string();
	std::size_t avail = std::min<std::size_t>(kNameLength, buf.size() - offset);
	const char* p = reinterpret_cast<const char*>(buf.data() + offset);
	return std::string(p, strnlen(p, avail));
}

bool SameName(const std::string& a, const std::string& b)
{
	return !a.empty() && a.size() == b.size() && _stricmp(a.c_str(), b.c_str()) == 0;
}

} // namespace

bool GameMemory::Attach(DWORD pid)
{
	Detach();
	handle_ = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!handle_)
	{
		Log("memory: OpenProcess(%lu) failed, error %lu", pid, GetLastError());
		return false;
	}
	pid_ = pid;
	ResolveModules();
	Log("memory: attached to pid %lu (exe base 0x%08X, menu040.dll 0x%08X)", pid_, exeBase_, menuBase_);
	return true;
}

void GameMemory::Detach()
{
	if (handle_)
		CloseHandle(handle_);
	handle_ = nullptr;
	pid_ = 0;
	exeBase_ = menuBase_ = 0;
	nextModuleScan_ = 0;
	lastGame_ = 0;
	profileName_.clear();
	zoneFolder_.clear();
}

bool GameMemory::ResolveModules()
{
	ULONGLONG now = GetTickCount64();
	if (now < nextModuleScan_)
		return exeBase_ != 0;
	nextModuleScan_ = now + 5000;

	HANDLE snap = INVALID_HANDLE_VALUE;
	// ERROR_BAD_LENGTH means the module list was changing, just try again
	for (int i = 0; i < 5 && snap == INVALID_HANDLE_VALUE; ++i)
	{
		snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_);
		if (snap == INVALID_HANDLE_VALUE && GetLastError() != ERROR_BAD_LENGTH)
			break;
	}

	if (snap != INVALID_HANDLE_VALUE)
	{
		MODULEENTRY32W me;
		me.dwSize = sizeof(me);
		bool first = true;
		for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me))
		{
			auto base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(me.modBaseAddr));
			// The first module is always the executable itself. Its file name is
			// Continuum_main.exe (renamed by the cRPC install), not Continuum.exe.
			if (first)
				exeBase_ = base;
			else if (_wcsicmp(me.szModule, L"menu040.dll") == 0)
				menuBase_ = base;
			first = false;
		}
		CloseHandle(snap);
	}

	if (!exeBase_)
		exeBase_ = kDefaultImageBase;
	if (menuBase_)
		nextModuleScan_ = ~0ULL; // everything resolved, no need to scan again
	return true;
}

bool GameMemory::ReadRaw(std::uint32_t address, void* out, std::size_t len) const
{
	if (!handle_ || address < 0x10000)
		return false;
	SIZE_T read = 0;
	return ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(address)), out, len, &read) &&
		read == len;
}

std::uint32_t GameMemory::ReadU32(std::uint32_t address, bool* ok) const
{
	std::uint32_t v = 0;
	bool r = ReadRaw(address, &v, sizeof(v));
	if (ok)
		*ok = r;
	return r ? v : 0;
}

std::string GameMemory::ReadString(std::uint32_t address, std::size_t maxLen) const
{
	std::string buf(maxLen, '\0');
	if (!ReadRaw(address, &buf[0], maxLen))
		return std::string();
	buf.resize(strnlen(buf.c_str(), maxLen));
	return buf;
}

std::string GameMemory::ReadProfileName() const
{
	if (!menuBase_ || !off_.profileArray)
		return std::string();

	std::uint32_t index = ReadU32(menuBase_ + off_.profileIndex) & 0xFFFF;
	std::uint32_t profiles = ReadU32(menuBase_ + off_.profileArray);
	if (!profiles || index > 1000)
		return std::string();

	std::string name = ReadString(profiles + off_.profileName + index * off_.profileSize, kNameLength);
	// "^" is prepended to names while the billing server is down
	if (!name.empty() && name[0] == '^')
		name.erase(0, 1);
	return name;
}

GameMemory::Result GameMemory::Read(GameSnapshot& snap)
{
	Result r;
	if (!handle_)
		return r;
	ResolveModules();

	bool ok = false;
	const std::uint32_t game = ReadU32(exeBase_ + off_.gamePointer, &ok);
	r.readable = ok;

	// The selected profile can change in the launcher; it is fixed while in a zone
	if (!game || game != lastGame_ || profileName_.empty())
		profileName_ = ReadProfileName();
	if (!profileName_.empty())
		snap.playerName = AnsiToUtf8(profileName_.c_str(), profileName_.size());

	if (!game)
	{
		lastGame_ = 0;
		return r;
	}
	if (game != lastGame_)
	{
		// new game struct = new zone
		lastGame_ = game;
		zoneFolder_.clear();
	}

	const std::uint32_t pd = game + off_.playerData;

	// Same check Marvin uses: the game is loaded once the map pointer is set
	const std::uint32_t mapPtr = off_.mapName ? ReadU32(pd + off_.mapName) : 1;
	if (!mapPtr)
		return r;
	r.gameLoaded = true;

	if (off_.mapName)
	{
		std::string map = ReadString(mapPtr + 1, 16);
		// only trust printable names
		bool printable = !map.empty() && std::all_of(map.begin(), map.end(), [](char c) {
			return static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F;
		});
		if (printable)
			snap.mapFile = map;
	}

	if (off_.serverFolder && zoneFolder_.empty())
	{
		// e.g. "zones\SSCU Trench Wars" -> "SSCU Trench Wars"
		std::uint32_t folderPtr = ReadU32(pd + off_.serverFolder);
		if (folderPtr)
		{
			std::string folder = ReadString(folderPtr + off_.serverFolderName, 256);
			size_t slash = folder.find_last_of("\\/");
			if (slash != std::string::npos)
				folder = folder.substr(slash + 1);
			zoneFolder_ = AnsiToUtf8(folder.c_str(), folder.size());
		}
	}
	if (!zoneFolder_.empty())
		snap.zone = zoneFolder_;

	// Player list: one read for the pointer array, one read per player struct
	std::size_t count = ReadU32(pd + off_.playerCount) & 0xFFFF;
	if (count == 0 || count > kMaxPlayers)
		return r;

	pointers_.resize(count);
	if (!ReadRaw(pd + off_.playerList, pointers_.data(), count * sizeof(std::uint32_t)))
		return r;

	const std::uint32_t chunkSize = std::max({
		off_.playerId + 4, off_.playerFreq + 4, off_.playerShip + 4, off_.playerStatus + 4,
		off_.playerBounty1 + 4, off_.playerBounty2 + 4, off_.playerFlags1 + 4, off_.playerFlags2 + 4,
		off_.playerName + static_cast<std::uint32_t>(kNameLength) });
	if (chunkSize > 0x1000)
		return r; // nonsense offsets in the ini
	chunk_.resize(chunkSize);

	struct Entry { int ship; int freq; };
	std::vector<Entry> entries;
	entries.reserve(count);

	for (std::uint32_t ptr : pointers_)
	{
		if (!ptr || !ReadRaw(ptr, chunk_.data(), chunk_.size()))
			continue;

		int ship = static_cast<int>(U32At(chunk_, off_.playerShip) & 0xFF);
		int freq = static_cast<int>(U32At(chunk_, off_.playerFreq) & 0xFFFF);
		if (ship > kSpectator)
			continue; // not a valid player entry
		entries.push_back({ ship, freq });

		if (r.selfFound || !SameName(NameAt(chunk_, off_.playerName), profileName_))
			continue;

		r.selfFound = true;
		snap.ship = ship;
		snap.freq = freq <= kMaxFreq ? freq : -1;

		if (off_.playerBounty1)
		{
			std::uint32_t bounty = U32At(chunk_, off_.playerBounty1) + (off_.playerBounty2 ? U32At(chunk_, off_.playerBounty2) : 0);
			snap.bounty = bounty < 100000 ? static_cast<int>(bounty) : -1;
		}
		if (off_.playerFlags1)
		{
			std::uint32_t flags = U32At(chunk_, off_.playerFlags1) + (off_.playerFlags2 ? U32At(chunk_, off_.playerFlags2) : 0);
			snap.flags = flags < 1000 ? static_cast<int>(flags) : -1;
		}
		if (off_.playerStatus && off_.safetyMask)
			snap.inSafeZone = (U32At(chunk_, off_.playerStatus) & off_.safetyMask) != 0;
	}

	snap.arenaPlayers = static_cast<int>(entries.size());
	snap.arenaPlaying = 0;
	snap.teamPlayers = 0;
	for (const Entry& e : entries)
	{
		if (e.ship == kSpectator)
			continue;
		++snap.arenaPlaying;
		if (r.selfFound && snap.ship != kSpectator && e.freq == snap.freq)
			++snap.teamPlayers;
	}

	snap.memoryValid = r.selfFound;
	return r;
}

} // namespace crpc
