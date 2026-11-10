// Continuum.cpp - see Continuum.h
#include "Continuum.h"

#include <tlhelp32.h>

#include <cwchar>

namespace crpc {

namespace {

const wchar_t* const kRegistryKey = L"SOFTWARE\\Continuum\\State";

bool StartsWith(const wchar_t* s, const wchar_t* prefix)
{
	return wcsncmp(s, prefix, wcslen(prefix)) == 0;
}

// Skips argv[0] of a Windows command line.
const wchar_t* ArgumentsOf(const wchar_t* cmd)
{
	if (*cmd == L'"')
	{
		++cmd;
		while (*cmd && *cmd != L'"')
			++cmd;
		if (*cmd)
			++cmd;
	}
	else
	{
		while (*cmd && *cmd != L' ' && *cmd != L'\t')
			++cmd;
	}
	while (*cmd == L' ' || *cmd == L'\t')
		++cmd;
	return cmd;
}

struct EnumContext {
	DWORD pid;
	WindowState* state;
};

BOOL CALLBACK EnumGameWindows(HWND hwnd, LPARAM lParam)
{
	auto* ctx = reinterpret_cast<EnumContext*>(lParam);
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if (pid != ctx->pid || !IsWindowVisible(hwnd))
		return TRUE;

	wchar_t title[128];
	if (!GetWindowTextW(hwnd, title, _countof(title)))
		return TRUE;

	switch (ClassifyWindowTitle(title))
	{
	case WindowKind::Game: ctx->state->hasGame = true; break;
	case WindowKind::Chat: ctx->state->hasChat = true; break;
	case WindowKind::Launcher: ctx->state->hasLauncher = true; break;
	default: break;
	}
	return TRUE;
}

} // namespace

// Window titles used by the client:
//   "Continuum" / "Subspace Continuum"                  - the game itself
//   "Continuum 0.40 - <zone>" / "Subspace Continuum ..." - windowed chat mode
//   "Continuum 0.40" / "Subspace Continuum 0.40"         - launcher (zone list)
WindowKind ClassifyWindowTitle(const wchar_t* title)
{
	const wchar_t* rest = nullptr;
	if (StartsWith(title, L"Subspace Continuum"))
		rest = title + wcslen(L"Subspace Continuum");
	else if (StartsWith(title, L"Continuum"))
		rest = title + wcslen(L"Continuum");
	else
		return WindowKind::None;

	if (*rest == L'\0')
		return WindowKind::Game;
	if (*rest != L' ')
		return WindowKind::None;
	if (wcsstr(rest, L" - "))
		return WindowKind::Chat;
	return WindowKind::Launcher;
}

bool GameClient::Launch()
{
	std::wstring exe = ExeDirectory() + cfg_.gameExecutable;
	std::wstring cmd = L"\"" + exe + L"\"";
	const wchar_t* args = ArgumentsOf(GetCommandLineW());
	if (*args)
		cmd += std::wstring(L" ") + args;

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	std::wstring dir = ExeDirectory();

	// CreateProcessW may modify the command line buffer, so it must be writable
	if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr,
		dir.empty() ? nullptr : dir.c_str(), &si, &pi))
	{
		Log("launch: CreateProcess(%s) failed, error %lu", WideToUtf8(exe).c_str(), GetLastError());
		return false;
	}

	Log("launch: started %s (pid %lu)", WideToUtf8(exe).c_str(), pi.dwProcessId);
	stubPid_ = pi.dwProcessId;
	launchedAt_ = GetTickCount64();
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

DWORD GameClient::FindProcess() const
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return 0;

	// Continuum restarts itself on launch: the copy we started spawns the real
	// client and exits. Like the original cRPC, leave that first copy alone
	// while it does so, and only fall back to it if it turns out to be the
	// game itself (still the only one running after a while).
	constexpr ULONGLONG kStubGraceMs = 15000;
	const bool stubStarting = stubPid_ && GetTickCount64() - launchedAt_ < kStubGraceMs;
	const DWORD self = GetCurrentProcessId();
	DWORD found = 0;
	DWORD stub = 0;

	PROCESSENTRY32W entry;
	entry.dwSize = sizeof(entry);
	for (BOOL ok = Process32FirstW(snap, &entry); ok; ok = Process32NextW(snap, &entry))
	{
		if (entry.th32ProcessID == self || _wcsicmp(entry.szExeFile, cfg_.gameExecutable.c_str()) != 0)
			continue;
		if (entry.th32ProcessID == stubPid_)
			stub = entry.th32ProcessID;
		else
			found = entry.th32ProcessID;
	}
	CloseHandle(snap);
	if (found)
		return found;
	return stubStarting ? 0 : stub;
}

void GameClient::Release()
{
	if (process_)
		CloseHandle(process_);
	process_ = nullptr;
	syncable_ = false;
	pid_ = 0;
}

bool GameClient::Alive() const
{
	if (process_ && syncable_)
		return WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
	if (process_)
	{
		DWORD code = 0;
		return GetExitCodeProcess(process_, &code) && code == STILL_ACTIVE;
	}

	// No handle: look for the pid (with the right name, in case it was reused)
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return true; // cannot tell; assume it is still there
	bool alive = false;
	PROCESSENTRY32W entry;
	entry.dwSize = sizeof(entry);
	for (BOOL ok = Process32FirstW(snap, &entry); ok && !alive; ok = Process32NextW(snap, &entry))
		alive = entry.th32ProcessID == pid_ && _wcsicmp(entry.szExeFile, cfg_.gameExecutable.c_str()) == 0;
	CloseHandle(snap);
	return alive;
}

// Records which kinds of access the game allows, to tell a protected process
// apart from one running as administrator.
void GameClient::LogAccess(DWORD pid) const
{
	struct Right { DWORD mask; const char* name; } rights[] = {
		{ PROCESS_QUERY_LIMITED_INFORMATION, "query" },
		{ SYNCHRONIZE, "synchronize" },
		{ PROCESS_VM_READ, "read memory" },
	};
	std::string granted, denied;
	for (const Right& r : rights)
	{
		HANDLE h = OpenProcess(r.mask, FALSE, pid);
		std::string& list = h ? granted : denied;
		list += list.empty() ? r.name : std::string(", ") + r.name;
		if (h)
			CloseHandle(h);
	}

	bool elevated = false;
	HANDLE token = nullptr;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
	{
		TOKEN_ELEVATION e{};
		DWORD size = 0;
		elevated = GetTokenInformation(token, TokenElevation, &e, sizeof(e), &size) && e.TokenIsElevated;
		CloseHandle(token);
	}
	Log("game: access to pid %lu - allowed: [%s], denied: [%s]; cRPC %s running as administrator",
		pid, granted.c_str(), denied.c_str(), elevated ? "is" : "is not");
}

bool GameClient::Track()
{
	if (pid_)
	{
		if (Alive())
			return true;
		Log("game: process %lu exited", pid_);
		Release();
	}

	const DWORD pid = FindProcess();
	if (!pid)
		return false;

	// Ask for as much as the game allows; nothing at all is fine too
	const DWORD attempts[] = { SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, SYNCHRONIZE, PROCESS_QUERY_LIMITED_INFORMATION };
	DWORD error = ERROR_ACCESS_DENIED;
	// test hook for tests/integration/run.sh: behave as if every request was denied
	const bool testNoHandle = GetEnvironmentVariableW(L"CRPC_TEST_NO_PROCESS_HANDLE", nullptr, 0) > 0;
	for (DWORD rights : attempts)
	{
		if (testNoHandle)
			break;
		process_ = OpenProcess(rights, FALSE, pid);
		if (process_)
		{
			syncable_ = (rights & SYNCHRONIZE) != 0;
			break;
		}
		error = GetLastError();
	}
	pid_ = pid;

	if (process_)
		Log("game: tracking pid %lu%s", pid_, pid_ == stubPid_ ? " (the launched copy did not restart itself)" : "");
	else
		Log("game: tracking pid %lu through the process list (the game refused access, error %lu)", pid_, error);
	LogAccess(pid_);
	return true;
}

bool GameClient::HasWindow() const
{
	const WindowState ws = Windows();
	return ws.foreground != WindowKind::None || ws.hasGame || ws.hasChat || ws.hasLauncher;
}

WindowKind GameClient::Foreground() const
{
	HWND fg = GetForegroundWindow();
	DWORD fgPid = 0;
	if (!pid_ || !fg)
		return WindowKind::None;
	GetWindowThreadProcessId(fg, &fgPid);
	if (fgPid != pid_)
		return WindowKind::None;
	wchar_t title[128];
	WindowKind kind = GetWindowTextW(fg, title, _countof(title)) ? ClassifyWindowTitle(title) : WindowKind::None;
	return kind == WindowKind::None ? WindowKind::Launcher : kind; // dialogs owned by the client
}

WindowState GameClient::Windows() const
{
	WindowState ws;
	if (!pid_)
		return ws;

	EnumContext ctx{ pid_, &ws };
	EnumWindows(EnumGameWindows, reinterpret_cast<LPARAM>(&ctx));

	ws.foreground = Foreground();
	return ws;
}

bool KeyShipTracker::Update(bool gameFocused)
{
	static const int kVk[kKeys] = { VK_ESCAPE, '1', '2', '3', '4', '5', '6', '7', '8', 'S' };
	if (!gameFocused)
	{
		menu_ = false;
		for (bool& d : down_)
			d = false;
		return false;
	}

	const int before = ship_;
	for (int i = 0; i < kKeys; ++i)
	{
		const bool down = (GetAsyncKeyState(kVk[i]) & 0x8000) != 0;
		const bool pressed = down && !down_[i];
		down_[i] = down;
		if (!pressed)
			continue;

		if (i == 0)
			menu_ = !menu_;          // Esc opens or closes the ship menu
		else if (menu_)
		{
			ship_ = i - 1;           // 1-8 -> ships 0-7, S -> 8 (spectator)
			menu_ = false;
		}
	}
	return ship_ != before;
}

std::wstring GameClient::RegistryString(const wchar_t* name)
{
	wchar_t buf[256];
	DWORD size = sizeof(buf);
	if (RegGetValueW(HKEY_CURRENT_USER, kRegistryKey, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
		return std::wstring();
	return buf;
}

bool GameClient::RegistryDword(const wchar_t* name, DWORD& out)
{
	DWORD size = sizeof(out);
	return RegGetValueW(HKEY_CURRENT_USER, kRegistryKey, name, RRF_RT_REG_DWORD, nullptr, &out, &size) == ERROR_SUCCESS;
}

} // namespace crpc
