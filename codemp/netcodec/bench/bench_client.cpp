// netcodec_bench_client: the client packets recorded by the server (sv_recordClientPackets, plan step 8), decoded as
// the server does with OpenJK's own msg.cpp and coded again as the client does (the bytes must be identical), with
// where the bytes go.
// Usage: netcodec_bench_client <file.jkcp> ...
#include "legacy/legacy_codec.h"

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

namespace {

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

bool ReadFile(const char *path, Totals &t, legacy::ClientPacketOut &out) {
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
	int32_t firstTime = 0, lastTime = 0, lastServerTime = 0, lastCommand = 0;
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

} // namespace

int main(int argc, char **argv) {
	if (argc < 2) { std::fprintf(stderr, "usage: %s file.jkcp ...\n", argv[0]); return 2; }
	const auto out = std::make_unique<legacy::ClientPacketOut>();
	Totals all;
	for (int a = 1; a < argc; a++) {
		Totals t;
		if (!ReadFile(argv[a], t, *out)) return 2;
		Print(argv[a], t);
		all.Add(t);
	}
	if (argc > 2) Print("all files", all);
	return all.notDecoded || all.codedDifferently ? 1 : 0;
}
