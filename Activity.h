// Activity.h - owns the Discord Game SDK connection and pushes presence updates.
#ifndef CRPC_ACTIVITY_H
#define CRPC_ACTIVITY_H

#include "DiscordBridge.h"
#include "Presence.h"
#include "discordsdk/discord.h"

#include <memory>

namespace crpc {

class DiscordClient {
public:
	DiscordClient(std::int64_t clientId, bool steam);
	~DiscordClient();
	DiscordClient(const DiscordClient&) = delete;
	DiscordClient& operator=(const DiscordClient&) = delete;

	// Queues `model` to be shown. Identical models are ignored, and updates
	// are spaced out to respect Discord's rate limit (5 updates / 20 seconds).
	void SetPresence(const PresenceModel& model);

	// Pumps SDK callbacks, (re)connects to Discord and flushes queued updates.
	// Must be called frequently and always from the same thread.
	void Tick();

	// Removes the presence and waits briefly for Discord to acknowledge it.
	void Shutdown();

private:
	bool Connect();
	void Disconnect(const char* reason);
	void Flush();

	std::int64_t clientId_;
	bool steam_;
	std::unique_ptr<discord::Core> core_;

	PresenceModel desired_;
	PresenceModel sent_;
	bool haveDesired_ = false;
	bool haveSent_ = false;
	bool inFlight_ = false;

	ULONGLONG lastSend_ = 0;
	ULONGLONG nextConnect_ = 0;
};

} // namespace crpc

#endif // CRPC_ACTIVITY_H
