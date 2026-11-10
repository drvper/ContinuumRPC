// GameMemory.h - reads game state out of the running Continuum client.
//
// The client is a separate 32-bit process, so everything is read with
// ReadProcessMemory using a read-only handle. Nothing is ever written to the
// game. Offsets come from Config::offsets (defaults taken from the Marvin bot).
#ifndef CRPC_GAMEMEMORY_H
#define CRPC_GAMEMEMORY_H

#include "DiscordBridge.h"
#include "Config.h"
#include "Presence.h"

#include <vector>

namespace crpc {

class GameMemory {
public:
	explicit GameMemory(const MemoryOffsets& offsets) : off_(offsets) {}
	~GameMemory() { Detach(); }
	GameMemory(const GameMemory&) = delete;
	GameMemory& operator=(const GameMemory&) = delete;

	bool Attach(DWORD pid);
	void Detach();
	DWORD Pid() const { return pid_; }
	bool Attached() const { return handle_ != nullptr; }

	struct Result {
		bool readable = false;    // the process could be read at all
		bool gameLoaded = false;  // inside a zone with the map loaded
		bool selfFound = false;   // our player was found in the player list
	};

	// Fills the memory backed fields of `snap` (zone, map, ship, freq, ...).
	Result Read(GameSnapshot& snap);

private:
	bool ResolveModules();
	bool ReadRaw(std::uint32_t address, void* out, std::size_t len) const;
	std::uint32_t ReadU32(std::uint32_t address, bool* ok = nullptr) const;
	std::string ReadString(std::uint32_t address, std::size_t maxLen) const;
	std::string ReadProfileName() const;

	MemoryOffsets off_;
	HANDLE handle_ = nullptr;
	DWORD pid_ = 0;
	std::uint32_t exeBase_ = 0;
	std::uint32_t menuBase_ = 0;
	ULONGLONG nextModuleScan_ = 0;

	// caches (reset whenever the game struct changes, i.e. on zone change)
	std::uint32_t lastGame_ = 0;
	std::string profileName_;
	std::string zoneFolder_;

	std::vector<std::uint32_t> pointers_;   // reusable buffers
	std::vector<unsigned char> chunk_;
};

} // namespace crpc

#endif // CRPC_GAMEMEMORY_H
