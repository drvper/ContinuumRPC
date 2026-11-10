// DiscordBridge.h - common Windows includes and helpers for cRPC.
#ifndef DISCORDBRIDGE_H
#define DISCORDBRIDGE_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Minimum supported OS: Windows 7 (GetTickCount64, PROCESS_QUERY_LIMITED_INFORMATION)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <sdkddkver.h>
#include <windows.h>

#include <cstdint>
#include <string>


#define CRPC_VERSION "2.0.0"

namespace crpc {

// UTF-16 -> UTF-8 (Discord expects UTF-8 strings)
std::string WideToUtf8(const std::wstring& wstr);
std::wstring Utf8ToWide(const std::string& str);
// Continuum stores names in the Windows-1252 code page -> UTF-8
std::string AnsiToUtf8(const char* str, std::size_t maxLen);

// Diagnostics written to cRPC.log when Debug=1 in cRPC.ini
void LogOpen(const std::wstring& path);
void Log(const char* fmt, ...);

} // namespace crpc

#endif // DISCORDBRIDGE_H
