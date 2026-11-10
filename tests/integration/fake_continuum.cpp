// Fake Continuum client used to test cRPC's memory reader end to end under
// Wine (or on Windows). It lays out its memory exactly the way the offsets in
// Config.h describe (taken from the Marvin bot) and walks through a scripted
// session:
//
//   0-3 s  launcher window ("Continuum 0.40"), not in a zone
//   3-7 s  in "Trench Wars" on trench.lvl as a Javelin, freq 1, bounty 42
//   7-11 s switched to Shark, 2 flags, in a safety zone
//   11 s   exits
//
// Like the real client it restarts itself on launch (the first process spawns
// a child and exits), which exercises cRPC's process re-tracking.
//
// `fake_continuum --print-rva` prints the [Memory] overrides for cRPC.ini,
// since the fake's globals cannot live at the real client's addresses.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
__declspec(dllexport) std::uint32_t g_gamePointer = 0;   // real client: exe + 0xC1AFC
}

namespace {

constexpr std::uint32_t kPlayerData = 0x127EC;
constexpr std::uint32_t kPlayerSize = 0x100;

unsigned char* g_game;
unsigned char g_map[32];
unsigned char g_folder[0x200];
unsigned char g_players[4][kPlayerSize];

void Put32(unsigned char* base, std::uint32_t off, std::uint32_t v) { std::memcpy(base + off, &v, 4); }
void PutStr(unsigned char* base, std::uint32_t off, const char* s) { std::strcpy(reinterpret_cast<char*>(base + off), s); }

void SetPlayer(int i, const char* name, std::uint32_t id, std::uint32_t ship, std::uint32_t freq,
	std::uint32_t bounty, std::uint32_t flags, std::uint32_t status)
{
	unsigned char* p = g_players[i];
	Put32(p, 0x18, id);
	Put32(p, 0x20, bounty / 2);           // bounty and flags are split across two fields
	Put32(p, 0x24, bounty - bounty / 2);
	Put32(p, 0x30, flags);
	Put32(p, 0x34, 0);
	Put32(p, 0x58, freq);
	Put32(p, 0x5C, ship);
	Put32(p, 0x60, status);
	PutStr(p, 0x6D, name);
}

void EnterZone()
{
	static unsigned char game[kPlayerData + 0x6000];
	g_game = game;
	unsigned char* pd = g_game + kPlayerData;

	PutStr(g_map, 1, "trench.lvl");
	Put32(pd, 0x6C4, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_map)));
	PutStr(g_folder, 0x10D, "zones\\Trench Wars");
	Put32(pd, 0x5A3C, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_folder)));

	SetPlayer(0, "Alpha", 10, 1, 1, 5, 0, 0);        // teammate
	SetPlayer(1, "Purge", 11, 1, 1, 42, 0, 0);       // us (Javelin, freq 1)
	SetPlayer(2, "Bravo", 12, 4, 0, 7, 0, 0);        // enemy
	SetPlayer(3, "Watcher", 13, 8, 8025, 0, 0, 0);   // spectator
	for (int i = 0; i < 4; ++i)
		Put32(pd, 0x884 + i * 4, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_players[i])));
	Put32(pd, 0x1884, 4);

	g_gamePointer = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_game));
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

// Keeps `wnd` in the foreground (a freshly started process may be refused
// focus on its first attempt) while pumping messages for `ms`.
void Pump(HWND wnd, DWORD ms)
{
	DWORD end = GetTickCount() + ms;
	while (GetTickCount() < end)
	{
		if (GetForegroundWindow() != wnd)
			SetForegroundWindow(wnd);
		MSG msg;
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
			DispatchMessageW(&msg);
		Sleep(20);
	}
}

} // namespace

int main(int argc, char** argv)
{
	HMODULE menu = LoadLibraryW(L"menu040.dll");
	if (!menu)
	{
		std::printf("menu040.dll missing\n");
		return 1;
	}

	if (argc > 1 && std::strcmp(argv[1], "--print-rva") == 0)
	{
		auto exe = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
		auto m = reinterpret_cast<std::uintptr_t>(menu);
		std::printf("GamePointer=0x%X\n", static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&g_gamePointer) - exe));
		std::printf("ProfileIndex=0x%X\n", static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(GetProcAddress(menu, "g_profileIndex")) - m));
		std::printf("ProfileArray=0x%X\n", static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(GetProcAddress(menu, "g_profileArray")) - m));
		return 0;
	}

	if (argc < 2 || std::strcmp(argv[1], "--child") != 0)
	{
		// Mimic Continuum restarting itself, then exit
		wchar_t path[MAX_PATH];
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		wchar_t cmd[MAX_PATH + 16];
		swprintf(cmd, MAX_PATH + 16, L"\"%ls\" --child", path);
		STARTUPINFOW si{};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi{};
		CreateProcessW(path, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
		Sleep(500);
		return 0;
	}

	WNDCLASSW wc{};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"FakeContinuum";
	RegisterClassW(&wc);
	HWND wnd = CreateWindowW(L"FakeContinuum", L"Continuum 0.40", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		0, 0, 320, 240, nullptr, nullptr, wc.hInstance, nullptr);
	Pump(wnd, 3000);

	EnterZone();
	SetWindowTextW(wnd, L"Continuum");
	Pump(wnd, 4000);

	SetPlayer(1, "Purge", 11, 7, 1, 120, 2, 0x20);   // Shark, 2 flags, in safe
	Pump(wnd, 4000);

	DestroyWindow(wnd);
	return 0;
}
