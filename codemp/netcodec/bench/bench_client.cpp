// netcodec_bench_client: the client packets recorded by the server (sv_recordClientPackets, plan step 8), decoded as
// the server does with OpenJK's own msg.cpp and coded again as the client does (the bytes must be identical), with
// where the bytes go; then each packet coded by netcodec and decoded again (the packet must come back unchanged), with
// the sizes and the coding times of both (cold: the fastest of 3 passes over every packet; warm: the fastest of 5
// codings of each packet, summed, as netcodec_bench_codec).
// --dump writes the decoded packets as text (tools/python/client_fields.py); --trace writes them as a client trace
// (test/client_trace.h) for the trainer and the tests.
// Usage: netcodec_bench_client [--dump out.txt] [--trace out.bin] <file.jkcp> ...
#include "client_trace.h"
#include "legacy/legacy_codec.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

namespace {

double NowNs() {
	return static_cast<double>(
	    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint64_t Ticks() noexcept { return __rdtsc(); }

// A decoded packet kept for the timing: its bytes and key, and what EncodeClientPacket needs.
struct Kept {
	std::vector<uint8_t> bytes;
	int32_t key = 0;
	legacy::ClientPacketView view;
	std::vector<std::string> texts;
	std::vector<legacy::Command> commands;
	std::vector<uint8_t> userCmds;
};

// The file of sv_recordClientPackets (codemp/server/sv_netcodec.cpp: ClientPacketFileHeader, ClientPacketRecord).
struct FileHeader {
	std::array<char, 4> magic;
	int32_t version;
	int32_t recordSize;
};

struct Record {
	int32_t svTime;
	int32_t realTime;
	int32_t key;
	int32_t snapTime;
	int32_t commandTime;
	std::array<float, 3> viewangles;
	std::array<int32_t, 3> deltaAngles;
	int32_t weapon;
	int32_t forcePowerSelected;
	int32_t holdableItem;
	int32_t length;
};
static_assert(sizeof(Record) == 15 * 4);

struct Totals {
	uint64_t packets = 0, notDecoded = 0, codedDifferently = 0, bytes = 0;
	uint64_t headerBits = 0, commandBits = 0, moveBits = 0;
	uint64_t withoutMove = 0, userCmds = 0, newUserCmds = 0;
	uint64_t withCommands = 0, newCommands = 0, withSnapshot = 0;
	double seconds = 0;

	void Add(const Totals &t) {
		packets += t.packets; notDecoded += t.notDecoded; codedDifferently += t.codedDifferently; bytes += t.bytes;
		headerBits += t.headerBits; commandBits += t.commandBits; moveBits += t.moveBits;
		withoutMove += t.withoutMove; userCmds += t.userCmds; newUserCmds += t.newUserCmds;
		withCommands += t.withCommands; newCommands += t.newCommands; withSnapshot += t.withSnapshot;
		seconds += t.seconds;
	}
};

void Print(const char *name, const Totals &t) {
	const double n = t.packets ? static_cast<double>(t.packets) : 1.0;
	std::printf("%s\n", name);
	std::printf("  %" PRIu64 " packets in %.0f s (%.1f per second), %" PRIu64 " not decoded, %" PRIu64 " coded again differently\n",
	            t.packets, t.seconds, static_cast<double>(t.packets) / (t.seconds > 0 ? t.seconds : 1.0), t.notDecoded,
	            t.codedDifferently);
	std::printf("  %.2f bytes per packet (acknowledges %.2f, client commands %.2f, user commands and end %.2f, last byte unused %.2f), "
	            "%.0f bytes per second\n",
	            static_cast<double>(t.bytes) / n, static_cast<double>(t.headerBits) / 8 / n,
	            static_cast<double>(t.commandBits) / 8 / n, static_cast<double>(t.moveBits) / 8 / n,
	            static_cast<double>(t.bytes * 8 - t.headerBits - t.commandBits - t.moveBits) / 8 / n,
	            static_cast<double>(t.bytes) / (t.seconds > 0 ? t.seconds : 1.0));
	std::printf("  user commands: %.2f per packet, %.2f of them new; packets without: %" PRIu64 "\n",
	            static_cast<double>(t.userCmds) / n, static_cast<double>(t.newUserCmds) / n, t.withoutMove);
	std::printf("  client commands: %" PRIu64 " new, in %" PRIu64 " packets; acknowledged snapshot known: %.1f %%\n",
	            t.newCommands, t.withCommands, 100.0 * static_cast<double>(t.withSnapshot) / n);
}

// --dump: each decoded packet as tab-separated text lines, for the measurements of the format (tools/python):
// P svTime realTime snapTime commandTime viewangles[3] deltaAngles[3] weapon forcePowerSelected holdableItem serverId
//   messageAck reliableAck move(0 none, 1 delta, 2 no delta) headerBits commandBits moveBits length
// U serverTime angles[3] buttons weapon forcesel invensel generic_cmd forwardmove rightmove upmove  (one per user command)
// C seq text  (one per client command; tab, newline and backslash written as \t, \n, \\)
void Dump(std::FILE *f, const Record &rec, const legacy::ClientPacketOut &out) {
	std::fprintf(f, "P\t%d\t%d\t%d\t%d\t%.9g\t%.9g\t%.9g\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", rec.svTime,
	             rec.realTime, rec.snapTime, rec.commandTime, static_cast<double>(rec.viewangles[0]),
	             static_cast<double>(rec.viewangles[1]), static_cast<double>(rec.viewangles[2]), rec.deltaAngles[0],
	             rec.deltaAngles[1], rec.deltaAngles[2], rec.weapon, rec.forcePowerSelected, rec.holdableItem, out.serverId,
	             out.messageAck, out.reliableAck, static_cast<int>(out.move), out.headerBits, out.commandBits, out.moveBits,
	             rec.length);
	for (int i = 0; i < out.numUserCmds; i++) {
		const uint8_t *u = out.userCmds + static_cast<size_t>(i) * legacy::kUserCmdBytes;
		std::array<int32_t, 5> v{};   // serverTime, angles[3], buttons
		std::memcpy(v.data(), u, sizeof v);
		std::fprintf(f, "U\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", v[0], v[1], v[2], v[3], v[4], u[20], u[21], u[22],
		             u[23], static_cast<int8_t>(u[24]), static_cast<int8_t>(u[25]), static_cast<int8_t>(u[26]));
	}
	for (int i = 0; i < out.numCommands; i++) {
		std::fprintf(f, "C\t%d\t", out.commands[i].seq);
		for (const char *c = out.commands[i].text; *c; c++) {
			if (*c == '\t') std::fputs("\\t", f);
			else if (*c == '\n') std::fputs("\\n", f);
			else if (*c == '\\') std::fputs("\\\\", f);
			else std::fputc(*c, f);
		}
		std::fputc('\n', f);
	}
}

bool ReadFile(const char *path, Totals &t, legacy::ClientPacketOut &out, std::FILE *dump, std::vector<Kept> &kept,
              netcodec::trace::ClientTraceStream &stream) {
	std::ifstream f(path, std::ios::binary);
	const std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	FileHeader header{};
	if (data.size() < sizeof header) { std::fprintf(stderr, "%s: too short\n", path); return false; }
	std::memcpy(&header, data.data(), sizeof header);
	if (std::memcmp(header.magic.data(), "JKCP", 4) != 0 || header.version != 1 || header.recordSize != sizeof(Record)) {
		std::fprintf(stderr, "%s: not a version 1 client packet file\n", path);
		return false;
	}
	std::vector<uint8_t> coded(1 << 16);
	int32_t firstTime = 0, lastTime = 0, lastServerTime = 0, lastCommand = 0, prevReliableAck = 0, prevServerId = 0;
	size_t pos = sizeof header;
	while (pos + sizeof(Record) <= data.size()) {
		Record rec{};
		std::memcpy(&rec, data.data() + pos, sizeof rec);
		pos += sizeof rec;
		if (rec.length < 0 || pos + static_cast<size_t>(rec.length) > data.size()) break;  // file cut while writing
		const auto *bytes = reinterpret_cast<const uint8_t *>(data.data() + pos);
		pos += static_cast<size_t>(rec.length);
		if (!t.packets) firstTime = rec.realTime;
		lastTime = rec.realTime;
		t.packets++;
		t.bytes += static_cast<uint64_t>(rec.length);
		t.withSnapshot += rec.snapTime != -1 ? 1 : 0;
		if (!legacy::DecodeClientPacket(bytes, rec.length, rec.key, out)) { t.notDecoded++; continue; }

		legacy::ClientPacketView view;
		view.serverId = out.serverId;
		view.messageAck = out.messageAck;
		view.reliableAck = out.reliableAck;
		view.commands = out.commands;
		view.numCommands = out.numCommands;
		view.move = out.move;
		view.userCmds = out.userCmds;
		view.numUserCmds = out.numUserCmds;
		view.key = rec.key;
		// the size is bit / 8 + 1 (msg.cpp), so a packet ending on a byte boundary carries one byte nobody wrote (on the
		// client, whatever its stack held): only the bytes holding bits are compared
		const int size = legacy::EncodeClientPacket(view, coded.data(), static_cast<int>(coded.size()));
		const int bits = out.headerBits + out.commandBits + out.moveBits;
		if (size != rec.length || std::memcmp(coded.data(), bytes, static_cast<size_t>((bits + 7) / 8)) != 0) t.codedDifferently++;
		if (dump) Dump(dump, rec, out);
		Kept &k = kept.emplace_back();
		k.bytes.assign(bytes, bytes + rec.length);
		k.key = rec.key;
		k.view = view;
		for (int i = 0; i < out.numCommands; i++) k.texts.emplace_back(out.commands[i].text);
		k.userCmds.assign(out.userCmds, out.userCmds + static_cast<size_t>(out.numUserCmds) * legacy::kUserCmdBytes);

		// the packet for netcodec, with its base: the recorded fields of the acknowledged snapshot, and its record
		// approximated (client_trace.h)
		netcodec::trace::ClientTracePacket &tp = stream.packets.emplace_back();
		tp.hasBase = rec.snapTime != -1;
		tp.commandTime = rec.commandTime;
		tp.viewangles = rec.viewangles;
		tp.deltaAngles = rec.deltaAngles;
		tp.weapon = rec.weapon;
		tp.forcePowerSelected = rec.forcePowerSelected;
		tp.holdableItem = rec.holdableItem;
		tp.baseCmdSeq = prevReliableAck;
		tp.baseAck = lastCommand;
		tp.baseServerId = prevServerId;
		tp.legacyBytes = rec.length;
		tp.legacyBits = bits;
		netcodec::ClientPacket &np = tp.packet;
		np.serverId = out.serverId;
		np.ack = out.messageAck;
		np.reliableAck = out.reliableAck;
		for (int i = 0; i < out.numCommands; i++) np.commands.push_back({ .seq = out.commands[i].seq, .text = out.commands[i].text });
		np.move = static_cast<netcodec::Move>(out.move);
		for (int i = 0; i < out.numUserCmds; i++) {
			netcodec::UserCmd &u = np.userCmds.emplace_back();
			std::memcpy(u.data(), out.userCmds + static_cast<size_t>(i) * legacy::kUserCmdBytes, u.size());
		}
		prevReliableAck = out.reliableAck;
		prevServerId = out.serverId;

		t.headerBits += static_cast<uint64_t>(out.headerBits);
		t.commandBits += static_cast<uint64_t>(out.commandBits);
		t.moveBits += static_cast<uint64_t>(out.moveBits);
		t.withoutMove += out.move == legacy::Move::kNone ? 1 : 0;
		t.userCmds += static_cast<uint64_t>(out.numUserCmds);
		for (int i = 0; i < out.numUserCmds; i++) {
			int32_t serverTime = 0;   // the first field of usercmd_t
			std::memcpy(&serverTime, out.userCmds + static_cast<size_t>(i) * legacy::kUserCmdBytes, sizeof serverTime);
			if (serverTime > lastServerTime) { t.newUserCmds++; lastServerTime = serverTime; }
		}
		t.withCommands += out.numCommands ? 1 : 0;
		for (int i = 0; i < out.numCommands; i++) {
			if (out.commands[i].seq > lastCommand) { t.newCommands++; lastCommand = out.commands[i].seq; }
		}
	}
	t.seconds = (lastTime - firstTime) / 1000.0;
	return true;
}

void Time(std::vector<Kept> &kept, legacy::ClientPacketOut &out, double &warmEncode, double &warmDecode) {
	for (Kept &k : kept) {   // the views point into the kept storage, now that it no longer moves
		for (size_t i = 0; i < k.texts.size(); i++) k.commands.push_back({ k.view.commands[i].seq, k.texts[i].c_str() });
		k.view.commands = k.commands.data();
		k.view.userCmds = k.userCmds.data();
	}
	std::vector<uint8_t> buf(1 << 16);
	size_t sink = 0;
	const auto encode = [&](const Kept &k) {
		sink += static_cast<size_t>(legacy::EncodeClientPacket(k.view, buf.data(), static_cast<int>(buf.size())));
	};
	const auto decode = [&](const Kept &k) {
		sink += legacy::DecodeClientPacket(k.bytes.data(), static_cast<int>(k.bytes.size()), k.key, out) ? 1U : 0U;
	};
	const auto cold = [&](auto code) {
		double best = 1e300;
		for (int r = 0; r < 3; r++) {
			const double t0 = NowNs();
			for (const Kept &k : kept) code(k);
			best = std::min(best, NowNs() - t0);
		}
		return best;
	};
	const auto warm = [&](auto code) {
		double best = 1e300;
		for (int r = 0; r < 3; r++) {
			uint64_t sum = 0;
			const double t0 = NowNs();
			const uint64_t c0 = Ticks();
			for (const Kept &k : kept) {
				uint64_t fastest = ~uint64_t{ 0 };
				for (int i = 0; i < 5; i++) {
					const uint64_t s = Ticks();
					code(k);
					fastest = std::min(fastest, Ticks() - s);
				}
				sum += fastest;
			}
			const double nsPerTick = (NowNs() - t0) / static_cast<double>(Ticks() - c0);
			best = std::min(best, static_cast<double>(sum) * nsPerTick);
		}
		return best;
	};
	const double n = static_cast<double>(kept.size());
	const double ce = cold(encode), we = warm(encode), cd = cold(decode), wd = warm(decode);
	std::printf("legacy coding time, %zu packets: encode %.1f ns per packet (warm %.1f), decode %.1f (warm %.1f)%s\n", kept.size(),
	            ce / n, we / n, cd / n, wd / n, sink ? "" : " ");
	warmEncode = we / n;
	warmDecode = wd / n;
}

// Finds the base of one packet (the server holds exactly the acknowledged message).
struct OneBase final : netcodec::ClientBaseLookup {
	const netcodec::ClientBase *base = nullptr;
	int64_t seq = 0;
	[[nodiscard]] bool Find(int64_t s, netcodec::ClientBase &b) const override {
		if (base == nullptr || s != seq) return false;
		b = *base;
		return true;
	}
};

bool SamePacket(const netcodec::ClientPacket &a, const netcodec::ClientPacket &b) {
	if (a.serverId != b.serverId || a.ack != b.ack || a.reliableAck != b.reliableAck || a.move != b.move ||
	    a.commands.size() != b.commands.size() || a.userCmds != b.userCmds)
		return false;
	for (size_t i = 0; i < a.commands.size(); i++)
		if (a.commands[i].seq != b.commands[i].seq || a.commands[i].text != b.commands[i].text) return false;
	return true;
}

// netcodec on every packet: sizes per stream against the legacy bytes, round trip, coding times
int Netcodec(const std::vector<netcodec::trace::ClientTraceStream> &streams, double legacyEncode, double legacyDecode) {
	struct Item {
		const netcodec::trace::ClientTracePacket *t;
		netcodec::HeaderRecord header;
		netcodec::PlayerState ps;
		netcodec::ClientBase base;
		std::vector<uint8_t> coded;
	};
	std::vector<Item> items;
	for (const auto &s : streams)
		for (const auto &t : s.packets) {
			Item &it = items.emplace_back();
			it.t = &t;
			t.FillBase(it.header, it.ps);
		}
	netcodec::ClientPacketEncoder enc;
	netcodec::ClientPacket dp;
	OneBase lookup;
	size_t failures = 0, idx = 0;
	uint64_t allNew = 0, allLegacy = 0;
	for (const auto &s : streams) {
		uint64_t bytesNew = 0, bytesLegacy = 0;
		for (size_t i = 0; i < s.packets.size(); i++, idx++) {
			Item &it = items[idx];
			it.base = netcodec::ClientBase{ .header = &it.header, .ps = it.ps };
			const netcodec::ClientBase *base = it.t->hasBase ? &it.base : nullptr;
			if (!enc.Encode(it.t->packet, base, 1 << 16, it.coded)) { failures++; continue; }
			lookup.base = base;
			lookup.seq = it.t->packet.ack;
			if (!netcodec::DecodeClientPacket(it.coded, it.t->packet.ack + 2, lookup, dp) || !SamePacket(dp, it.t->packet)) failures++;
			bytesNew += it.coded.size();
			bytesLegacy += static_cast<uint64_t>(it.t->legacyBytes);
		}
		const double n = s.packets.empty() ? 1.0 : static_cast<double>(s.packets.size());
		std::printf("netcodec %s: %.2f bytes per packet, legacy %.2f (ratio %.3f)\n", s.name.c_str(),
		            static_cast<double>(bytesNew) / n, static_cast<double>(bytesLegacy) / n,
		            static_cast<double>(bytesNew) / static_cast<double>(bytesLegacy > 0 ? bytesLegacy : 1));
		allNew += bytesNew;
		allLegacy += bytesLegacy;
	}
	const double n = items.empty() ? 1.0 : static_cast<double>(items.size());
	std::printf("netcodec all: %zu packets, %.2f bytes per packet, legacy %.2f (ratio %.3f), %zu failures\n", items.size(),
	            static_cast<double>(allNew) / n, static_cast<double>(allLegacy) / n,
	            static_cast<double>(allNew) / static_cast<double>(allLegacy > 0 ? allLegacy : 1), failures);

	std::vector<uint8_t> out;
	size_t sink = 0;
	const auto encode = [&](const Item &it) {
		sink += enc.Encode(it.t->packet, it.t->hasBase ? &it.base : nullptr, 1 << 16, out) ? out.size() : 0;
	};
	const auto decode = [&](const Item &it) {
		lookup.base = it.t->hasBase ? &it.base : nullptr;
		lookup.seq = it.t->packet.ack;
		sink += netcodec::DecodeClientPacket(it.coded, it.t->packet.ack + 2, lookup, dp) ? dp.userCmds.size() : 0;
	};
	const auto cold = [&](auto code) {
		double best = 1e300;
		for (int r = 0; r < 3; r++) {
			const double t0 = NowNs();
			for (const Item &it : items) code(it);
			best = std::min(best, NowNs() - t0);
		}
		return best;
	};
	const auto warm = [&](auto code) {
		double best = 1e300;
		for (int r = 0; r < 3; r++) {
			uint64_t sum = 0;
			const double t0 = NowNs();
			const uint64_t c0 = Ticks();
			for (const Item &it : items) {
				uint64_t fastest = ~uint64_t{ 0 };
				for (int k = 0; k < 5; k++) {
					const uint64_t st = Ticks();
					code(it);
					fastest = std::min(fastest, Ticks() - st);
				}
				sum += fastest;
			}
			const double nsPerTick = (NowNs() - t0) / static_cast<double>(Ticks() - c0);
			best = std::min(best, static_cast<double>(sum) * nsPerTick);
		}
		return best;
	};
	const double ce = cold(encode), we = warm(encode), cd = cold(decode), wd = warm(decode);
	std::printf("netcodec coding time: encode %.1f ns per packet (warm %.1f), decode %.1f (warm %.1f)%s\n", ce / n, we / n,
	            cd / n, wd / n, sink ? "" : " ");
	std::printf("netcodec / legacy (warm): encode %.3f, decode %.3f\n", we / n / legacyEncode, wd / n / legacyDecode);
	return failures != 0 ? 1 : 0;
}

} // namespace

int main(int argc, char **argv) {
	int first = 1;
	std::FILE *dump = nullptr;
	const char *tracePath = nullptr;
	while (first + 1 < argc && argv[first][0] == '-') {
		if (std::strcmp(argv[first], "--dump") == 0) {
			dump = std::fopen(argv[first + 1], "w");
			if (!dump) { std::fprintf(stderr, "cannot write %s\n", argv[first + 1]); return 2; }
		} else if (std::strcmp(argv[first], "--trace") == 0) {
			tracePath = argv[first + 1];
		} else {
			break;
		}
		first += 2;
	}
	if (argc <= first) { std::fprintf(stderr, "usage: %s [--dump out.txt] [--trace out.bin] file.jkcp ...\n", argv[0]); return 2; }
	const auto out = std::make_unique<legacy::ClientPacketOut>();
	Totals all;
	std::vector<Kept> kept;
	std::vector<netcodec::trace::ClientTraceStream> streams;
	for (int a = first; a < argc; a++) {
		Totals t;
		if (dump) std::fprintf(dump, "F\t%s\n", argv[a]);
		netcodec::trace::ClientTraceStream &stream = streams.emplace_back();
		const char *slash = std::strrchr(argv[a], '/');
		stream.name = slash != nullptr ? slash + 1 : argv[a];
		if (!ReadFile(argv[a], t, *out, dump, kept, stream)) return 2;
		Print(argv[a], t);
		all.Add(t);
	}
	if (argc - first > 1) Print("all files", all);
	if (dump) std::fclose(dump);
	if (tracePath != nullptr && !netcodec::trace::WriteClientTrace(tracePath, streams)) return 2;
	double legacyEncode = 0, legacyDecode = 0;
	Time(kept, *out, legacyEncode, legacyDecode);
	const int nc = Netcodec(streams, legacyEncode, legacyDecode);
	return all.notDecoded || all.codedDifferently || nc != 0 ? 1 : 0;
}
