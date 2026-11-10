// DiscordRpc.h - platform independent parts of Discord's local RPC protocol.
//
// The Discord client listens on a local IPC pipe (\\.\pipe\discord-ipc-N on
// Windows). Every message is a frame: a little-endian u32 opcode, a u32 length
// and that many bytes of JSON. See
// https://discord.com/developers/docs/topics/rpc#rpc-over-ipc
#ifndef CRPC_DISCORDRPC_H
#define CRPC_DISCORDRPC_H

#include <cstdint>
#include <string>

#include "Presence.h"

namespace crpc {
namespace rpc {

enum Opcode : std::uint32_t {
	kHandshake = 0,
	kFrame = 1,
	kClose = 2,
	kPing = 3,
	kPong = 4,
};

// Frames larger than this are treated as a corrupt stream.
constexpr std::uint32_t kMaxFrameSize = 64 * 1024;

struct Frame {
	std::uint32_t opcode = 0;
	std::string json;
};

std::string EncodeFrame(Opcode opcode, const std::string& json);

// Decodes one frame from the start of `buffer`. Returns the number of bytes
// consumed, 0 if more data is needed, or -1 if the stream is corrupt.
long DecodeFrame(const std::string& buffer, Frame& out);

std::string JsonEscape(const std::string& s);

// {"v":1,"client_id":"..."}
std::string HandshakeJson(std::int64_t clientId);

// SET_ACTIVITY command for `model`, or a command that clears the presence when
// `model` is null. Fields Discord would reject (strings shorter than two
// characters, an invalid party) are left out so one bad value cannot cause
// the whole update to be refused.
std::string SetActivityJson(const PresenceModel* model, unsigned long pid, const std::string& nonce);

// Value of the first "key":"value" string pair in `json` (unescaped), or "".
// Enough for the handful of fields we inspect in Discord's replies.
std::string JsonStringField(const std::string& json, const char* key);

} // namespace rpc
} // namespace crpc

#endif // CRPC_DISCORDRPC_H
