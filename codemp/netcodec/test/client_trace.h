// Client packet traces (plan step 8): the client packets the server recorded (sv_recordClientPackets), decoded with
// OpenJK's msg.cpp by netcodec_bench_client --trace, with what the codec codes them against. Not committed (they hold
// player names and chat): docs_rewrite/netcode/data/traces/client_train.bin and client_holdout.bin.
//
// File, little-endian: "JKCT", int32 version (1), int32 stream count; per stream: int32 name length, the name, int32
// packet count; per packet, int32 unless said: snapTime (-1: the server did not hold the acknowledged snapshot),
// then its player state's commandTime, viewangles[3] (float), delta_angles[3], weapon, fd.forcePowerSelected,
// stats[STAT_HOLDABLE_ITEM]; the acknowledged message's record (cmdSeq, ack, serverId); the packet: serverId, ack,
// reliableAck, move (uint8), user-command count (uint8), client-command count (uint16), the legacy size in bytes and
// in bits (without the unused bits of the last byte); per client command: seq, length (uint16), text; per user
// command: the 28-byte usercmd_t image.
//
// The recording holds the acknowledged snapshot's player-state fields but not its record: cmdSeq, ack and serverId
// are approximated by the exporter (the previous packet's reliableAck and serverId, and the last client command
// sequence of the earlier packets). They are exact in the engine; here they only weigh on the rare symbols that use
// them.
#pragma once

#include "netcodec/codec.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace netcodec::trace {

struct ClientTracePacket {
	ClientPacket packet;
	bool hasBase = false;
	int32_t commandTime = 0;
	std::array<float, 3> viewangles{};
	std::array<int32_t, 3> deltaAngles{};
	int32_t weapon = 0, forcePowerSelected = 0, holdableItem = 0;
	int64_t baseCmdSeq = 0, baseAck = 0, baseServerId = 0;
	int32_t legacyBytes = 0, legacyBits = 0;

	// the base as the codec takes it: a record and a player state holding the fields above (playerState_t offsets
	// of src/client_codec.cpp: commandTime 0, delta_angles 80, weapon 176, viewangles 184, stats[1] 248,
	// fd.forcePowerSelected 884)
	void FillBase(HeaderRecord &h, PlayerState &ps) const noexcept {
		h = HeaderRecord{};
		h.cmdSeq = baseCmdSeq;
		h.ack = baseAck;
		h.serverId = baseServerId;
		ps.fill(0);
		const auto put = [&ps](size_t offset, const void *v) { std::memcpy(ps.data() + offset, v, 4); };
		put(0, &commandTime);
		for (size_t a = 0; a < 3; a++) {
			put(80 + (4 * a), &deltaAngles[a]);
			put(184 + (4 * a), &viewangles[a]);
		}
		put(176, &weapon);
		put(248, &holdableItem);
		put(884, &forcePowerSelected);
	}
};

struct ClientTraceStream {
	std::string name;
	std::vector<ClientTracePacket> packets;
};

namespace detail_client {

struct File {
	std::unique_ptr<FILE, int (*)(FILE *)> f;
	bool ok = true;

	template <class T> void Put(T v) { ok = ok && std::fwrite(&v, sizeof v, 1, f.get()) == 1; }
	void PutBytes(const void *p, size_t n) { ok = ok && (n == 0 || std::fwrite(p, 1, n, f.get()) == n); }
	template <class T> T Get() {
		T v{};
		ok = ok && std::fread(&v, sizeof v, 1, f.get()) == 1;
		return v;
	}
	void GetBytes(void *p, size_t n) { ok = ok && (n == 0 || std::fread(p, 1, n, f.get()) == n); }
};

} // namespace detail_client

inline bool WriteClientTrace(const char *path, const std::vector<ClientTraceStream> &streams) {
	detail_client::File out{ std::unique_ptr<FILE, int (*)(FILE *)>(std::fopen(path, "wb"), &std::fclose) };
	if (!out.f) { std::fprintf(stderr, "cannot write %s\n", path); return false; }
	out.PutBytes("JKCT", 4);
	out.Put<int32_t>(1);
	out.Put(static_cast<int32_t>(streams.size()));
	for (const ClientTraceStream &s : streams) {
		out.Put(static_cast<int32_t>(s.name.size()));
		out.PutBytes(s.name.data(), s.name.size());
		out.Put(static_cast<int32_t>(s.packets.size()));
		for (const ClientTracePacket &t : s.packets) {
			out.Put<int32_t>(t.hasBase ? 0 : -1);
			out.Put(t.commandTime);
			for (const float v : t.viewangles) out.Put(v);
			for (const int32_t v : t.deltaAngles) out.Put(v);
			out.Put(t.weapon);
			out.Put(t.forcePowerSelected);
			out.Put(t.holdableItem);
			out.Put(static_cast<int32_t>(t.baseCmdSeq));
			out.Put(static_cast<int32_t>(t.baseAck));
			out.Put(static_cast<int32_t>(t.baseServerId));
			const ClientPacket &p = t.packet;
			out.Put(static_cast<int32_t>(p.serverId));
			out.Put(static_cast<int32_t>(p.ack));
			out.Put(static_cast<int32_t>(p.reliableAck));
			out.Put(static_cast<uint8_t>(p.move));
			out.Put(static_cast<uint8_t>(p.userCmds.size()));
			out.Put(static_cast<uint16_t>(p.commands.size()));
			out.Put(t.legacyBytes);
			out.Put(t.legacyBits);
			for (const ClientCommand &c : p.commands) {
				out.Put(static_cast<int32_t>(c.seq));
				out.Put(static_cast<uint16_t>(c.text.size()));
				out.PutBytes(c.text.data(), c.text.size());
			}
			for (const UserCmd &u : p.userCmds) out.PutBytes(u.data(), u.size());
		}
	}
	return out.ok;
}

inline bool ReadClientTrace(const char *path, std::vector<ClientTraceStream> &streams) {
	detail_client::File in{ std::unique_ptr<FILE, int (*)(FILE *)>(std::fopen(path, "rb"), &std::fclose) };
	if (!in.f) { std::fprintf(stderr, "cannot read %s\n", path); return false; }
	std::array<char, 4> magic{};
	in.GetBytes(magic.data(), 4);
	if (std::memcmp(magic.data(), "JKCT", 4) != 0 || in.Get<int32_t>() != 1) {
		std::fprintf(stderr, "%s: not a version 1 client trace\n", path);
		return false;
	}
	const auto numStreams = in.Get<int32_t>();
	for (int32_t si = 0; si < numStreams && in.ok; si++) {
		ClientTraceStream &s = streams.emplace_back();
		s.name.resize(static_cast<size_t>(in.Get<int32_t>()));
		in.GetBytes(s.name.data(), s.name.size());
		const auto numPackets = in.Get<int32_t>();
		s.packets.resize(static_cast<size_t>(numPackets));
		for (ClientTracePacket &t : s.packets) {
			t.hasBase = in.Get<int32_t>() != -1;
			t.commandTime = in.Get<int32_t>();
			for (float &v : t.viewangles) v = in.Get<float>();
			for (int32_t &v : t.deltaAngles) v = in.Get<int32_t>();
			t.weapon = in.Get<int32_t>();
			t.forcePowerSelected = in.Get<int32_t>();
			t.holdableItem = in.Get<int32_t>();
			t.baseCmdSeq = in.Get<int32_t>();
			t.baseAck = in.Get<int32_t>();
			t.baseServerId = in.Get<int32_t>();
			ClientPacket &p = t.packet;
			p.serverId = in.Get<int32_t>();
			p.ack = in.Get<int32_t>();
			p.reliableAck = in.Get<int32_t>();
			p.move = static_cast<Move>(in.Get<uint8_t>());
			p.userCmds.resize(in.Get<uint8_t>());
			p.commands.resize(in.Get<uint16_t>());
			t.legacyBytes = in.Get<int32_t>();
			t.legacyBits = in.Get<int32_t>();
			for (ClientCommand &c : p.commands) {
				c.seq = in.Get<int32_t>();
				c.text.resize(in.Get<uint16_t>());
				in.GetBytes(c.text.data(), c.text.size());
			}
			for (UserCmd &u : p.userCmds) in.GetBytes(u.data(), u.size());
		}
	}
	if (!in.ok) std::fprintf(stderr, "%s: truncated client trace\n", path);
	return in.ok;
}

} // namespace netcodec::trace
