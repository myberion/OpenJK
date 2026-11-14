// The directory of a pk3 file, read by 60 lines of code: what the two benchmarks walk, and the directory reader the
// "direct" readers of archives.cpp are measured with.
// Plain ZIP only (no ZIP64, no encryption), which is what the game's archives are.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace pk3 {

enum { kClasses = 3 };

struct Entry {
	std::string name;
	uint32_t local = 0;	// offset of the entry's local header in the archive
	uint32_t offset = 0;	// offset of the entry's data (archives held in memory only)
	uint32_t csize = 0, usize = 0, crc = 0;
	int method = 0;		// 0 stored, 8 deflated
	int cls = 0;		// 0: maps, models and TGA (data deflate shrinks); 1: JPEG, PNG, MP3, video (it does not); 2: the rest
};

struct Directory {
	uint32_t at = 0, size = 0, count = 0;
};

struct Archive {
	std::string path;
	std::vector<uint8_t> bytes;
	std::vector<Entry> entries;
};

inline uint32_t U16(const uint8_t *p) { return static_cast<uint32_t>(p[0] | (p[1] << 8)); }
inline uint32_t U32(const uint8_t *p) { return U16(p) | (U16(p + 2) << 16); }

inline int ClassOf(const std::string &name) {
	const size_t dot = name.rfind('.');
	std::string ext = dot == std::string::npos ? "" : name.substr(dot + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
	for (const char *e : {"bsp", "gla", "glm", "md3", "tga"})
		if (ext == e) return 0;
	for (const char *e : {"jpg", "png", "mp3", "roq", "wav"})
		if (ext == e) return 1;
	return 2;
}

// where the directory is, from the last bytes of the archive (the end record, before an optional comment)
inline bool FindDirectory(const uint8_t *tail, size_t n, Directory &d) {
	if (n < 22) return false;
	for (size_t i = n - 22;; i--) {
		if (U32(tail + i) == 0x06054b50) {
			d.count = U16(tail + i + 10);
			d.size = U32(tail + i + 12);
			d.at = U32(tail + i + 16);
			return true;
		}
		if (!i) return false;
	}
}

// the entries of a directory held in memory (d.size bytes); folders are left out
inline bool ParseDirectory(const uint8_t *cd, const Directory &d, std::vector<Entry> &entries) {
	size_t at = 0;
	entries.reserve(d.count);
	for (uint32_t i = 0; i < d.count; i++) {
		if (at + 46 > d.size || U32(cd + at) != 0x02014b50) return false;
		const uint8_t *h = cd + at;
		const uint32_t namelen = U16(h + 28);
		if (at + 46 + namelen > d.size) return false;
		Entry e;
		e.method = static_cast<int>(U16(h + 10));
		e.crc = U32(h + 16);
		e.csize = U32(h + 20);
		e.usize = U32(h + 24);
		e.local = U32(h + 42);
		e.name.assign(reinterpret_cast<const char *>(h + 46), namelen);
		e.cls = ClassOf(e.name);
		at += 46 + namelen + U16(h + 30) + U16(h + 32);
		if (!e.name.empty() && e.name.back() != '/') entries.push_back(std::move(e));
	}
	return true;
}

// the distance from an entry's local header (its 30 first bytes) to its data; 0 when it is not a local header
inline uint32_t DataAfter(const uint8_t *local) { return U32(local) == 0x04034b50 ? 30 + U16(local + 26) + U16(local + 28) : 0; }

inline std::vector<std::string> List(const std::string &base) {
	std::vector<std::string> paths;
	for (const auto &f : std::filesystem::directory_iterator(base)) {
		const std::string name = f.path().filename().string();
		if (name.rfind("assets", 0) == 0 && f.path().extension() == ".pk3") paths.push_back(f.path().string());
	}
	std::sort(paths.begin(), paths.end());
	return paths;
}

// every archive of the folder, whole in memory, with its entries
inline std::vector<Archive> LoadAll(const std::string &base) {
	std::vector<Archive> archives;
	for (const std::string &path : List(base)) {
		Archive a;
		a.path = path;
		std::ifstream f(path, std::ios::binary | std::ios::ate);
		a.bytes.resize(static_cast<size_t>(f.tellg()));
		f.seekg(0);
		f.read(reinterpret_cast<char *>(a.bytes.data()), static_cast<std::streamsize>(a.bytes.size()));
		const std::vector<uint8_t> &b = a.bytes;
		Directory d;
		bool ok = FindDirectory(b.data(), b.size(), d) && static_cast<size_t>(d.at) + d.size <= b.size() && ParseDirectory(&b[d.at], d, a.entries);
		for (Entry &e : a.entries) {
			ok = ok && static_cast<size_t>(e.local) + 30 <= b.size() && DataAfter(&b[e.local]);
			if (!ok) break;
			e.offset = e.local + DataAfter(&b[e.local]);
			ok = static_cast<size_t>(e.offset) + e.csize <= b.size();
		}
		if (!ok) {
			fprintf(stderr, "not a plain ZIP: %s\n", path.c_str());
			exit(1);
		}
		archives.push_back(std::move(a));
	}
	return archives;
}

// the reference CRC-32 of the checks (slicing by 8), independent of every library measured
inline uint32_t Crc32(const uint8_t *p, size_t n) {
	static uint32_t t[8][256];
	if (!t[0][1]) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
			t[0][i] = c;
		}
		for (uint32_t i = 0; i < 256; i++)
			for (int k = 1; k < 8; k++) t[k][i] = (t[k - 1][i] >> 8) ^ t[0][t[k - 1][i] & 255];
	}
	uint32_t c = 0xFFFFFFFFu;
	for (; n >= 8; n -= 8, p += 8) {
		const uint32_t a = U32(p) ^ c, b = U32(p + 4);
		c = t[7][a & 255] ^ t[6][(a >> 8) & 255] ^ t[5][(a >> 16) & 255] ^ t[4][a >> 24] ^ t[3][b & 255] ^ t[2][(b >> 8) & 255] ^
			t[1][(b >> 16) & 255] ^ t[0][b >> 24];
	}
	for (; n; n--) c = (c >> 8) ^ t[0][(c ^ *p++) & 255];
	return ~c;
}

}	// namespace pk3
