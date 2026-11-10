// Activity.cpp - see Activity.h
#include "Activity.h"
#include "DiscordRpc.h"

namespace crpc {

namespace {

constexpr ULONGLONG kMinUpdateIntervalMs = 4000;   // 5 updates per 20 s
constexpr ULONGLONG kReconnectIntervalMs = 10000;
constexpr ULONGLONG kReplyTimeoutMs = 10000;
constexpr int kMaxPipes = 10;                      // discord-ipc-0 .. discord-ipc-9

} // namespace

DiscordClient::DiscordClient(std::int64_t clientId) : clientId_(clientId)
{
}

DiscordClient::~DiscordClient()
{
	if (pipe_ != INVALID_HANDLE_VALUE)
		CloseHandle(pipe_);
}

bool DiscordClient::Connect()
{
	nextConnect_ = GetTickCount64() + kReconnectIntervalMs;

	for (int i = 0; i < kMaxPipes && pipe_ == INVALID_HANDLE_VALUE; ++i)
	{
		wchar_t name[64];
		swprintf(name, _countof(name), L"\\\\.\\pipe\\discord-ipc-%d", i);
		pipe_ = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	}
	if (pipe_ == INVALID_HANDLE_VALUE)
		return false; // Discord is not running; try again later

	rx_.clear();
	state_ = State::Handshaking;
	deadline_ = GetTickCount64() + kReplyTimeoutMs;
	if (!Send(rpc::kHandshake, rpc::HandshakeJson(clientId_)))
		return false;
	Log("discord: pipe opened, handshake sent");
	return true;
}

void DiscordClient::Disconnect(const char* reason)
{
	if (pipe_ != INVALID_HANDLE_VALUE)
	{
		Log("discord: disconnected (%s)", reason);
		CloseHandle(pipe_);
	}
	pipe_ = INVALID_HANDLE_VALUE;
	state_ = State::Disconnected;
	haveSent_ = false;         // a new connection starts without a presence
	inFlightNonce_.clear();
	rx_.clear();
	nextConnect_ = GetTickCount64() + kReconnectIntervalMs;
}

bool DiscordClient::Send(std::uint32_t opcode, const std::string& json)
{
	const std::string frame = rpc::EncodeFrame(static_cast<rpc::Opcode>(opcode), json);
	DWORD written = 0;
	if (!WriteFile(pipe_, frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr) || written != frame.size())
	{
		Disconnect("write failed");
		return false;
	}
	return true;
}

void DiscordClient::Receive()
{
	// Only read what is already there so the loop never blocks
	for (;;)
	{
		DWORD available = 0;
		if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &available, nullptr))
		{
			Disconnect("Discord closed the connection");
			return;
		}
		if (!available)
			break;

		char buf[4096];
		DWORD read = 0;
		if (!ReadFile(pipe_, buf, available < sizeof(buf) ? available : static_cast<DWORD>(sizeof(buf)), &read, nullptr) || !read)
		{
			Disconnect("read failed");
			return;
		}
		rx_.append(buf, read);
	}

	rpc::Frame frame;
	long used;
	while ((used = rpc::DecodeFrame(rx_, frame)) > 0)
	{
		rx_.erase(0, static_cast<std::size_t>(used));
		OnFrame(frame.opcode, frame.json);
		if (state_ == State::Disconnected)
			return;
	}
	if (used < 0)
		Disconnect("corrupt data");
}

void DiscordClient::OnFrame(std::uint32_t opcode, const std::string& json)
{
	switch (opcode)
	{
	case rpc::kPing:
		Send(rpc::kPong, json);
		return;
	case rpc::kClose:
		// e.g. {"code":4000,"message":"Invalid Client ID"}
		Log("discord: closed by Discord: %s", json.c_str());
		Disconnect("closed by Discord");
		return;
	case rpc::kFrame:
		break;
	default:
		return;
	}

	const std::string evt = rpc::JsonStringField(json, "evt");
	if (state_ == State::Handshaking)
	{
		if (evt == "READY")
		{
			state_ = State::Connected;
			Log("discord: connected");
		}
		return;
	}

	const std::string nonce = rpc::JsonStringField(json, "nonce");
	if (nonce.empty() || nonce != inFlightNonce_)
		return;
	inFlightNonce_.clear();

	if (evt == "ERROR")
	{
		Log("discord: SET_ACTIVITY rejected: %s", json.c_str());
		haveSent_ = false; // retried after the rate limit interval
	}
	else
	{
		sent_ = pending_;
		haveSent_ = true;
	}
}

void DiscordClient::SetPresence(const PresenceModel& model)
{
	desired_ = model;
	haveDesired_ = true;
}

void DiscordClient::Flush()
{
	if (state_ != State::Connected || !haveDesired_)
		return;

	const ULONGLONG now = GetTickCount64();
	if (!inFlightNonce_.empty())
	{
		if (now < deadline_)
			return;
		inFlightNonce_.clear(); // no reply; send again
	}
	if (haveSent_ && desired_ == sent_)
		return;
	if (lastSend_ && now - lastSend_ < kMinUpdateIntervalMs)
		return; // the latest model stays queued until the rate limit allows it

	pending_ = desired_;
	inFlightNonce_ = std::to_string(++nonceCounter_);
	lastSend_ = now;
	deadline_ = now + kReplyTimeoutMs;
	Send(rpc::kFrame, rpc::SetActivityJson(&pending_, GetCurrentProcessId(), inFlightNonce_));
}

void DiscordClient::Tick()
{
	if (state_ == State::Disconnected)
	{
		if (GetTickCount64() < nextConnect_ || !Connect())
			return;
	}

	Receive();
	if (state_ == State::Handshaking && GetTickCount64() > deadline_)
		Disconnect("handshake timed out");
	Flush();
}

void DiscordClient::Shutdown()
{
	if (state_ == State::Connected)
	{
		Send(rpc::kFrame, rpc::SetActivityJson(nullptr, GetCurrentProcessId(), "clear"));
		// give Discord a moment to apply it before the pipe closes
		for (int i = 0; i < 10 && state_ == State::Connected; ++i)
		{
			DWORD available = 0;
			if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &available, nullptr) || available)
				break;
			Sleep(20);
		}
	}
	if (pipe_ != INVALID_HANDLE_VALUE)
		CloseHandle(pipe_);
	pipe_ = INVALID_HANDLE_VALUE;
	state_ = State::Disconnected;
}

} // namespace crpc
