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

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>

namespace crpc {

namespace {
FILE* g_log = nullptr;
wchar_t g_crashLogPath[MAX_PATH];

// Last-resort diagnostics: if cRPC ever crashes, record where so it can be
// fixed. Only uses stack memory and plain Win32 calls (the heap may be broken).
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info)
{
	const EXCEPTION_RECORD* rec = info->ExceptionRecord;
	wchar_t module[MAX_PATH] = L"?";
	HMODULE mod = nullptr;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		static_cast<LPCWSTR>(rec->ExceptionAddress), &mod))
		GetModuleFileNameW(mod, module, MAX_PATH);

	char text[1024];
	int len = wsprintfA(text, "cRPC " CRPC_VERSION " crashed\r\nexception 0x%08lX at 0x%08lX (%ls + 0x%lX)\r\n",
		rec->ExceptionCode, static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(rec->ExceptionAddress)), module,
		static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(rec->ExceptionAddress) - reinterpret_cast<std::uintptr_t>(mod)));

	HANDLE f = CreateFileW(g_crashLogPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f != INVALID_HANDLE_VALUE)
	{
		DWORD written = 0;
		WriteFile(f, text, static_cast<DWORD>(len), &written, nullptr);
		CloseHandle(f);
	}
	if (g_log)
		fprintf(g_log, "%s", text);
	return EXCEPTION_EXECUTE_HANDLER; // exit quietly; Discord drops the presence when the pipe closes
}
}

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
	if (!g_log && _wfopen_s(&g_log, path.c_str(), L"w") != 0)
		g_log = nullptr;
}

void Log(const char* fmt, ...)
{
	if (!g_log)
		return;
	SYSTEMTIME t;
	GetLocalTime(&t);
	fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
	va_list args;
	va_start(args, fmt);
	vfprintf(g_log, fmt, args);
	va_end(args);
	fputc('\n', g_log);
	fflush(g_log);
}

namespace {

constexpr DWORD kTickMs = 50;               // Discord callback pump interval
constexpr ULONGLONG kLostGraceMs = 5000;    // Continuum restarts itself on launch

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

GameSnapshot TakeSnapshot(const Config& cfg, GameClient& game, GameMemory& memory, GameMemory::Result& mem)
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

	// Without memory the best we know is the last ship saved in the registry
	if (snap.screen != Screen::Launcher && !snap.memoryValid)
	{
		DWORD ship = 0;
		if (GameClient::RegistryDword(L"Ship", ship) && ship <= kSpectator)
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
	lstrcpynW(g_crashLogPath, (dir + L"cRPC-crash.log").c_str(), MAX_PATH);
	SetUnhandledExceptionFilter(CrashFilter);

	const std::wstring ini = dir + L"cRPC.ini";
	Config::WriteDefault(ini);
	Config cfg;
	cfg.Load(ini);
	if (cfg.debugLog)
		LogOpen(dir + L"cRPC.log");
	Log("cRPC %s", CRPC_VERSION);
	for (const std::string& w : cfg.warnings)
		Log("config: %s", w.c_str());

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

	const ULONGLONG started = GetTickCount64();
	ULONGLONG lastSeen = 0;
	ULONGLONG nextPoll = 0;
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
					if (cfg.memoryReading && memory.Pid() != game.Pid())
						memory.Attach(game.Pid());

					GameMemory::Result mem;
					GameSnapshot snap = TakeSnapshot(cfg, game, memory, mem);
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
					memory.Detach();
					if (lastSeen ? now - lastSeen > kLostGraceMs : now - started > cfg.startupTimeoutSec * 1000ULL)
						break; // the game was closed (or never started)
				}
			}
			catch (const std::exception& e)
			{
				// never take the presence down over one bad sample
				Log("error while sampling the game: %s", e.what());
			}
		}

		discord.Tick();

		// Sleep until the next tick, waking immediately if the game exits
		if (game.Process())
		{
			if (WaitForSingleObject(game.Process(), kTickMs) == WAIT_OBJECT_0)
				nextPoll = 0; // re-track right away instead of spinning on a signaled handle
		}
		else
			Sleep(kTickMs);
	}

	Log("exiting");
	discord.Shutdown();
	if (instance)
		CloseHandle(instance);
	return 0;
}
