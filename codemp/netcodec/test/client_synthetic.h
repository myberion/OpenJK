// Client packets for the tests and the fuzzer (plan step 8): a random packet with a random base on every path of the
// format (SyntheticPacket), a lookup that holds one base, and the comparison of two packets.
#pragma once

#include "synthetic.h"

#include "netcodec/codec.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace netcodec::test {

struct OneBase final : ClientBaseLookup {
	const ClientBase *base = nullptr;
	int64_t seq = 0;
	[[nodiscard]] bool Find(int64_t s, ClientBase &b) const override {
		if (base == nullptr || s != seq) return false;
		b = *base;
		return true;
	}
};

[[nodiscard]] inline bool SamePacket(const ClientPacket &a, const ClientPacket &b) {
	if (a.serverId != b.serverId || a.ack != b.ack || a.reliableAck != b.reliableAck || a.move != b.move ||
	    a.commands.size() != b.commands.size() || a.userCmds != b.userCmds)
		return false;
	for (size_t i = 0; i < a.commands.size(); i++)
		if (a.commands[i].seq != b.commands[i].seq || a.commands[i].text != b.commands[i].text) return false;
	return true;
}

inline void Put32(UserCmd &c, size_t offset, int64_t v) {
	const auto w = static_cast<int32_t>(static_cast<uint32_t>(v));
	std::memcpy(c.data() + offset, &w, 4);
}

// a random packet and base; the packet's angles and buttons hold 16 bits (what the format sends)
inline void SyntheticPacket(synth::Rng &r, ClientPacket &p, HeaderRecord &h, PlayerState &ps) {
	const auto any32 = [&r] { return r.Range(-(int64_t{ 1 } << 31), (int64_t{ 1 } << 31) - 1); };
	h = HeaderRecord{};
	h.cmdSeq = any32();
	h.ack = any32();
	h.serverId = any32();
	ps.fill(0);
	for (size_t i = 0; i < 64; i++) ps[r.Index(ps.size())] = static_cast<uint8_t>(r.Next());   // garbage elsewhere
	const auto put = [&ps](size_t offset, int64_t v) {
		const auto w = static_cast<int32_t>(static_cast<uint32_t>(v));
		std::memcpy(ps.data() + offset, &w, 4);
	};
	put(0, any32());                                                   // commandTime
	for (size_t a = 0; a < 3; a++) {
		put(80 + (4 * a), r.Range(-32768, 32767));                     // delta_angles
		const uint64_t kind = r.Below(4);                              // view angles: indices, other floats, any pattern
		const float f = kind == 0 ? static_cast<float>(static_cast<double>(r.Range(-65536, 65536)) * (360.0 / 65536))
		                          : static_cast<float>(r.Range(-100000, 100000)) / 7.0F;
		uint32_t pattern = 0;
		std::memcpy(&pattern, &f, 4);
		put(184 + (4 * a), kind == 3 ? static_cast<int64_t>(r.Next() & 0xFFFFFFFFU) : static_cast<int64_t>(pattern));
	}
	put(176, r.Range(0, 300));
	put(248, r.Range(-10, 300));
	put(884, r.Range(-10, 300));

	p = ClientPacket{};
	p.ack = r.Range(0, (int64_t{ 1 } << 31) - 1);
	p.serverId = r.Below(4) == 0 ? any32() : h.serverId;
	p.reliableAck = r.Below(4) == 0 ? any32() : h.cmdSeq + r.Range(-3, 3);
	const uint64_t numCommands = r.Below(8) == 0 ? r.Below(6) : 0;
	int64_t seq = r.Below(4) == 0 ? any32() : h.ack + 1 + r.Range(-2, 2);
	for (uint64_t k = 0; k < numCommands; k++) {
		std::string text;
		const uint64_t len = r.Below(60);
		for (uint64_t i = 0; i < len; i++) text.push_back(static_cast<char>(r.Range(1, 255)));
		p.commands.push_back(ClientCommand{ .seq = seq++, .text = std::move(text) });
	}
	p.move = static_cast<Move>(r.Below(3));
	if (p.move == Move::kNone) return;
	const uint64_t n = r.Below(4) == 0 ? 1 + r.Below(kMaxUserCmds) : 1 + r.Below(6);
	int64_t t = r.Below(4) == 0 ? any32() : static_cast<int32_t>(static_cast<uint32_t>(h.ack));
	UserCmd prev{};
	for (uint64_t i = 0; i < n; i++) {
		UserCmd c = r.Below(3) == 0 ? prev : UserCmd{};
		const uint64_t step = r.Below(8);
		t += step == 0 ? any32() : step == 1 ? r.Range(-100, 100) : r.Range(0, 20);
		Put32(c, 0, t);
		for (size_t a = 0; a < 3; a++)
			if (r.Below(2) == 0) Put32(c, 4 + (4 * a), r.Range(0, 65535));
		if (r.Below(4) == 0) Put32(c, 16, r.Range(0, 65535));
		for (size_t k = 20; k < 27; k++)
			if (r.Below(6) == 0) c[k] = static_cast<uint8_t>(r.Next());
		c[27] = 0;
		p.userCmds.push_back(c);
		prev = c;
	}
}

} // namespace netcodec::test
