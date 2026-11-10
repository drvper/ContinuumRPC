// DiscordBridge.cpp - cRPC entry point.
//
// cRPC replaces Continuum.exe: it starts the real client (Continuum_main.exe),
// then samples the game state from the client's memory once per poll interval
// and mirrors it to Discord rich presence. Everything runs on one thread.
#include "DiscordBridge.h"
#include "Activity.h"
#include "Config.h"
#include "Continuum.h"
#include "GameMemory.h"
#include "Presence.h"

#include <tlhelp32.h>

#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <stdexcept>

namespace crpc {

namespace {

// Both files are written with plain WriteFile so they can also be used from
// crash handlers (where the CRT or the heap may be broken).
HANDLE g_log = INVALID_HANDLE_VALUE;
wchar_t g_crashLogPath[MAX_PATH];

void WriteRaw(HANDLE f, const char* text)
{
	DWORD written = 0;
	WriteFile(f, text, lstrlenA(text), &written, nullptr);
}

void Timestamp(char* out)
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	wsprintfA(out, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
}

// Records a fatal problem in cRPC-crash.log (always) and cRPC.log (if enabled).
void WriteCrash(const char* what)
{
	char line[1200];
	Timestamp(line);
	lstrcatA(line, "cRPC " CRPC_VERSION ": ");
	lstrcpynA(line + lstrlenA(line), what, 1024);
	lstrcatA(line, "\r\n");

	HANDLE f = CreateFileW(g_crashLogPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f != INVALID_HANDLE_VALUE)
	{
		WriteRaw(f, line);
		CloseHandle(f);
	}
	if (g_log != INVALID_HANDLE_VALUE)
		WriteRaw(g_log, line);
}

// "exception 0xC0000005 at 0x0040123A (C:\...\Continuum.exe + 0x123A)"
void DescribeException(const EXCEPTION_RECORD* rec, char* out)
{
	wchar_t module[MAX_PATH] = L"?";
	HMODULE mod = nullptr;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		static_cast<LPCWSTR>(rec->ExceptionAddress), &mod))
		GetModuleFileNameW(mod, module, MAX_PATH);
	const auto addr = reinterpret_cast<std::uintptr_t>(rec->ExceptionAddress);
	wsprintfA(out, "exception 0x%08lX at 0x%08lX (%ls + 0x%lX)", rec->ExceptionCode,
		static_cast<unsigned long>(addr), module, static_cast<unsigned long>(addr - reinterpret_cast<std::uintptr_t>(mod)));
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info)
{
	char text[1024] = "crashed: ";
	DescribeException(info->ExceptionRecord, text + lstrlenA(text));
	WriteCrash(text);
	return EXCEPTION_EXECUTE_HANDLER; // exit quietly; Discord drops the presence when the pipe closes
}

// Runs before any other handler, including ones installed by DLLs that other
// programs inject into this process, so a fatal error is recorded even if one
// of those swallows the crash. Only logged to cRPC.log because the exception
// may still be handled.
LONG CALLBACK FirstChanceHandler(EXCEPTION_POINTERS* info)
{
	static int reported = 0;
	const DWORD code = info->ExceptionRecord->ExceptionCode;
	if ((code & 0xF0000000) == 0xC0000000 && g_log != INVALID_HANDLE_VALUE && reported < 20)
	{
		++reported;
		char line[1200];
		Timestamp(line);
		lstrcatA(line, "first-chance ");
		DescribeException(info->ExceptionRecord, line + lstrlenA(line));
		lstrcatA(line, "\r\n");
		WriteRaw(g_log, line);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

void OnAbort(int) { WriteCrash("abort() was called"); }
void OnTerminate()
{
	WriteCrash("std::terminate: an exception was not handled");
	std::abort();
}

void InstallCrashHandlers(const std::wstring& dir)
{
	lstrcpynW(g_crashLogPath, (dir + L"cRPC-crash.log").c_str(), MAX_PATH);
	AddVectoredExceptionHandler(1, FirstChanceHandler);
	SetUnhandledExceptionFilter(CrashFilter);
	std::signal(SIGABRT, OnAbort);
	std::set_terminate(OnTerminate);
#ifdef _MSC_VER
	_set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
		WriteCrash("the C runtime reported an invalid parameter");
		TerminateProcess(GetCurrentProcess(), 3);
	});
	_set_purecall_handler([] {
		WriteCrash("pure virtual function call");
		TerminateProcess(GetCurrentProcess(), 3);
	});
#endif
}

} // namespace

std::string WideToUtf8(const std::wstring& wstr)
{
	if (wstr.empty())
		return std::string();
	int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
	std::string out(size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &out[0], size, nullptr, nullptr);
	return out;
}

std::wstring Utf8ToWide(const std::string& str)
{
	if (str.empty())
		return std::wstring();
	int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
	std::wstring out(size, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), &out[0], size);
	return out;
}

std::string AnsiToUtf8(const char* str, std::size_t maxLen)
{
	std::size_t len = strnlen(str, maxLen);
	if (!len)
		return std::string();
	int wlen = MultiByteToWideChar(1252, 0, str, static_cast<int>(len), nullptr, 0);
	std::wstring w(wlen, L'\0');
	MultiByteToWideChar(1252, 0, str, static_cast<int>(len), &w[0], wlen);
	return WideToUtf8(w);
}

void LogOpen(const std::wstring& path)
{
	if (g_log == INVALID_HANDLE_VALUE)
		g_log = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void Log(const char* fmt, ...)
{
	if (g_log == INVALID_HANDLE_VALUE)
		return;
	char line[2048];
	Timestamp(line);
	const int prefix = lstrlenA(line);
	va_list args;
	va_start(args, fmt);
	int n = std::vsnprintf(line + prefix, sizeof(line) - prefix - 3, fmt, args);
	va_end(args);
	if (n < 0)
		n = 0;
	if (n > static_cast<int>(sizeof(line)) - prefix - 3)
		n = static_cast<int>(sizeof(line)) - prefix - 3;
	lstrcpyA(line + prefix + n, "\r\n");
	WriteRaw(g_log, line);
}

namespace {

constexpr DWORD kTickMs = 50;               // Discord callback pump interval
constexpr ULONGLONG kLostGraceMs = 5000;    // Continuum restarts itself on launch

// "pid 1234 (Discord.exe)" - shows how cRPC was launched (Steam, Discord, Explorer)
std::string ParentDescription()
{
	std::string result = "unknown";
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return result;
	PROCESSENTRY32W e;
	e.dwSize = sizeof(e);
	DWORD parent = 0;
	for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
		if (e.th32ProcessID == GetCurrentProcessId())
			parent = e.th32ParentProcessID;
	for (BOOL ok = Process32FirstW(snap, &e); ok && parent; ok = Process32NextW(snap, &e))
		if (e.th32ProcessID == parent)
			result = "pid " + std::to_string(parent) + " (" + WideToUtf8(e.szExeFile) + ")";
	CloseHandle(snap);
	return result;
}

std::string Describe(const GameSnapshot& s, const GameMemory::Result& r)
{
	static const char* const kScreens[] = { "launcher", "zone", "chat" };
	char buf[512];
	snprintf(buf, sizeof(buf),
		"screen=%s focused=%d mem(readable=%d loaded=%d self=%d) zone='%s' map='%s' name='%s' "
		"ship=%d freq=%d bounty=%d flags=%d safe=%d players=%d playing=%d team=%d",
		kScreens[static_cast<int>(s.screen)], s.focused, r.readable, r.gameLoaded, r.selfFound,
		s.zone.c_str(), s.mapFile.c_str(), s.playerName.c_str(), s.ship, s.freq, s.bounty, s.flags,
		s.inSafeZone, s.arenaPlayers, s.arenaPlaying, s.teamPlayers);
	return buf;
}

GameSnapshot TakeSnapshot(const Config& cfg, GameClient& game, GameMemory& memory, const KeyShipTracker* keys,
	GameMemory::Result& mem)
{
	GameSnapshot snap;
	snap.zone = WideToUtf8(GameClient::RegistryString(L"Zone"));
	snap.skin = WideToUtf8(GameClient::RegistryString(L"Skin"));

	mem = GameMemory::Result();
	if (cfg.memoryReading && memory.Attached())
		mem = memory.Read(snap);

	const WindowState ws = game.Windows();
	const bool memoryUsable = mem.readable;

	if (ws.foreground == WindowKind::Chat)
	{
		snap.screen = Screen::Chat;
		snap.focused = true;
	}
	else if (memoryUsable ? mem.gameLoaded : (ws.foreground == WindowKind::Game || ws.hasGame))
	{
		snap.screen = Screen::InZone;
		snap.focused = ws.foreground == WindowKind::Game;
	}
	else
	{
		snap.screen = Screen::Launcher;
		snap.focused = ws.foreground != WindowKind::None;
	}

	// Without memory: the ship picked with the keyboard, else the last ship
	// saved in the registry
	if (snap.screen != Screen::Launcher && !snap.memoryValid)
	{
		DWORD ship = 0;
		if (keys && keys->Ship() >= 0)
			snap.ship = keys->Ship();
		else if (GameClient::RegistryDword(L"Ship", ship) && ship <= kSpectator)
			snap.ship = static_cast<int>(ship);
	}
	return snap;
}

} // namespace
} // namespace crpc

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);
	using namespace crpc;

	const std::wstring dir = ExeDirectory();
	InstallCrashHandlers(dir);

	const std::wstring ini = dir + L"cRPC.ini";
	Config::WriteDefault(ini);
	Config cfg;
	cfg.Load(ini);
	if (cfg.debugLog)
		LogOpen(dir + L"cRPC.log");
	Log("cRPC %s, pid %lu, started by %s", CRPC_VERSION, GetCurrentProcessId(), ParentDescription().c_str());
	for (const std::string& w : cfg.warnings)
		Log("config: %s", w.c_str());

	// Test hook for tests/integration/run.sh: proves each failure path is logged
	wchar_t test[16] = L"";
	if (GetEnvironmentVariableW(L"CRPC_TEST_CRASH", test, _countof(test)))
	{
		if (!lstrcmpW(test, L"access"))
			*static_cast<volatile int*>(nullptr) = 1;
		if (!lstrcmpW(test, L"abort"))
			std::abort();
		if (!lstrcmpW(test, L"throw"))
			throw std::runtime_error("test");
	}

	GameClient game(cfg);

	// Only one presence instance; a second launch just starts another client
	HANDLE instance = CreateMutexW(nullptr, TRUE, L"Local\\ContinuumRPC");
	const bool primary = !instance || GetLastError() != ERROR_ALREADY_EXISTS;

	if (cfg.launchGame)
		game.Launch();
	if (!primary)
		return 0;

	DiscordClient discord(cfg.clientId);
	GameMemory memory(cfg.offsets);
	PresenceBuilder builder;
	DWORD initialShip = 0;
	KeyShipTracker keys(GameClient::RegistryDword(L"Ship", initialShip) && initialShip <= kSpectator
		? static_cast<int>(initialShip) : kShipUnknown);
	DWORD memoryTriedPid = 0;   // memory is attached at most once per game process

	const ULONGLONG started = GetTickCount64();
	ULONGLONG lastSeen = 0;
	ULONGLONG nextPoll = 0;
	ULONGLONG nextHeartbeat = 0;
	bool waitingLogged = false;
	std::string lastDescription;

	for (;;)
	{
		const ULONGLONG now = GetTickCount64();

		if (now >= nextPoll)
		{
			nextPoll = now + cfg.pollIntervalMs;
			try
			{
				if (game.Track())
				{
					lastSeen = now;
					// Attach only once the client is up (has a window), never
					// while it is still starting
					if (cfg.memoryReading && memoryTriedPid != game.Pid() && game.HasWindow())
					{
						memoryTriedPid = game.Pid();
						if (!memory.Attach(game.Pid()))
							Log("memory: not available, using window titles, the registry%s instead",
								cfg.presence.guessShipFromKeys ? " and the ship-select keys" : "");
					}

					GameMemory::Result mem;
					const bool useKeys = cfg.presence.guessShipFromKeys && !memory.Attached();
					GameSnapshot snap = TakeSnapshot(cfg, game, memory, useKeys ? &keys : nullptr, mem);
					discord.SetPresence(builder.Build(snap, cfg.presence, static_cast<std::int64_t>(std::time(nullptr))));

					if (cfg.debugLog)
					{
						std::string d = Describe(snap, mem);
						if (d != lastDescription)
							Log("state: %s", d.c_str());
						lastDescription.swap(d);
					}
				}
				else
				{
					if (!waitingLogged)
						Log("game: waiting for %s to start", WideToUtf8(cfg.gameExecutable).c_str());
					waitingLogged = true;
					memory.Detach();
					if (lastSeen && now - lastSeen > kLostGraceMs)
					{
						Log("exiting: the game was closed");
						break;
					}
					if (!lastSeen && now - started > cfg.startupTimeoutSec * 1000ULL)
					{
						Log("exiting: %s did not start within %u s", WideToUtf8(cfg.gameExecutable).c_str(), cfg.startupTimeoutSec);
						break;
					}
				}
			}
			catch (const std::exception& e)
			{
				// never take the presence down over one bad sample
				Log("error while sampling the game: %s", e.what());
			}
		}

		discord.Tick();

		// Ship-select keys are short presses, so they are checked every tick
		if (cfg.presence.guessShipFromKeys && game.Pid() && !memory.Attached() &&
			keys.Update(game.Foreground() == WindowKind::Game))
		{
			Log("keys: ship changed to %s", ShipName(keys.Ship()));
			nextPoll = 0; // show it right away
		}

		if (cfg.debugLog && now >= nextHeartbeat)
		{
			nextHeartbeat = now + 10000;
			Log("alive: game pid %lu, memory %s, discord %s", game.Pid(),
				memory.Attached() ? "attached" : "not attached", discord.Status());
		}

		// Sleep until the next tick, waking immediately if the game exits
		if (game.WaitHandle())
		{
			if (WaitForSingleObject(game.WaitHandle(), kTickMs) == WAIT_OBJECT_0)
				nextPoll = 0; // re-track right away instead of spinning on a signaled handle
		}
		else
			Sleep(kTickMs);
	}

	discord.Shutdown();
	if (instance)
		CloseHandle(instance);
	return 0;
}
