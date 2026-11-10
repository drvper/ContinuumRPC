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
	pid_ = 0;
}

bool GameClient::Track()
{
	if (process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT)
		return true; // still running

	if (process_)
		Log("game: process %lu exited", pid_);
	Release();

	DWORD pid = FindProcess();
	if (!pid)
		return false;

	process_ = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process_)
	{
		if (pid != lastFailedPid_)
			Log("game: found pid %lu but OpenProcess failed, error %lu", pid, GetLastError());
		lastFailedPid_ = pid;
		return false;
	}
	pid_ = pid;
	Log("game: tracking pid %lu%s", pid_, pid_ == stubPid_ ? " (the launched copy did not restart itself)" : "");
	return true;
}

bool GameClient::HasWindow() const
{
	const WindowState ws = Windows();
	return ws.foreground != WindowKind::None || ws.hasGame || ws.hasChat || ws.hasLauncher;
}

WindowState GameClient::Windows() const
{
	WindowState ws;
	if (!pid_)
		return ws;

	EnumContext ctx{ pid_, &ws };
	EnumWindows(EnumGameWindows, reinterpret_cast<LPARAM>(&ctx));

	HWND fg = GetForegroundWindow();
	DWORD fgPid = 0;
	if (fg)
		GetWindowThreadProcessId(fg, &fgPid);
	if (fgPid == pid_)
	{
		wchar_t title[128];
		if (GetWindowTextW(fg, title, _countof(title)))
			ws.foreground = ClassifyWindowTitle(title);
		if (ws.foreground == WindowKind::None)
			ws.foreground = WindowKind::Launcher; // dialogs owned by the client (e.g. profile editor)
	}
	return ws;
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
