// Unit tests for the platform independent presence logic (Presence.cpp).
//
// Build & run (any platform):
//   g++ -std=c++17 -I. tests/presence_tests.cpp Presence.cpp -o presence_tests && ./presence_tests
#include "Presence.h"

#include <cstdio>
#include <string>

using namespace crpc;

static int g_failures = 0;

#define EXPECT_EQ(a, b) do { \
	auto _a = (a); auto _b = (b); \
	if (!(_a == _b)) { ++g_failures; std::printf("%s:%d: expected %s == %s\n", __FILE__, __LINE__, #a, #b); } \
} while (0)
#define EXPECT_TRUE(a) EXPECT_EQ(static_cast<bool>(a), true)

static GameSnapshot InZone()
{
	GameSnapshot s;
	s.screen = Screen::InZone;
	s.focused = true;
	s.zone = "Trench Wars";
	s.memoryValid = true;
	s.playerName = "Purge";
	s.mapFile = "trench.lvl";
	s.ship = kJavelin;
	s.freq = 1;
	s.bounty = 42;
	s.flags = 0;
	s.arenaPlayers = 24;
	s.arenaPlaying = 18;
	s.teamPlayers = 9;
	return s;
}

static void TestPlayingShip()
{
	PresenceBuilder b;
	PresenceSettings cfg;
	PresenceModel m = b.Build(InZone(), cfg, 1000);
	EXPECT_EQ(m.details, std::string("Trench Wars"));
	EXPECT_EQ(m.state, std::string("Javelin \xC2\xB7 Freq 1 \xC2\xB7 Bounty 42"));
	EXPECT_EQ(m.smallImage, std::string("jav"));
	EXPECT_EQ(m.smallText, std::string("Playing as Javelin"));
	EXPECT_EQ(m.largeText, std::string("Map: trench.lvl \xC2\xB7 24 players (18 playing)"));
	EXPECT_EQ(m.partySize, 9);
	EXPECT_EQ(m.partyMax, 24);
	EXPECT_TRUE(!m.partyId.empty());
	EXPECT_TRUE(m.instance);
	EXPECT_EQ(m.startTimestamp, static_cast<std::int64_t>(1000));
}

static void TestFlagsSafeAndName()
{
	PresenceBuilder b;
	PresenceSettings cfg;
	cfg.showPlayerName = true;
	GameSnapshot s = InZone();
	s.flags = 3;
	s.inSafeZone = true;
	PresenceModel m = b.Build(s, cfg, 0);
	EXPECT_EQ(m.state, std::string("Javelin \xC2\xB7 Freq 1 \xC2\xB7 Bounty 42 \xC2\xB7 3 flags \xC2\xB7 In Safe"));
	EXPECT_EQ(m.smallText, std::string("Purge in a Javelin"));
	EXPECT_EQ(m.largeText.find("Purge"), static_cast<std::size_t>(0));
}

static void TestPrivacyOptions()
{
	PresenceBuilder b;
	PresenceSettings cfg;
	cfg.showZone = false;
	cfg.showFrequency = false;
	cfg.showBounty = false;
	cfg.showPartySize = false;
	cfg.showMap = false;
	cfg.showArenaPopulation = false;
	PresenceModel m = b.Build(InZone(), cfg, 0);
	EXPECT_EQ(m.details, std::string("In a zone"));
	EXPECT_EQ(m.state, std::string("Javelin"));
	EXPECT_EQ(m.largeText, std::string("Subspace Continuum"));
	EXPECT_TRUE(m.partyId.empty());
	EXPECT_EQ(m.partySize, 0);
	EXPECT_EQ(m.smallText.find("Purge"), std::string::npos); // name hidden by default
}

static void TestSpectatorHasNoParty()
{
	PresenceBuilder b;
	GameSnapshot s = InZone();
	s.ship = kSpectator;
	s.teamPlayers = 0;
	PresenceModel m = b.Build(s, PresenceSettings(), 0);
	EXPECT_EQ(m.state, std::string("Spectating"));
	EXPECT_EQ(m.smallImage, std::string("idle"));
	EXPECT_TRUE(m.partyId.empty());
}

static void TestChatAndIdleKeepTimer()
{
	PresenceBuilder b;
	PresenceSettings cfg;
	GameSnapshot s = InZone();
	EXPECT_EQ(b.Build(s, cfg, 100).startTimestamp, static_cast<std::int64_t>(100));

	s.screen = Screen::Chat;
	PresenceModel chat = b.Build(s, cfg, 200);
	EXPECT_EQ(chat.state, std::string("Chatting"));
	EXPECT_EQ(chat.smallImage, std::string("chat"));
	EXPECT_EQ(chat.startTimestamp, static_cast<std::int64_t>(100)); // tabbing out does not reset the timer

	s.screen = Screen::InZone;
	s.focused = false;
	PresenceModel idle = b.Build(s, cfg, 300);
	EXPECT_EQ(idle.state, std::string("Idle"));
	EXPECT_EQ(idle.smallImage, std::string("idle2"));
	EXPECT_EQ(idle.startTimestamp, static_cast<std::int64_t>(100));

	// switching ships keeps the timer unless configured otherwise
	s.focused = true;
	s.ship = kShark;
	EXPECT_EQ(b.Build(s, cfg, 400).startTimestamp, static_cast<std::int64_t>(100));
	cfg.resetTimerOnShipChange = true;
	EXPECT_EQ(b.Build(s, cfg, 500).startTimestamp, static_cast<std::int64_t>(500));

	// changing zones resets it
	cfg.resetTimerOnShipChange = false;
	s.zone = "Hyperspace";
	EXPECT_EQ(b.Build(s, cfg, 600).startTimestamp, static_cast<std::int64_t>(600));
}

static void TestLauncher()
{
	PresenceBuilder b;
	PresenceSettings cfg;
	GameSnapshot s;
	s.screen = Screen::Launcher;
	s.focused = true;
	s.skin = "Default";
	PresenceModel m = b.Build(s, cfg, 50);
	EXPECT_EQ(m.details, std::string("Selecting Zone"));
	EXPECT_EQ(m.state, std::string("Skin: Default"));
	EXPECT_EQ(m.smallImage, std::string("launcher"));
	EXPECT_EQ(m.startTimestamp, static_cast<std::int64_t>(0));
	EXPECT_TRUE(!m.instance);

	s.focused = false;
	m = b.Build(s, cfg, 60);
	EXPECT_EQ(m.state, std::string("Idle"));
	EXPECT_EQ(m.smallImage, std::string("idle2"));

	cfg.showLauncherTimer = true;
	EXPECT_EQ(b.Build(s, cfg, 70).startTimestamp, static_cast<std::int64_t>(50));
}

static void TestFallbackWithoutMemory()
{
	// memory unavailable: registry ship only, nothing memory-derived is shown
	PresenceBuilder b;
	GameSnapshot s;
	s.screen = Screen::InZone;
	s.focused = true;
	s.zone = "Trench Wars";
	s.ship = kWarbird;
	s.bounty = 99;           // stale/garbage must be ignored
	s.arenaPlayers = 5;
	PresenceModel m = b.Build(s, PresenceSettings(), 0);
	EXPECT_EQ(m.state, std::string("Warbird"));
	EXPECT_EQ(m.smallImage, std::string("playing"));
	EXPECT_EQ(m.largeText, std::string("Subspace Continuum"));
	EXPECT_TRUE(m.partyId.empty());

	s.ship = kShipUnknown;
	m = b.Build(s, PresenceSettings(), 0);
	EXPECT_EQ(m.state, std::string("Playing"));
	EXPECT_TRUE(m.smallImage.empty());
}

static void TestTruncation()
{
	EXPECT_EQ(TruncateUtf8("abc", 2), std::string("ab"));
	// "é" is 2 bytes; never cut in the middle of it
	EXPECT_EQ(TruncateUtf8("a\xC3\xA9", 2), std::string("a"));
	EXPECT_EQ(TruncateUtf8("a\xC3\xA9", 3), std::string("a\xC3\xA9"));

	PresenceBuilder b;
	GameSnapshot s = InZone();
	s.zone = std::string(300, 'z');
	PresenceModel m = b.Build(s, PresenceSettings(), 0);
	EXPECT_TRUE(m.details.size() <= kMaxActivityString);
	EXPECT_TRUE(m.partyId.size() <= kMaxActivityString);
}

static void TestEquality()
{
	PresenceBuilder b;
	PresenceModel a = b.Build(InZone(), PresenceSettings(), 10);
	PresenceModel c = b.Build(InZone(), PresenceSettings(), 20);
	EXPECT_TRUE(a == c); // same phase -> same start time -> no redundant Discord update
	GameSnapshot s = InZone();
	s.bounty = 43;
	EXPECT_TRUE(a != b.Build(s, PresenceSettings(), 30));
}

int main()
{
	TestPlayingShip();
	TestFlagsSafeAndName();
	TestPrivacyOptions();
	TestSpectatorHasNoParty();
	TestChatAndIdleKeepTimer();
	TestLauncher();
	TestFallbackWithoutMemory();
	TestTruncation();
	TestEquality();
	if (g_failures)
	{
		std::printf("%d failure(s)\n", g_failures);
		return 1;
	}
	std::printf("all presence tests passed\n");
	return 0;
}
