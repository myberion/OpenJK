// libFuzzer target on the client packet decoder (plan step 8 part 4; preset mingw-clang-fuzz: clang, ASan + UBSan,
// every netcodec target instrumented for coverage): the server decodes packets from anyone. Inputs are decoded with a
// fixed server state: the last message sent is kLatest, and the server holds one base (that of a synthetic packet,
// test/client_synthetic.h, seed 1) for whichever message a packet acknowledges. Each input must decode without fault;
// when it decodes, it is coded again against the base its decoding used and decoded again, and the result must equal
// the first decode (the decoder accepts nothing the encoder cannot code).
// Corpus: fuzz/corpus_client (committed, minimised with -merge=1). It is tied to that state and to the format: after a
// change of either, rebuild it (--write-seeds=<dir> writes 256 synthetic packets coded against the state, then exits;
// fuzz from them, then -merge=1 into an emptied fuzz/corpus_client).
// Run: netcodec_fuzz_client -max_len=4096 -timeout=10 -rss_limit_mb=2048 -max_total_time=<s> <work dir> fuzz/corpus_client
// (new inputs go to the first directory, so keep the committed corpus second)
#include "client_synthetic.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace netcodec;

namespace {

constexpr int64_t kLatest = 1000;

// the one base the server holds, for any acknowledged message; records whether a decode used it
struct AnyBase final : ClientBaseLookup {
	ClientBase base;
	mutable bool used = false;
	[[nodiscard]] bool Find(int64_t, ClientBase &b) const override {
		b = base;
		used = true;
		return true;
	}
};

AnyBase *g_lookup = nullptr;

void WriteSeeds(const char *dir) {
	ClientPacketEncoder enc;
	ClientPacket p;
	HeaderRecord h;
	PlayerState ps{};
	size_t written = 0;
	for (uint64_t k = 0; k < 256; k++) {
		synth::Rng r(0xC11E0000ULL + k);
		test::SyntheticPacket(r, p, h, ps);
		p.ack = kLatest - static_cast<int64_t>(r.Below(256));
		std::vector<uint8_t> out;
		if (!enc.Encode(p, r.Below(4) != 0 ? &g_lookup->base : nullptr, 1 << 16, out)) continue;
		const std::string path = std::string(dir) + "/seed_" + std::to_string(k);
		FILE *f = std::fopen(path.c_str(), "wb");
		if (f == nullptr || std::fwrite(out.data(), 1, out.size(), f) != out.size()) {
			std::fprintf(stderr, "cannot write %s\n", path.c_str());
			std::exit(1);
		}
		std::fclose(f);
		written++;
	}
	std::printf("%zu seeds written to %s\n", written, dir);
}

} // namespace

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
	static AnyBase lookup;
	g_lookup = &lookup;
	synth::Rng r(1);
	ClientPacket p;
	HeaderRecord h;
	PlayerState ps{};
	test::SyntheticPacket(r, p, h, ps);
	lookup.base = ClientBaseOf(h, ps);
	for (int i = 1; i < *argc; i++)
		if (std::strncmp((*argv)[i], "--write-seeds=", 14) == 0) {
			WriteSeeds((*argv)[i] + 14);
			std::exit(0);
		}
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	static ClientPacketEncoder enc;
	static ClientPacket d1, d2;
	static std::vector<uint8_t> again;
	g_lookup->used = false;
	if (!DecodeClientPacket(std::span<const uint8_t>(data, size), kLatest, *g_lookup, d1)) return 0;
	const bool used = g_lookup->used;
	if (!enc.Encode(d1, used ? &g_lookup->base : nullptr, 1 << 16, again)) {
		std::fprintf(stderr, "the encoder refuses a decoded client packet\n");
		std::abort();
	}
	g_lookup->used = false;
	if (!DecodeClientPacket(again, kLatest, *g_lookup, d2) || g_lookup->used != used || !test::SamePacket(d2, d1)) {
		std::fprintf(stderr, "decode(encode(decode(x))) differs from decode(x)\n");
		std::abort();
	}
	return 0;
}
