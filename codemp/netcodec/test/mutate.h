// Mutations of coded bytes for the malformed-input tests (test_decode_robustness.cpp, test_client_robustness.cpp): bits
// flipped, bytes changed, truncated, bytes inserted, deleted or appended, or random bytes of the same length. The
// mutations come from the synthetic integer generator: every compiler runs the same inputs.
#pragma once

#include "synthetic.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace netcodec::test {

inline void Mutate(synth::Rng &r, const std::vector<uint8_t> &in, std::vector<uint8_t> &out) {
	out = in;
	const auto pos = [&](size_t n) { return n == 0 ? size_t{ 0 } : r.Index(n); };
	switch (r.Index(7)) {
	case 0:   // flip 1 to 4 bits
		for (size_t k = 1 + r.Index(4); k > 0 && !out.empty(); k--) out[pos(out.size())] ^= static_cast<uint8_t>(1U << r.Index(8));
		break;
	case 1:   // change 1 to 4 bytes
		for (size_t k = 1 + r.Index(4); k > 0 && !out.empty(); k--) out[pos(out.size())] = static_cast<uint8_t>(r.Below(256));
		break;
	case 2:   // truncate
		out.resize(pos(out.size()));
		break;
	case 3: { // insert 1 to 8 bytes
		const size_t at = pos(out.size() + 1);
		for (size_t k = 1 + r.Index(8); k > 0; k--)
			out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), static_cast<uint8_t>(r.Below(256)));
		break;
	}
	case 4: { // delete 1 to 8 bytes
		const size_t at = pos(out.size());
		const size_t n = std::min(out.size() - at, 1 + r.Index(8));
		out.erase(out.begin() + static_cast<std::ptrdiff_t>(at), out.begin() + static_cast<std::ptrdiff_t>(at + n));
		break;
	}
	case 5:   // append 1 to 16 bytes
		for (size_t k = 1 + r.Index(16); k > 0; k--) out.push_back(static_cast<uint8_t>(r.Below(256)));
		break;
	default:  // random bytes of the same length
		for (uint8_t &b : out) b = static_cast<uint8_t>(r.Below(256));
		break;
	}
}

} // namespace netcodec::test
