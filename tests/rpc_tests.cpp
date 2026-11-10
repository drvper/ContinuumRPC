// Unit tests for the Discord RPC protocol helpers (DiscordRpc.cpp).
//
// Build & run (any platform):
//   g++ -std=c++17 -I. tests/rpc_tests.cpp DiscordRpc.cpp Presence.cpp -o rpc_tests && ./rpc_tests
#include "DiscordRpc.h"

#include <cstdio>
#include <string>

using namespace crpc;

static int g_failures = 0;

#define EXPECT_EQ(a, b) do { \
	auto _a = (a); auto _b = (b); \
	if (!(_a == _b)) { ++g_failures; std::printf("%s:%d: expected %s == %s\n", __FILE__, __LINE__, #a, #b); } \
} while (0)
#define EXPECT_TRUE(a) EXPECT_EQ(static_cast<bool>(a), true)
#define CONTAINS(hay, needle) ((hay).find(needle) != std::string::npos)

static void TestFrames()
{
	std::string f = rpc::EncodeFrame(rpc::kHandshake, "{\"v\":1}");
	EXPECT_EQ(f.size(), static_cast<std::size_t>(8 + 7));
	EXPECT_EQ(f[0], '\0');                 // opcode 0, little endian
	EXPECT_EQ(static_cast<int>(f[4]), 7);  // length 7

	// partial data -> need more
	rpc::Frame out;
	EXPECT_EQ(rpc::DecodeFrame(f.substr(0, 5), out), 0L);
	EXPECT_EQ(rpc::DecodeFrame(f.substr(0, 10), out), 0L);

	// two frames back to back
	std::string two = f + rpc::EncodeFrame(rpc::kPing, "x");
	long used = rpc::DecodeFrame(two, out);
	EXPECT_EQ(used, 15L);
	EXPECT_EQ(out.opcode, static_cast<std::uint32_t>(rpc::kHandshake));
	EXPECT_EQ(out.json, std::string("{\"v\":1}"));
	two.erase(0, static_cast<std::size_t>(used));
	EXPECT_EQ(rpc::DecodeFrame(two, out), 9L);
	EXPECT_EQ(out.opcode, static_cast<std::uint32_t>(rpc::kPing));

	// absurd length -> corrupt
	std::string bad = rpc::EncodeFrame(rpc::kFrame, "");
	bad[7] = 0x7F;
	EXPECT_EQ(rpc::DecodeFrame(bad, out), -1L);
}

static void TestHandshake()
{
	EXPECT_EQ(rpc::HandshakeJson(611578814073405465), std::string("{\"v\":1,\"client_id\":\"611578814073405465\"}"));
}

static void TestEscape()
{
	EXPECT_EQ(rpc::JsonEscape("a\"b\\c\nd"), std::string("a\\\"b\\\\c\\nd"));
	EXPECT_EQ(rpc::JsonEscape(std::string("\x01", 1)), std::string("\\u0001"));
	EXPECT_EQ(rpc::JsonEscape("caf\xC3\xA9"), std::string("caf\xC3\xA9")); // UTF-8 untouched
}

static PresenceModel Model()
{
	PresenceModel m;
	m.details = "Trench Wars";
	m.state = "Javelin \xC2\xB7 Freq 1";
	m.largeImage = "large-ss";
	m.largeText = "Map: trench.lvl";
	m.smallImage = "jav";
	m.smallText = "Playing as Javelin";
	m.partyId = "crpc:Trench Wars:trench.lvl:1";
	m.partySize = 9;
	m.partyMax = 24;
	m.startTimestamp = 1700000000;
	m.instance = true;
	m.statusDisplay = kStatusDetails;
	m.buttons.push_back({ "Play Subspace Continuum", "https://store.steampowered.com/app/352700/" });
	return m;
}

static void TestSetActivity()
{
	PresenceModel m = Model();
	std::string j = rpc::SetActivityJson(&m, 1234, "7");
	EXPECT_TRUE(CONTAINS(j, "\"cmd\":\"SET_ACTIVITY\""));
	EXPECT_TRUE(CONTAINS(j, "\"pid\":1234"));
	EXPECT_TRUE(CONTAINS(j, "\"nonce\":\"7\""));
	EXPECT_TRUE(CONTAINS(j, "\"type\":0"));
	EXPECT_TRUE(CONTAINS(j, "\"status_display_type\":2"));
	EXPECT_TRUE(CONTAINS(j, "\"details\":\"Trench Wars\""));
	EXPECT_TRUE(CONTAINS(j, "\"state\":\"Javelin \xC2\xB7 Freq 1\""));
	EXPECT_TRUE(CONTAINS(j, "\"timestamps\":{\"start\":1700000000}"));
	EXPECT_TRUE(CONTAINS(j, "\"assets\":{\"large_image\":\"large-ss\",\"large_text\":\"Map: trench.lvl\",\"small_image\":\"jav\",\"small_text\":\"Playing as Javelin\"}"));
	EXPECT_TRUE(CONTAINS(j, "\"party\":{\"id\":\"crpc:Trench Wars:trench.lvl:1\",\"size\":[9,24]}"));
	EXPECT_TRUE(CONTAINS(j, "\"buttons\":[{\"label\":\"Play Subspace Continuum\",\"url\":\"https://store.steampowered.com/app/352700/\"}]"));
	EXPECT_TRUE(CONTAINS(j, "\"instance\":true"));
	EXPECT_EQ(j.back(), '}');
	EXPECT_EQ(rpc::JsonStringField(j, "nonce"), std::string("7"));
}

static void TestDiscordRejectedValuesAreDropped()
{
	PresenceModel m = Model();
	m.state = "X";                  // Discord rejects strings shorter than 2 characters
	m.smallImage = "";
	m.smallText = "";
	m.partyMax = 3;                 // max < size is invalid
	m.startTimestamp = 0;
	m.buttons.push_back({ "", "https://example.com" });                      // empty label
	m.buttons.push_back({ "Bad", "ftp://example.com" });                     // not http(s)
	m.buttons.push_back({ std::string(33, 'L'), "https://example.com" });  // label too long
	std::string j = rpc::SetActivityJson(&m, 1, "n");
	EXPECT_TRUE(!CONTAINS(j, "\"state\""));
	EXPECT_TRUE(!CONTAINS(j, "small_image"));
	EXPECT_TRUE(!CONTAINS(j, "\"party\""));
	EXPECT_TRUE(!CONTAINS(j, "timestamps"));
	EXPECT_TRUE(!CONTAINS(j, "ftp://"));
	EXPECT_TRUE(!CONTAINS(j, "LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL"));
	EXPECT_TRUE(CONTAINS(j, "\"buttons\":[{\"label\":\"Play Subspace Continuum\""));

	// at most two buttons
	m = Model();
	m.buttons.push_back({ "Two", "https://two.example" });
	m.buttons.push_back({ "Three", "https://three.example" });
	j = rpc::SetActivityJson(&m, 1, "n");
	EXPECT_TRUE(CONTAINS(j, "https://two.example"));
	EXPECT_TRUE(!CONTAINS(j, "https://three.example"));
}

static void TestClear()
{
	EXPECT_EQ(rpc::SetActivityJson(nullptr, 42, "c"), std::string("{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":42},\"nonce\":\"c\"}"));
}

static void TestResponses()
{
	const std::string ready = "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1,\"user\":{\"username\":\"x\"}},\"evt\":\"READY\",\"nonce\":null}";
	EXPECT_EQ(rpc::JsonStringField(ready, "evt"), std::string("READY"));
	EXPECT_EQ(rpc::JsonStringField(ready, "nonce"), std::string()); // null is not a string

	const std::string err = "{\"cmd\":\"SET_ACTIVITY\",\"data\":{\"code\":4000,\"message\":\"child \\\"activity\\\" fails\"},\"evt\":\"ERROR\",\"nonce\":\"3\"}";
	EXPECT_EQ(rpc::JsonStringField(err, "evt"), std::string("ERROR"));
	EXPECT_EQ(rpc::JsonStringField(err, "nonce"), std::string("3"));
	EXPECT_EQ(rpc::JsonStringField(err, "message"), std::string("child \"activity\" fails"));
	EXPECT_EQ(rpc::JsonStringField(err, "missing"), std::string());
}

int main()
{
	TestFrames();
	TestHandshake();
	TestEscape();
	TestSetActivity();
	TestDiscordRejectedValuesAreDropped();
	TestClear();
	TestResponses();
	if (g_failures)
	{
		std::printf("%d failure(s)\n", g_failures);
		return 1;
	}
	std::printf("all rpc tests passed\n");
	return 0;
}
