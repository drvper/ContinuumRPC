// Fake Discord client for the integration test: serves Discord's local RPC
// protocol on \\.\pipe\discord-ipc-0 and records everything cRPC sends.
//
//   fake_discord <log-file>
//
// It answers the handshake with READY, sends a PING (expects a PONG back),
// acknowledges SET_ACTIVITY commands and rejects the second one with an ERROR
// to exercise cRPC's retry path. Exits when cRPC closes the pipe.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

static FILE* g_log;

static bool ReadExact(HANDLE pipe, void* buf, DWORD len)
{
	auto* p = static_cast<char*>(buf);
	while (len)
	{
		DWORD read = 0;
		if (!ReadFile(pipe, p, len, &read, nullptr) || !read)
			return false;
		p += read;
		len -= read;
	}
	return true;
}

static bool ReadFrame(HANDLE pipe, std::uint32_t& op, std::string& json)
{
	std::uint32_t header[2];
	if (!ReadExact(pipe, header, sizeof(header)))
		return false;
	op = header[0];
	json.resize(header[1]);
	return header[1] == 0 || ReadExact(pipe, &json[0], header[1]);
}

static void WriteFrame(HANDLE pipe, std::uint32_t op, const std::string& json)
{
	std::uint32_t header[2] = { op, static_cast<std::uint32_t>(json.size()) };
	std::string frame(reinterpret_cast<char*>(header), sizeof(header));
	frame += json;
	DWORD written = 0;
	WriteFile(pipe, frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr);
}

static std::string Nonce(const std::string& json)
{
	size_t p = json.find("\"nonce\":\"");
	if (p == std::string::npos)
		return "";
	p += 9;
	return json.substr(p, json.find('"', p) - p);
}

int main(int argc, char** argv)
{
	if (argc < 2)
		return 1;
	g_log = std::fopen(argv[1], "w");

	HANDLE pipe = CreateNamedPipeW(L"\\\\.\\pipe\\discord-ipc-0", PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
	if (pipe == INVALID_HANDLE_VALUE)
	{
		std::fprintf(g_log, "CreateNamedPipe failed %lu\n", GetLastError());
		return 1;
	}
	std::fprintf(g_log, "LISTENING\n");
	std::fflush(g_log);
	if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED)
		return 1;

	int activities = 0;
	std::uint32_t op;
	std::string json;
	while (ReadFrame(pipe, op, json))
	{
		switch (op)
		{
		case 0:
			std::fprintf(g_log, "HANDSHAKE %s\n", json.c_str());
			WriteFrame(pipe, 1, "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1,\"config\":{},\"user\":{\"id\":\"1\",\"username\":\"tester\"}},\"evt\":\"READY\",\"nonce\":null}");
			WriteFrame(pipe, 3, "{\"ping\":1}");
			break;
		case 1:
			if (json.find("\"activity\"") == std::string::npos)
				std::fprintf(g_log, "CLEAR %s\n", json.c_str());
			else if (++activities == 2)
			{
				std::fprintf(g_log, "REJECTED %s\n", json.c_str());
				WriteFrame(pipe, 1, "{\"cmd\":\"SET_ACTIVITY\",\"data\":{\"code\":4000,\"message\":\"test rejection\"},\"evt\":\"ERROR\",\"nonce\":\"" + Nonce(json) + "\"}");
				break;
			}
			else
				std::fprintf(g_log, "ACTIVITY %s\n", json.c_str());
			WriteFrame(pipe, 1, "{\"cmd\":\"SET_ACTIVITY\",\"data\":{},\"evt\":null,\"nonce\":\"" + Nonce(json) + "\"}");
			break;
		case 4:
			std::fprintf(g_log, "PONG %s\n", json.c_str());
			break;
		default:
			std::fprintf(g_log, "OP%u %s\n", op, json.c_str());
		}
		std::fflush(g_log);
	}
	std::fprintf(g_log, "CLIENT CLOSED\n");
	std::fclose(g_log);
	return 0;
}
