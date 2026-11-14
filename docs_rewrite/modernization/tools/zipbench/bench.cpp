// Decompression benchmark of deflate libraries on the game's own pk3 files: every deflated entry of the archives
// inflated from memory to a buffer of its size by each decoder built in, the best of RUNS decodings per entry, and
// each output checked against the CRC-32 the archive's directory holds. The second mode times the CRC-32 functions.
// Usage: bench <game base folder holding assets*.pk3> inflate|crc
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

#include "decoders.h"
#include "pk3.h"

struct Decoder {
	const char *name;
	Inflate inflate;
	Crc crc;
};
static const Decoder kDecoders[] = {
#ifdef WITH_ZLIB
	{ZLIB_NAME, InfZlib, CrcZlib},
#endif
#ifdef WITH_ZLIBNG
	{"zlib-ng 2.3.3", InfZlibNg, CrcZlibNg},
#endif
#ifdef WITH_LIBDEFLATE
	{"libdeflate 1.26", InfLibdeflate, CrcLibdeflate},
#endif
#ifdef WITH_ISAL
	{"ISA-L 2.32.1", InfIsal, CrcIsal},
#endif
#ifdef WITH_MINIZ
	{"miniz 3.1.2", InfMiniz, CrcMiniz},
#endif
#ifdef WITH_WUFFS
	{"Wuffs 0.4 alpha 12", InfWuffs, CrcWuffs},
#endif
#ifdef WITH_STB
	{"stb_image 2.32", InfStb, nullptr},
#endif
};

int main(int argc, char **argv) {
	const int RUNS = 5;
	if (argc < 3) return 1;
	const bool crcmode = !strcmp(argv[2], "crc");
#ifdef _WIN32
	SetThreadAffinityMask(GetCurrentThread(), 1ull << 20);
#endif
	std::vector<pk3::Archive> archives = pk3::LoadAll(argv[1]);
	size_t largest = 0;
	for (const pk3::Archive &a : archives)
		for (const pk3::Entry &e : a.entries) largest = std::max<size_t>(largest, e.usize);
	std::vector<uint8_t> out(largest + 64);
	static const char *const kClass[pk3::kClasses] = {"maps, models, TGA", "JPEG, PNG, MP3, video", "the rest"};

	printf("%-20s %6s %5s %5s %9s %7s", "decoder", "files", "fail", "crc", "ms", "MB/s");
	for (const char *c : kClass) printf(" | %-21s ms %6s", c, "MB/s");
	printf("\n");
	for (const Decoder &d : kDecoders) {
		if (crcmode && !d.crc) continue;
		int n = 0, fail = 0, bad = 0;
		double ms[pk3::kClasses] = {}, bytes[pk3::kClasses] = {};
		for (const pk3::Archive &a : archives) {
			for (const pk3::Entry &e : a.entries) {
				if (e.method != 8 || !e.usize) continue;
				const uint8_t *in = a.bytes.data() + e.offset;
				n++;
				if (crcmode) {
					// the data to hash: the entry inflated by the first decoder
					if (!kDecoders[0].inflate(in, e.csize, out.data(), e.usize)) continue;
					bad += d.crc(out.data(), e.usize) != e.crc;
				} else {
					if (!d.inflate(in, e.csize, out.data(), e.usize)) {
						fail++;
						continue;
					}
					bad += pk3::Crc32(out.data(), e.usize) != e.crc;
				}
				double best = 1e30;
				for (int r = 0; r < RUNS; r++) {
					const auto t0 = std::chrono::steady_clock::now();
					if (crcmode) d.crc(out.data(), e.usize);
					else d.inflate(in, e.csize, out.data(), e.usize);
					best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
				}
				ms[e.cls] += best;
				bytes[e.cls] += e.usize;
			}
		}
		const double tms = ms[0] + ms[1] + ms[2], tb = bytes[0] + bytes[1] + bytes[2];
		printf("%-20s %6d %5d %5d %9.1f %7.0f", d.name, n, fail, bad, tms, tb / 1e6 / (tms / 1e3));
		for (int c = 0; c < pk3::kClasses; c++) printf(" | %24.1f %6.0f", ms[c], bytes[c] / 1e6 / (ms[c] / 1e3));
		printf("\n");
		fflush(stdout);
	}
	return 0;
}
