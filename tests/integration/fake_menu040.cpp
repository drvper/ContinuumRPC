// Fake menu040.dll: exposes a profile table shaped like the real launcher's
// (see MemoryOffsets::profile* in Config.h). The fake game reports the RVAs of
// these exports so the test can point cRPC.ini at them.
#include <cstdint>

extern "C" {
__declspec(dllexport) std::uint32_t g_profileIndex = 1;      // second profile is selected
__declspec(dllexport) std::uint32_t g_profileArray = 0;      // -> profiles (set at load)
}

static unsigned char g_profiles[2860 * 3];

static void Init()
{
	const char* names[] = { "SomeoneElse", "^Purge", "Third" };   // '^' = biller down marker
	for (int i = 0; i < 3; ++i)
	{
		char* dst = reinterpret_cast<char*>(g_profiles + i * 2860 + 0x15);
		for (const char* s = names[i]; *s; ++s)
			*dst++ = *s;
	}
	g_profileArray = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_profiles));
}

extern "C" int __stdcall DllMain(void*, unsigned long reason, void*)
{
	if (reason == 1) // DLL_PROCESS_ATTACH
		Init();
	return 1;
}
