// Robustness of the client packet decoder on malformed input (plan step 8 part 4; run on every preset, so under
// ASan + UBSan too): the server decodes packets from anyone. Random packets (client_synthetic.h) are coded, then
// mutated copies of their bytes (mutate.h) are decoded with a random `latest` (near the acknowledge, or anywhere) and
// one of three lookups: the packet's own base, another base for any message, or none. The decoder must return without
// fault. When a mutated packet decodes, it is coded again against the base its decoding used and decoded again: the
// result must equal the first decode (the decoder accepts nothing the encoder cannot code). Then the limits: a client
// command of kMaxClientCommandLength characters decodes, one character more is refused.
// The inputs come from the synthetic integer generator: every compiler runs the same ones.
// Usage: netcodec_test_client_robustness [packets] [mutations per packet] [seed]
#include "client_synthetic.h"
#include "mutate.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace netcodec;
using netcodec::test::Mutate;
using netcodec::test::SamePacket;
using netcodec::test::SyntheticPacket;

namespace {

// the base of the acknowledged message as one of three servers would find it, recording what it gave
struct RecordingLookup final : ClientBaseLookup {
	enum class Mode : uint8_t { kOwn, kAny, kNone };
	Mode mode = Mode::kNone;
	int64_t seq = 0;     // kOwn: the message whose base is held
	ClientBase own, any;
	mutable bool used = false;   // reset by the caller before each decode
	mutable ClientBase found;

	[[nodiscard]] bool Find(int64_t s, ClientBase &b) const override {
		if (mode == Mode::kNone || (mode == Mode::kOwn && s != seq)) return false;
		b = mode == Mode::kOwn ? own : any;
		used = true;
		found = b;
		return true;
	}
};

struct Stats {
	size_t packets = 0, encodeRefused = 0, decodes = 0, accepted = 0, reencodeRefused = 0, mismatches = 0;
};

// the limit on a client command's text: kMaxClientCommandLength characters decode, one more does not
bool CheckLength(ClientPacketEncoder &enc) {
	const RecordingLookup none;
	bool ok = true;
	for (const size_t len : { kMaxClientCommandLength, kMaxClientCommandLength + 1 }) {
		ClientPacket p;
		p.ack = 100;
		p.commands.push_back(ClientCommand{ .seq = 1, .text = std::string(len, 'a') });
		std::vector<uint8_t> out;
		ClientPacket back;
		const bool decoded = enc.Encode(p, nullptr, 1 << 16, out) && DecodeClientPacket(out, 100, none, back);
		const bool expected = len <= kMaxClientCommandLength;
		if (decoded != expected) {
			std::fprintf(stderr, "a client command of %zu characters %s\n", len, decoded ? "decodes" : "does not decode");
			ok = false;
		}
	}
	return ok;
}

} // namespace

int main(int argc, char **argv) {
	const uint64_t count = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 20000;
	const uint64_t nmut = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 20;
	const uint64_t seed = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 1;
	synth::Rng r(seed), mr(seed ^ 0x5DEECE66DULL);
	ClientPacketEncoder enc;
	RecordingLookup lookup;
	ClientPacket p, other, d1, d2;
	HeaderRecord h, oh;
	PlayerState ps{}, ops{};
	std::vector<uint8_t> out, bad, again;
	Stats st;
	for (uint64_t i = 0; i < count; i++) {
		SyntheticPacket(r, p, h, ps);
		SyntheticPacket(r, other, oh, ops);   // only its base is used: the one a wrong lookup gives
		lookup.own = ClientBaseOf(h, ps);
		lookup.any = ClientBaseOf(oh, ops);
		lookup.seq = p.ack;
		const bool withBase = r.Below(4) != 0;
		st.packets++;
		if (!enc.Encode(p, withBase ? &lookup.own : nullptr, 1 << 16, out)) {
			st.encodeRefused++;
			continue;
		}
		for (uint64_t m = 0; m < nmut; m++) {
			Mutate(mr, out, bad);
			lookup.mode = static_cast<RecordingLookup::Mode>(mr.Index(3));
			const int64_t latest = mr.Below(2) != 0 ? p.ack + mr.Range(0, 255) : mr.Range(0, (int64_t{ 1 } << 31) - 1);
			lookup.used = false;
			st.decodes++;
			if (!DecodeClientPacket(bad, latest, lookup, d1)) continue;
			st.accepted++;
			const bool used = lookup.used;
			const ClientBase base = lookup.found;
			if (!enc.Encode(d1, used ? &base : nullptr, 1 << 16, again)) {
				if (st.reencodeRefused++ < 5) std::fprintf(stderr, "re-encoding refused: packet %" PRIu64 " mutation %" PRIu64 "\n", i, m);
				continue;
			}
			lookup.used = false;
			const bool same = DecodeClientPacket(again, latest, lookup, d2) && lookup.used == used && SamePacket(d2, d1);
			if (!same && st.mismatches++ < 5)
				std::fprintf(stderr, "decode(encode(decode(x))) differs: packet %" PRIu64 " mutation %" PRIu64 "\n", i, m);
		}
	}
	const bool lengthOk = CheckLength(enc);
	std::printf("client packet decoder robustness: %zu packets, %zu malformed inputs decoded without fault, %zu accepted; "
	            "re-encoding refused %zu, decode(encode(decode(x))) different %zu, packets the encoder refused %zu; "
	            "command length limit %s\n",
	            st.packets, st.decodes, st.accepted, st.reencodeRefused, st.mismatches, st.encodeRefused, lengthOk ? "ok" : "FAILED");
	return st.decodes != 0 && st.reencodeRefused == 0 && st.mismatches == 0 && st.encodeRefused == 0 && lengthOk ? 0 : 1;
}
