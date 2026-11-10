// Continuum.h - launching, finding and observing the Continuum client process.
#ifndef CRPC_CONTINUUM_H
#define CRPC_CONTINUUM_H

#include "DiscordBridge.h"
#include "Config.h"
#include "Presence.h"

namespace crpc {

// Classification of a top level window owned by the game process.
enum class WindowKind { None, Game, Chat, Launcher };

struct WindowState {
	WindowKind foreground = WindowKind::None;  // game window that has focus, if any
	bool hasGame = false;
	bool hasChat = false;
	bool hasLauncher = false;
};

class GameClient {
public:
	explicit GameClient(const Config& cfg) : cfg_(cfg) {}
	~GameClient() { Release(); }
	GameClient(const GameClient&) = delete;
	GameClient& operator=(const GameClient&) = delete;

	// Starts the real game executable, forwarding our command line arguments.
	bool Launch();

	// Makes sure a live game process is being tracked. Continuum restarts
	// itself on launch, so the process we started may be replaced by a new one;
	// this re-finds it by name. Returns false when no game process exists.
	//
	// The real client may refuse to be opened (its process is protected, or it
	// runs as administrator). It is then followed by pid through the process
	// list instead, and only window titles and the registry are used.
	bool Track();
	DWORD Pid() const { return pid_; }
	// Handle that becomes signaled when the game exits, or null if unavailable.
	HANDLE WaitHandle() const { return syncable_ ? process_ : nullptr; }
	// Which game window has focus (cheap; fine to call every tick).
	WindowKind Foreground() const;

	WindowState Windows() const;
	// True once the game has shown a window, i.e. it has finished starting up.
	bool HasWindow() const;

	static std::wstring RegistryString(const wchar_t* name);
	static bool RegistryDword(const wchar_t* name, DWORD& out);

private:
	void Release();
	DWORD FindProcess() const;
	bool Alive() const;
	void LogAccess(DWORD pid) const;

	const Config& cfg_;
	HANDLE process_ = nullptr;   // may be null if the game refuses access
	bool syncable_ = false;      // process_ has SYNCHRONIZE
	DWORD pid_ = 0;
	DWORD stubPid_ = 0;          // the copy we launched (restarts itself)
	ULONGLONG launchedAt_ = 0;
};

// Fallback ship detection for when the game's memory cannot be read: watches
// the ship-select keys (Esc, then 1-8 or S), the same approach the original
// cRPC used. Keys are only looked at while Continuum's game window has focus,
// and nothing is hooked or recorded.
class KeyShipTracker {
public:
	explicit KeyShipTracker(int initialShip) : ship_(initialShip) {}
	// Call frequently (every tick). Returns true when the ship changed.
	bool Update(bool gameFocused);
	int Ship() const { return ship_; }

private:
	static constexpr int kKeys = 10;   // Esc, 1-8, S
	bool down_[kKeys] = {};
	bool menu_ = false;
	int ship_;
};

WindowKind ClassifyWindowTitle(const wchar_t* title);

} // namespace crpc

#endif // CRPC_CONTINUUM_H
