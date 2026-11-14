// Checks and times the engine's pk3 reader (shared/qcommon/q_pk3.cpp), built alone with the stand-ins of shim/, on
// the game's own archives:
//   - its directory against minizip's, entry by entry (name, CRC-32, size): what FS_LoadZipFile hashes and lists;
//   - every file read whole in one call, then in pieces of 8 KB: the CRC-32 of what it delivers;
//   - seeks from every origin on one file in 40, each followed by a read compared with the file's content;
//   - two files of one archive read in turns;
//   - a file read after its archive was closed, as the music is across a restart of the file system;
//   - the test archives of seeds.py, two of them ZIP64 (the game's are not): directory and files as above.
// Usage: pk3check <game base folder holding assets*.pk3> [folder of test archives]
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

#include <unzip.h>

#include "pk3.h"
#include "qcommon/q_shared.h"
#include "qcommon/q_pk3.h"

int pk3_warnings;

extern "C" {
void *openjk_minizip_malloc(int size) { return malloc(static_cast<size_t>(size)); }
int openjk_minizip_free(void *p) {
	free(p);
	return 0;
}
}

static double Since(std::chrono::steady_clock::time_point t0) {
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// the entries of the directory that differ from minizip's
static int CompareDirectory(const std::string &path, pk3Archive_t *archive) {
	unzFile uf = unzOpen(path.c_str());
	unz_global_info gi;
	int bad = 0;
	if (!uf || unzGetGlobalInfo(uf, &gi) != UNZ_OK) return 1;
	if (static_cast<int>(gi.number_entry) != PK3_NumEntries(archive)) bad++;
	unzGoToFirstFile(uf);
	for (int i = 0; i < PK3_NumEntries(archive) && i < static_cast<int>(gi.number_entry); i++) {
		unz_file_info info;
		char name[1024];
		pk3Entry_t e;
		unzGetCurrentFileInfo(uf, &info, name, sizeof name, nullptr, 0, nullptr, 0);
		PK3_GetEntry(archive, i, &e);
		bad += strcmp(name, e.name) || info.crc != e.crc || info.uncompressed_size != e.size;
		unzGoToNextFile(uf);
	}
	unzClose(uf);
	return bad;
}

// every file read from its start in pieces of piece bytes (0: in one call); the files whose bytes are not the
// directory's CRC-32 are counted when check is set
static int ReadAll(pk3Archive_t *archive, uint8_t *buf, int piece, bool check, uint64_t *bytes) {
	int bad = 0;
	for (int i = 0; i < PK3_NumEntries(archive); i++) {
		pk3Entry_t e;
		PK3_GetEntry(archive, i, &e);
		if (!e.size) continue;
		pk3File_t *f = PK3_OpenFile(archive, i);
		if (!f) {
			bad++;
			continue;
		}
		const int size = static_cast<int>(e.size);
		int got = 0;
		while (got < size) {
			const int n = PK3_Read(f, buf + got, piece ? std::min(piece, size - got) : size);
			if (n <= 0) break;
			got += n;
		}
		if (check) bad += got != size || PK3_Read(f, buf, 1) != 0 || PK3_Tell(f) != size || pk3::Crc32(buf, e.size) != e.crc;
		PK3_CloseFile(f);
		*bytes += e.size;
	}
	return bad;
}

// seeks on one file in step, each checked by the position it gives and by the bytes read after it
static int Seeks(pk3Archive_t *archive, uint8_t *buf, int step, int *done) {
	int bad = 0;
	uint32_t rng = 12345;
	auto next = [&rng]() { return rng = rng * 1664525u + 1013904223u; };
	std::vector<uint8_t> got(4096);
	for (int i = 0; i < PK3_NumEntries(archive); i += step) {
		pk3Entry_t e;
		PK3_GetEntry(archive, i, &e);
		if (!e.size) continue;
		const int64_t size = e.size;
		pk3File_t *f = PK3_OpenFile(archive, i);
		if (!f || PK3_Read(f, buf, static_cast<int>(size)) != size) {
			bad++;
			if (f) PK3_CloseFile(f);
			continue;
		}
		int64_t at = size;
		for (int k = 0; k < 40; k++) {
			const int origin = static_cast<int>(next() >> 8) % 3;
			const long offset = static_cast<long>(static_cast<int64_t>(next() >> 4) % (2 * size + 21) - size - 10);
			const int64_t base = origin == FS_SEEK_SET ? 0 : origin == FS_SEEK_CUR ? at : size;
			at = std::min<int64_t>(std::max<int64_t>(base + offset, 0), size);
			if (PK3_Seek(f, offset, origin) != offset || PK3_Tell(f) != at) bad++;
			const int n = static_cast<int>(std::min<int64_t>(static_cast<int64_t>(got.size()), size - at));
			if (PK3_Read(f, got.data(), static_cast<int>(got.size())) != n || memcmp(got.data(), buf + at, static_cast<size_t>(n))) bad++;
			at += n;
			(*done)++;
		}
		PK3_CloseFile(f);
	}
	return bad;
}

// two files of the archive open at once, read in turns of 5000 bytes
static int TwoFiles(pk3Archive_t *archive, uint8_t *buf, int step, int *done) {
	int bad = 0;
	std::vector<uint8_t> a, b;
	for (int i = 0; i + 1 < PK3_NumEntries(archive); i += step) {
		pk3Entry_t ea, eb;
		PK3_GetEntry(archive, i, &ea);
		PK3_GetEntry(archive, i + 1, &eb);
		if (!ea.size || !eb.size) continue;
		pk3File_t *fa = PK3_OpenFile(archive, i), *fb = PK3_OpenFile(archive, i + 1);
		a.clear();
		b.clear();
		for (bool more = fa && fb; more;) {
			const int na = PK3_Read(fa, buf, 5000);
			if (na > 0) a.insert(a.end(), buf, buf + na);
			const int nb = PK3_Read(fb, buf, 5000);
			if (nb > 0) b.insert(b.end(), buf, buf + nb);
			more = na > 0 || nb > 0;
		}
		bad += a.size() != ea.size || b.size() != eb.size || pk3::Crc32(a.data(), a.size()) != ea.crc || pk3::Crc32(b.data(), b.size()) != eb.crc;
		if (fa) PK3_CloseFile(fa);
		if (fb) PK3_CloseFile(fb);
		(*done)++;
	}
	return bad;
}

int main(int argc, char **argv) {
	if (argc < 2) return 1;
#ifdef _WIN32
	SetThreadAffinityMask(GetCurrentThread(), 1ull << 20);
#endif
	const std::vector<std::string> paths = pk3::List(argv[1]);
	std::vector<uint8_t> buf(64u << 20);
	int entries = 0, badDir = 0, badWhole = 0, badPieces = 0, badSeeks = 0, badTwo = 0, seeks = 0, pairs = 0;
	double open = 1e30, whole = 1e30, pieces = 1e30;
	uint64_t bytes = 0;

	for (int run = 0; run < 5; run++) {
		double ms = 0;
		for (const std::string &path : paths) {
			const auto t0 = std::chrono::steady_clock::now();
			pk3Archive_t *archive = PK3_Open(path.c_str());
			ms += Since(t0);
			if (!archive) return 1;
			if (!run) {
				entries += PK3_NumEntries(archive);
				badDir += CompareDirectory(path, archive);
				badSeeks += Seeks(archive, buf.data(), 40, &seeks);
				badTwo += TwoFiles(archive, buf.data(), 100, &pairs);
			}
			PK3_Close(archive);
		}
		open = std::min(open, ms);
	}
	for (int run = 0; run < 4; run++) {	// the first run checks, the three others are timed
		double msWhole = 0, msPieces = 0;
		uint64_t n = 0;
		for (const std::string &path : paths) {
			pk3Archive_t *archive = PK3_Open(path.c_str());
			auto t0 = std::chrono::steady_clock::now();
			badWhole += ReadAll(archive, buf.data(), 0, !run, &n);
			msWhole += Since(t0);
			t0 = std::chrono::steady_clock::now();
			badPieces += ReadAll(archive, buf.data(), 8192, !run, &n);
			msPieces += Since(t0);
			PK3_Close(archive);
		}
		bytes = n / 2;
		if (run) {
			whole = std::min(whole, msWhole);
			pieces = std::min(pieces, msPieces);
		}
	}
	printf("directory: %d entries in %d archives, %d differ from minizip's; opened in %.1f ms\n", entries, static_cast<int>(paths.size()), badDir, open);
	printf("read whole:        %8.1f ms %6.0f MB/s, %d files wrong\n", whole, bytes / 1e6 / (whole / 1e3), badWhole);
	printf("read 8 KB a time:  %8.1f ms %6.0f MB/s, %d files wrong\n", pieces, bytes / 1e6 / (pieces / 1e3), badPieces);
	printf("seeks: %d, %d wrong; two files at once: %d pairs, %d wrong; warnings printed: %d\n", seeks, badSeeks, pairs, badTwo, pk3_warnings);

	// the first file of 100 KB or more of the first archive, read once the archive is closed
	pk3Archive_t *archive = PK3_Open(paths[0].c_str());
	pk3Entry_t e{};
	int index = 0;
	for (; index < PK3_NumEntries(archive) && e.size < 100000; index++) PK3_GetEntry(archive, index, &e);
	pk3File_t *f = PK3_OpenFile(archive, index - 1);
	PK3_Close(archive);
	const bool badClosed = !f || PK3_Read(f, buf.data(), static_cast<int>(e.size)) != static_cast<int>(e.size) || pk3::Crc32(buf.data(), e.size) != e.crc;
	if (f) PK3_CloseFile(f);
	printf("a file read after its archive was closed: %s\n", badClosed ? "wrong" : "right");

	// the test archives of seeds.py, the ZIP64 ones among them: the directory against minizip's, every file read
	int badTest = 0, testArchives = 0, testEntries = 0;
	if (argc > 2) {
		for (const auto &file : std::filesystem::directory_iterator(argv[2])) {
			const std::string path = file.path().string();
			pk3Archive_t *test = PK3_Open(path.c_str());
			uint64_t n = 0;
			testArchives++;
			if (!test) {
				badTest++;
				continue;
			}
			testEntries += PK3_NumEntries(test);
			// minizip 1.2.8 does not open an archive with no file
			badTest += (PK3_NumEntries(test) ? CompareDirectory(path, test) : 0) + ReadAll(test, buf.data(), 0, true, &n) + ReadAll(test, buf.data(), 100, true, &n);
			PK3_Close(test);
		}
		printf("test archives: %d with %d entries, %d wrong\n", testArchives, testEntries, badTest);
	}
	return badDir || badWhole || badPieces || badSeeks || badTwo || badClosed || badTest || pk3_warnings ? 1 : 0;
}
