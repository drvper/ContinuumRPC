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
	bool Track();
	DWORD Pid() const { return pid_; }
	HANDLE Process() const { return process_; }

	WindowState Windows() const;
	// True once the game has shown a window, i.e. it has finished starting up.
	bool HasWindow() const;

	static std::wstring RegistryString(const wchar_t* name);
	static bool RegistryDword(const wchar_t* name, DWORD& out);

private:
	void Release();
	DWORD FindProcess() const;

	const Config& cfg_;
	HANDLE process_ = nullptr;
	DWORD pid_ = 0;
	DWORD stubPid_ = 0;          // the copy we launched (restarts itself)
	ULONGLONG launchedAt_ = 0;
	DWORD lastFailedPid_ = 0;    // avoid repeating the same log line
};

WindowKind ClassifyWindowTitle(const wchar_t* title);

} // namespace crpc

#endif // CRPC_CONTINUUM_H
