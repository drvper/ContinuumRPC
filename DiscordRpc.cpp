// DiscordRpc.cpp - see DiscordRpc.h
#include "DiscordRpc.h"

#include <cstdio>

namespace crpc {
namespace rpc {

namespace {

void PutU32(std::string& out, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
		out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

std::uint32_t GetU32(const std::string& in, std::size_t pos)
{
	std::uint32_t v = 0;
	for (int i = 0; i < 4; ++i)
		v |= static_cast<std::uint32_t>(static_cast<unsigned char>(in[pos + i])) << (8 * i);
	return v;
}

// Discord rejects text fields shorter than 2 characters
bool Usable(const std::string& s) { return Utf8Length(s) >= 2; }

void AddString(std::string& json, bool& first, const char* key, const std::string& value)
{
	if (!Usable(value))
		return;
	json += first ? "" : ",";
	first = false;
	json += "\"";
	json += key;
	json += "\":\"";
	json += JsonEscape(value);
	json += "\"";
}

} // namespace

std::string EncodeFrame(Opcode opcode, const std::string& json)
{
	std::string out;
	out.reserve(8 + json.size());
	PutU32(out, opcode);
	PutU32(out, static_cast<std::uint32_t>(json.size()));
	out += json;
	return out;
}

long DecodeFrame(const std::string& buffer, Frame& out)
{
	if (buffer.size() < 8)
		return 0;
	std::uint32_t length = GetU32(buffer, 4);
	if (length > kMaxFrameSize)
		return -1;
	if (buffer.size() < 8 + static_cast<std::size_t>(length))
		return 0;
	out.opcode = GetU32(buffer, 0);
	out.json.assign(buffer, 8, length);
	return static_cast<long>(8 + length);
}

std::string JsonEscape(const std::string& s)
{
	std::string out;
	out.reserve(s.size() + 8);
	for (unsigned char c : s)
	{
		switch (c)
		{
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20)
			{
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			}
			else
				out.push_back(static_cast<char>(c)); // UTF-8 passes through unchanged
		}
	}
	return out;
}

std::string HandshakeJson(std::int64_t clientId)
{
	return "{\"v\":1,\"client_id\":\"" + std::to_string(clientId) + "\"}";
}

std::string SetActivityJson(const PresenceModel* m, unsigned long pid, const std::string& nonce)
{
	std::string json = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid);

	if (m)
	{
		json += ",\"activity\":{\"type\":0";
		if (m->statusDisplay >= kStatusName && m->statusDisplay <= kStatusDetails)
			json += ",\"status_display_type\":" + std::to_string(m->statusDisplay);

		bool first = false; // "type" already written
		AddString(json, first, "details", m->details);
		AddString(json, first, "state", m->state);

		if (m->startTimestamp > 0)
			json += ",\"timestamps\":{\"start\":" + std::to_string(m->startTimestamp) + "}";

		std::string assets;
		bool firstAsset = true;
		AddString(assets, firstAsset, "large_image", m->largeImage);
		AddString(assets, firstAsset, "large_text", m->largeText);
		AddString(assets, firstAsset, "small_image", m->smallImage);
		AddString(assets, firstAsset, "small_text", m->smallText);
		if (!assets.empty())
			json += ",\"assets\":{" + assets + "}";

		if (Usable(m->partyId) && m->partySize > 0 && m->partyMax >= m->partySize)
		{
			json += ",\"party\":{\"id\":\"" + JsonEscape(m->partyId) + "\",\"size\":[" +
				std::to_string(m->partySize) + "," + std::to_string(m->partyMax) + "]}";
		}

		std::string buttons;
		int count = 0;
		for (const PresenceButton& b : m->buttons)
		{
			if (count == 2 || !IsValidButton(b))
				continue;
			buttons += count++ ? "," : "";
			buttons += "{\"label\":\"" + JsonEscape(b.label) + "\",\"url\":\"" + JsonEscape(b.url) + "\"}";
		}
		if (count)
			json += ",\"buttons\":[" + buttons + "]";

		json += m->instance ? ",\"instance\":true" : ",\"instance\":false";
		json += "}";
	}

	json += "},\"nonce\":\"" + JsonEscape(nonce) + "\"}";
	return json;
}

std::string JsonStringField(const std::string& json, const char* key)
{
	const std::string needle = std::string("\"") + key + "\"";
	std::size_t pos = 0;
	while ((pos = json.find(needle, pos)) != std::string::npos)
	{
		pos += needle.size();
		while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':'))
			++pos;
		if (pos >= json.size() || json[pos] != '"')
			continue; // not a string value; keep looking

		std::string value;
		for (++pos; pos < json.size() && json[pos] != '"'; ++pos)
		{
			if (json[pos] == '\\' && pos + 1 < json.size())
			{
				char e = json[++pos];
				value.push_back(e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e);
			}
			else
				value.push_back(json[pos]);
		}
		return value;
	}
	return std::string();
}

} // namespace rpc
} // namespace crpc
