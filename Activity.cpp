// Activity.cpp - see Activity.h
#include "Activity.h"
#include "Config.h"

namespace crpc {

namespace {

constexpr ULONGLONG kMinUpdateIntervalMs = 4000;   // 5 updates per 20 s
constexpr ULONGLONG kReconnectIntervalMs = 15000;
constexpr std::int64_t kSteamAppId = 352700;

discord::Activity ToActivity(const PresenceModel& m)
{
	discord::Activity a{};
	a.SetType(discord::ActivityType::Playing);
	a.SetDetails(m.details.c_str());
	a.SetState(m.state.c_str());
	a.GetAssets().SetLargeImage(m.largeImage.c_str());
	a.GetAssets().SetLargeText(m.largeText.c_str());
	a.GetAssets().SetSmallImage(m.smallImage.c_str());
	a.GetAssets().SetSmallText(m.smallText.c_str());
	a.GetTimestamps().SetStart(m.startTimestamp);
	if (!m.partyId.empty() && m.partySize > 0 && m.partyMax >= m.partySize)
	{
		a.GetParty().SetId(m.partyId.c_str());
		a.GetParty().GetSize().SetCurrentSize(m.partySize);
		a.GetParty().GetSize().SetMaxSize(m.partyMax);
	}
	a.SetInstance(m.instance);
	return a;
}

} // namespace

DiscordClient::DiscordClient(std::int64_t clientId, bool steam) : clientId_(clientId), steam_(steam)
{
	Connect();
}

DiscordClient::~DiscordClient()
{
	core_.reset();
}

bool DiscordClient::Connect()
{
	nextConnect_ = GetTickCount64() + kReconnectIntervalMs;

	discord::Core* core = nullptr;
	discord::Result result = discord::Core::Create(clientId_, DiscordCreateFlags_NoRequireDiscord, &core);
	if (result != discord::Result::Ok || !core)
	{
		// Discord is probably not running; try again later
		Log("discord: connect failed (%d)", static_cast<int>(result));
		return false;
	}
	core_.reset(core);

	core_->SetLogHook(discord::LogLevel::Warn, [](discord::LogLevel level, const char* message) {
		Log("discord sdk [%d]: %s", static_cast<int>(level), message);
	});

	// Lets Discord launch the game (e.g. from the game library)
	if (steam_)
		core_->ActivityManager().RegisterSteam(kSteamAppId);
	else
	{
		wchar_t path[MAX_PATH];
		DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
		std::string command = "\"" + WideToUtf8(std::wstring(path, len)) + "\"";
		core_->ActivityManager().RegisterCommand(command.c_str());
	}

	Log("discord: connected");
	haveSent_ = false; // the new connection has no activity yet
	inFlight_ = false;
	return true;
}

void DiscordClient::Disconnect(const char* reason)
{
	Log("discord: disconnected (%s)", reason);
	core_.reset();
	haveSent_ = false;
	inFlight_ = false;
	nextConnect_ = GetTickCount64() + kReconnectIntervalMs;
}

void DiscordClient::SetPresence(const PresenceModel& model)
{
	desired_ = model;
	haveDesired_ = true;
}

void DiscordClient::Flush()
{
	if (!core_ || !haveDesired_ || inFlight_)
		return;
	if (haveSent_ && desired_ == sent_)
		return;

	ULONGLONG now = GetTickCount64();
	if (lastSend_ && now - lastSend_ < kMinUpdateIntervalMs)
		return; // keep the latest model queued until the rate limit allows it

	lastSend_ = now;
	inFlight_ = true;
	PresenceModel sending = desired_;
	core_->ActivityManager().UpdateActivity(ToActivity(sending), [this, sending](discord::Result result) {
		inFlight_ = false;
		if (result == discord::Result::Ok)
		{
			sent_ = sending;
			haveSent_ = true;
		}
		else
		{
			Log("discord: UpdateActivity failed (%d)", static_cast<int>(result));
			haveSent_ = false; // retried after the rate limit interval
		}
	});
}

void DiscordClient::Tick()
{
	if (!core_)
	{
		if (GetTickCount64() >= nextConnect_)
			Connect();
		if (!core_)
			return;
	}

	discord::Result result = core_->RunCallbacks();
	if (result != discord::Result::Ok)
	{
		Disconnect(result == discord::Result::NotRunning ? "Discord closed" : "callback error");
		return;
	}
	Flush();
}

void DiscordClient::Shutdown()
{
	if (!core_)
		return;

	bool done = false;
	core_->ActivityManager().ClearActivity([&done](discord::Result) { done = true; });
	for (int i = 0; i < 20 && !done; ++i)
	{
		if (core_->RunCallbacks() != discord::Result::Ok)
			break;
		Sleep(25);
	}
	core_.reset();
}

} // namespace crpc
