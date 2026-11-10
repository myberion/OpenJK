// netcodec_train_client: trains the tables of the client packets (plan step 8) on client traces (test/client_trace.h,
// written by netcodec_bench_client --trace) and writes src/generated/jka_client_tables.inc. Only the client contexts
// are written: the client command texts are coded with the server command text tables of jka_tables.inc.
// Usage: netcodec_train_client <jka_client_tables.inc> <client_train.bin> ...
#include "client_trace.h"
#include "contexts.h"
#include "write_tables.h"

#include <cstdio>
#include <vector>

using namespace netcodec;

int main(int argc, char **argv) {
	if (argc < 3) { std::fprintf(stderr, "usage: %s jka_client_tables.inc client_train.bin ...\n", argv[0]); return 2; }
	SymbolCounts counts = NewSymbolCounts();
	size_t packets = 0, refused = 0;
	HeaderRecord header;
	PlayerState ps{};
	for (int a = 2; a < argc; a++) {
		std::vector<trace::ClientTraceStream> streams;
		if (!trace::ReadClientTrace(argv[a], streams)) return 1;
		for (const trace::ClientTraceStream &s : streams)
			for (const trace::ClientTracePacket &t : s.packets) {
				t.FillBase(header, ps);
				const ClientBase base = ClientBaseOf(header, ps);
				packets++;
				if (!CountClientPacket(t.packet, t.hasBase ? &base : nullptr, counts)) refused++;
			}
		std::printf("%s: %zu streams\n", argv[a], streams.size());
	}
	std::printf("%zu packets counted, %zu refused\n", packets, refused);
	if (refused != 0) return 1;
	const tools::TablesFile file{ .tool = "netcodec_train_client", .what = "Trained tables of the client packets",
		                          .unit = "packets", .prefix = "kClientTrained" };
	return tools::WriteTables(argv[1], counts, packets, file, detail::ctx::kClientScalar, detail::ctx::kCount) ? 0 : 1;
}
