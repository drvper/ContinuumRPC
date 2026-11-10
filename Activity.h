// Activity.h - talks to the local Discord client and keeps the rich presence
// in sync.
//
// This speaks Discord's documented local RPC protocol directly over the
// \\.\pipe\discord-ipc-N named pipe (see DiscordRpc.h), so no Discord SDK DLL
// is needed. The archived Game SDK previously used here crashed shortly after
// connecting with current Discord clients.
#ifndef CRPC_ACTIVITY_H
#define CRPC_ACTIVITY_H

#include "DiscordBridge.h"
#include "Presence.h"

namespace crpc {

class DiscordClient {
public:
	explicit DiscordClient(std::int64_t clientId);
	~DiscordClient();
	DiscordClient(const DiscordClient&) = delete;
	DiscordClient& operator=(const DiscordClient&) = delete;

	// Queues `model` to be shown. Identical models are ignored and updates are
	// spaced out to respect Discord's rate limit (5 updates / 20 seconds).
	void SetPresence(const PresenceModel& model);

	// Connects/reconnects, processes Discord's messages and flushes queued
	// updates. Never blocks; call it frequently from one thread.
	void Tick();

	// Clears the presence and closes the connection.
	void Shutdown();

private:
	enum class State { Disconnected, Handshaking, Connected };

	bool Connect();
	void Disconnect(const char* reason);
	bool Send(std::uint32_t opcode, const std::string& json);
	void Receive();
	void OnFrame(std::uint32_t opcode, const std::string& json);
	void Flush();

	std::int64_t clientId_;
	HANDLE pipe_ = INVALID_HANDLE_VALUE;
	State state_ = State::Disconnected;
	std::string rx_;

	PresenceModel desired_;
	PresenceModel sent_;
	PresenceModel pending_;
	bool haveDesired_ = false;
	bool haveSent_ = false;
	std::string inFlightNonce_;
	unsigned nonceCounter_ = 0;

	ULONGLONG lastSend_ = 0;
	ULONGLONG nextConnect_ = 0;
	ULONGLONG deadline_ = 0;  // handshake / reply timeout
};

} // namespace crpc

#endif // CRPC_ACTIVITY_H
