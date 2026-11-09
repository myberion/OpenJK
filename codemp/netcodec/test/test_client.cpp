// Client packets (plan step 8): round trip of the format, with the hash of the coded bytes.
//  - synthetic: random packets on every path (with and without base, serverId changed, commands, time steps beyond
//    the escape, any angle and byte), extreme values included; needs no trace;
//  - traces: the client traces (test/client_trace.h), each packet coded against its acknowledged snapshot when the
//    recording had it, then every packet again without a base.
// A packet must come back unchanged (angles and buttons: their low 16 bits, as the format sends them).
// Usage: netcodec_test_client --hash=0x... synthetic <packets>
//        netcodec_test_client --hash=0x... <client trace> ...
#include "client_trace.h"
#include "fnv.h"
#include "synthetic.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace netcodec;

namespace {

struct OneBase final : ClientBaseLookup {
	const ClientBase *base = nullptr;
	int64_t seq = 0;
	[[nodiscard]] bool Find(int64_t s, ClientBase &b) const override {
		if (base == nullptr || s != seq) return false;
		b = *base;
		return true;
	}
};

bool Same(const ClientPacket &a, const ClientPacket &b) {
	if (a.serverId != b.serverId || a.ack != b.ack || a.reliableAck != b.reliableAck || a.move != b.move ||
	    a.commands.size() != b.commands.size() || a.userCmds != b.userCmds)
		return false;
	for (size_t i = 0; i < a.commands.size(); i++)
		if (a.commands[i].seq != b.commands[i].seq || a.commands[i].text != b.commands[i].text) return false;
	return true;
}

struct Run {
	ClientPacketEncoder enc;
	std::vector<uint8_t> out;
	ClientPacket back;
	OneBase lookup;
	test::Fnv fnv;
	size_t packets = 0, bytes = 0, failures = 0;

	// latest: the server's last message, at most 255 after the acknowledge
	void Check(const ClientPacket &p, const ClientBase *base, int64_t latest) {
		packets++;
		if (!enc.Encode(p, base, 1 << 16, out)) { failures++; return; }
		bytes += out.size();
		fnv.Feed(out);
		lookup.base = base;
		lookup.seq = p.ack;
		if (!DecodeClientPacket(out, latest, lookup, back) || !Same(back, p)) failures++;
	}
};

void Put32(UserCmd &c, size_t offset, int64_t v) {
	const auto w = static_cast<int32_t>(static_cast<uint32_t>(v));
	std::memcpy(c.data() + offset, &w, 4);
}

// a random packet and base; the packet's angles and buttons hold 16 bits (what the format sends)
void Synthetic(synth::Rng &r, ClientPacket &p, HeaderRecord &h, PlayerState &ps) {
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

} // namespace

int main(int argc, char **argv) {
	if (argc < 3 || std::strncmp(argv[1], "--hash=", 7) != 0) {
		std::fprintf(stderr, "usage: %s --hash=0x... synthetic <packets> | <client trace> ...\n", argv[0]);
		return 2;
	}
	const uint64_t expected = std::strtoull(argv[1] + 7, nullptr, 16);
	Run run;
	if (std::strcmp(argv[2], "synthetic") == 0) {
		const uint64_t count = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 100000;
		synth::Rng r(1);
		ClientPacket p;
		HeaderRecord h;
		PlayerState ps{};
		for (uint64_t i = 0; i < count; i++) {
			Synthetic(r, p, h, ps);
			const ClientBase base{ .header = &h, .ps = ps };
			// drawn one after the other: the order of a call's arguments is unspecified (GCC and clang differ)
			const bool withBase = r.Below(4) != 0;
			const int64_t latest = p.ack + r.Range(0, 255);
			run.Check(p, withBase ? &base : nullptr, latest);
		}
		std::printf("client packets, synthetic: %zu packets, %zu bytes, failures %zu\n", run.packets, run.bytes, run.failures);
	} else {
		size_t withBase = 0;
		for (int a = 2; a < argc; a++) {
			std::vector<trace::ClientTraceStream> streams;
			if (!trace::ReadClientTrace(argv[a], streams)) return 2;
			HeaderRecord h;
			PlayerState ps{};
			for (const trace::ClientTraceStream &s : streams)
				for (const trace::ClientTracePacket &t : s.packets) {
					t.FillBase(h, ps);
					const ClientBase base{ .header = &h, .ps = ps };
					run.Check(t.packet, t.hasBase ? &base : nullptr, t.packet.ack + 2);
					run.Check(t.packet, nullptr, t.packet.ack + 255);
					withBase += t.hasBase ? 1 : 0;
				}
		}
		std::printf("client packets, traces: %zu codings (%zu with a base), %zu bytes, failures %zu\n", run.packets, withBase,
		            run.bytes, run.failures);
	}
	std::printf("hash of the coded bytes 0x%016" PRIX64 ", expected 0x%016" PRIX64 " (%s)\n", run.fnv.h, expected,
	            run.fnv.h == expected ? "ok" : "MISMATCH");
	return run.failures == 0 && run.fnv.h == expected ? 0 : 1;
}
