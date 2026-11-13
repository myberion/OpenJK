// Client packets (plan step 8): round trip of the format, with the hash of the coded bytes.
//  - synthetic: random packets on every path (with and without base, serverId changed, commands, time steps beyond
//    the escape, any angle and byte), extreme values included; needs no trace;
//  - traces: the client traces (test/client_trace.h), each packet coded against its acknowledged snapshot when the
//    recording had it, then every packet again without a base; both also through the engine's interface (engine.h),
//    which must give the same bytes and the same packet, and must refuse a packet whose base it no longer holds.
// A packet must come back unchanged (angles and buttons: their low 16 bits, as the format sends them).
// Usage: netcodec_test_client --hash=0x... synthetic <packets>
//        netcodec_test_client --hash=0x... <client trace> ...
#include "client_synthetic.h"
#include "client_trace.h"
#include "fnv.h"

#include "netcodec/engine.h"

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace netcodec;
using netcodec::test::OneBase;
using netcodec::test::SamePacket;
using netcodec::test::SyntheticPacket;

namespace {

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
		if (!DecodeClientPacket(out, latest, lookup, back) || !SamePacket(back, p)) failures++;
	}
};

// The same packet through engine.h: the client codes it against PacketBaseOf its frame of the acknowledged message,
// the server decodes it against its window of kAckWindow PacketBases.
struct EngineRun {
	[[nodiscard]] static engine::PacketBase NoMessage() noexcept {
		engine::PacketBase b{};
		b.seq = -1;
		return b;
	}

	engine::ClientCodec client;
	engine::ServerCodec server;
	std::vector<engine::PacketBase> bases = std::vector<engine::PacketBase>(engine::kAckWindow, NoMessage());
	std::vector<engine::Command> commands;
	std::array<uint8_t, 1 << 16> out{};
	size_t packets = 0, failures = 0;

	[[nodiscard]] static bool Same(const engine::ClientPacket &a, const ClientPacket &b) {
		if (a.serverId != b.serverId || a.ack != b.ack || a.reliableAck != b.reliableAck ||
		    a.move != static_cast<engine::Move>(b.move) || a.numCommands != b.commands.size() || a.numUserCmds != b.userCmds.size())
			return false;
		const std::span<const engine::Command> c(a.commands, a.numCommands);
		for (size_t i = 0; i < c.size(); i++)
			if (c[i].seq != b.commands[i].seq || std::string_view(c[i].text, c[i].length) != b.commands[i].text) return false;
		return b.userCmds.empty() || std::memcmp(a.userCmds, b.userCmds.data(), b.userCmds.size() * kUserCmdBytes) == 0;
	}

	// expected: the library's bytes for p
	void Check(const ClientPacket &p, const HeaderRecord *h, const PlayerState &ps, int64_t latest, std::span<const uint8_t> expected) {
		packets++;
		commands.clear();
		for (const ClientCommand &c : p.commands)
			commands.push_back(engine::Command{ .seq = static_cast<int32_t>(c.seq), .text = c.text.c_str(), .length = ToU32(c.text.size()) });
		const engine::ClientPacket ep{ .serverId = static_cast<int32_t>(p.serverId), .ack = static_cast<int32_t>(p.ack),
		                               .reliableAck = static_cast<int32_t>(p.reliableAck), .commands = commands.data(),
		                               .numCommands = ToU32(commands.size()), .move = static_cast<engine::Move>(p.move),
		                               .userCmds = p.userCmds.data(), .numUserCmds = ToU32(p.userCmds.size()) };
		engine::PacketBase base = NoMessage();
		engine::PacketBase &slot = bases[static_cast<size_t>(p.ack) % engine::kAckWindow];
		if (h != nullptr) {
			const engine::Frame frame{ .seq = static_cast<int32_t>(p.ack), .ps = ps.data(), .vps = nullptr, .firstEntity = 0,
			                           .numEntities = 0, .index = nullptr,
			                           .record = { .ack = h->ack, .cmdSeq = h->cmdSeq, .time = 0, .olderSeq = -1,
			                                       .serverId = static_cast<int32_t>(h->serverId) } };
			base = engine::PacketBaseOf(frame);
			slot = base;
		}
		const size_t n = client.EncodePacket(ep, h != nullptr ? &base : nullptr, out.data(), out.size());
		engine::ClientPacket back{};
		if (n == 0 || !std::ranges::equal(std::span<const uint8_t>(out.data(), n), expected) ||
		    !server.DecodePacket(out.data(), n, static_cast<int32_t>(latest), bases.data(), back) || !Same(back, p))
			failures++;
		if (h != nullptr) {
			slot.seq = static_cast<int32_t>(p.ack) + static_cast<int32_t>(engine::kAckWindow);   // a later message in its slot
			if (server.DecodePacket(out.data(), n, static_cast<int32_t>(latest), bases.data(), back)) failures++;
			slot.seq = -1;
		}
	}
};

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
			SyntheticPacket(r, p, h, ps);
			const ClientBase base = ClientBaseOf(h, ps);
			// drawn one after the other: the order of a call's arguments is unspecified (GCC and clang differ)
			const bool withBase = r.Below(4) != 0;
			const int64_t latest = p.ack + r.Range(0, 255);
			run.Check(p, withBase ? &base : nullptr, latest);
		}
		std::printf("client packets, synthetic: %zu packets, %zu bytes, failures %zu\n", run.packets, run.bytes, run.failures);
	} else {
		size_t withBase = 0;
		EngineRun eng;
		for (int a = 2; a < argc; a++) {
			std::vector<trace::ClientTraceStream> streams;
			if (!trace::ReadClientTrace(argv[a], streams)) return 2;
			HeaderRecord h;
			PlayerState ps{};
			for (const trace::ClientTraceStream &s : streams)
				for (const trace::ClientTracePacket &t : s.packets) {
					t.FillBase(h, ps);
					const ClientBase base = ClientBaseOf(h, ps);
					run.Check(t.packet, t.hasBase ? &base : nullptr, t.packet.ack + 2);
					eng.Check(t.packet, t.hasBase ? &h : nullptr, ps, t.packet.ack + 2, run.out);
					run.Check(t.packet, nullptr, t.packet.ack + 255);
					eng.Check(t.packet, nullptr, ps, t.packet.ack + 255, run.out);
					withBase += t.hasBase ? 1 : 0;
				}
		}
		std::printf("client packets, traces: %zu codings (%zu with a base), %zu bytes, failures %zu; through engine.h: %zu codings, "
		            "failures %zu\n", run.packets, withBase, run.bytes, run.failures, eng.packets, eng.failures);
		run.failures += eng.failures;
	}
	std::printf("hash of the coded bytes 0x%016" PRIX64 ", expected 0x%016" PRIX64 " (%s)\n", run.fnv.h, expected,
	            run.fnv.h == expected ? "ok" : "MISMATCH");
	return run.failures == 0 && run.fnv.h == expected ? 0 : 1;
}
