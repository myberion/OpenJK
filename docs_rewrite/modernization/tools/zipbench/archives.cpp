// Benchmark of archive readers on the game's own pk3 files, from the files on disk (in the system's cache): each
// reader opens the archives and lists their entries, then reads every entry whole, as the engine's FS_LoadZipFile
// and FS_ReadFile do. The "direct" readers are pk3.h's directory reader with one decoder of decoders.h: the entry's
// compressed bytes read in one call, inflated in one call, the CRC-32 checked.
// Usage: archives <game base folder holding assets*.pk3>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

#include "decoders.h"
#include "pk3.h"

// what a reader delivered, hashed by the reference CRC-32 when a check is asked for
struct Check {
	uint64_t files = 0, bytes = 0, crc = 0;
};
static void Seen(Check *c, const uint8_t *p, size_t n) {
	if (!c) return;
	c->files++;
	c->bytes += n;
	c->crc += pk3::Crc32(p, n);
}

struct Reader {
	const char *name;
	void *(*open)(const pk3::Archive &ref);		// opens the archive and lists its entries
	bool (*read)(void *h, uint8_t *buf, Check *c);	// reads every entry whole
	void (*close)(void *h);
};

#ifdef WITH_MINIZIP	// minizip, driven as the engine drives it
#include <unzip.h>
extern "C" {
void *openjk_minizip_malloc(int size) { return malloc(static_cast<size_t>(size)); }
int openjk_minizip_free(void *p) {
	free(p);
	return 0;
}
}
struct Minizip {
	unzFile uf = nullptr;
	std::string names;
	std::vector<std::pair<uLong, uLong>> files;	// position in the directory, size
};
static void *MinizipOpen(const pk3::Archive &ref) {
	// as FS_LoadZipFile: the directory walked twice, the names and the positions kept
	Minizip *m = new Minizip;
	unz_global_info gi;
	unz_file_info info;
	char name[256];
	size_t len = 0;
	m->uf = unzOpen(ref.path.c_str());
	if (!m->uf || unzGetGlobalInfo(m->uf, &gi) != UNZ_OK) return nullptr;
	unzGoToFirstFile(m->uf);
	for (uLong i = 0; i < gi.number_entry; i++) {
		if (unzGetCurrentFileInfo(m->uf, &info, name, sizeof name, nullptr, 0, nullptr, 0) != UNZ_OK) return nullptr;
		len += strlen(name) + 1;
		unzGoToNextFile(m->uf);
	}
	m->names.reserve(len);
	unzGoToFirstFile(m->uf);
	for (uLong i = 0; i < gi.number_entry; i++) {
		if (unzGetCurrentFileInfo(m->uf, &info, name, sizeof name, nullptr, 0, nullptr, 0) != UNZ_OK) return nullptr;
		m->names.append(name, strlen(name) + 1);
		if (name[0] && name[strlen(name) - 1] != '/') m->files.push_back({unzGetOffset(m->uf), info.uncompressed_size});
		unzGoToNextFile(m->uf);
	}
	return m;
}
static bool MinizipRead(void *h, uint8_t *buf, Check *c) {
	Minizip *m = static_cast<Minizip *>(h);
	for (const auto &f : m->files) {
		// as FS_FOpenFileRead and FS_Read
		if (unzSetOffset(m->uf, f.first) != UNZ_OK || unzOpenCurrentFile(m->uf) != UNZ_OK) return false;
		const int got = f.second ? unzReadCurrentFile(m->uf, buf, static_cast<unsigned>(f.second)) : 0;
		if (unzCloseCurrentFile(m->uf) != UNZ_OK || got != static_cast<int>(f.second)) return false;
		Seen(c, buf, f.second);
	}
	return true;
}
static void MinizipClose(void *h) {
	Minizip *m = static_cast<Minizip *>(h);
	unzClose(m->uf);
	delete m;
}
#endif

#ifdef WITH_MINIZ
struct Miniz {
	mz_zip_archive zip{};
	std::vector<std::pair<mz_uint, size_t>> files;
};
static void *MinizOpen(const pk3::Archive &ref) {
	Miniz *m = new Miniz;
	if (!mz_zip_reader_init_file(&m->zip, ref.path.c_str(), 0)) return nullptr;
	const mz_uint n = mz_zip_reader_get_num_files(&m->zip);
	for (mz_uint i = 0; i < n; i++) {
		mz_zip_archive_file_stat st;
		if (!mz_zip_reader_file_stat(&m->zip, i, &st)) return nullptr;
		if (!st.m_is_directory) m->files.push_back({i, static_cast<size_t>(st.m_uncomp_size)});
	}
	return m;
}
static bool MinizRead(void *h, uint8_t *buf, Check *c) {
	Miniz *m = static_cast<Miniz *>(h);
	for (const auto &f : m->files) {
		if (!mz_zip_reader_extract_to_mem(&m->zip, f.first, buf, f.second, 0)) return false;
		Seen(c, buf, f.second);
	}
	return true;
}
static void MinizClose(void *h) {
	Miniz *m = static_cast<Miniz *>(h);
	mz_zip_reader_end(&m->zip);
	delete m;
}
#endif

#ifdef WITH_MZNG	// minizip-ng: its reader walks the directory in order
#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
static void *MzNgOpen(const pk3::Archive &ref) {
	void *r = mz_zip_reader_create();
	mz_zip_file *info = nullptr;
	if (mz_zip_reader_open_file(r, ref.path.c_str()) != MZ_OK || mz_zip_reader_goto_first_entry(r) != MZ_OK) return nullptr;
	do {
		if (mz_zip_reader_entry_get_info(r, &info) != MZ_OK) return nullptr;
	} while (mz_zip_reader_goto_next_entry(r) == MZ_OK);
	return r;
}
static bool MzNgRead(void *r, uint8_t *buf, Check *c) {
	mz_zip_file *info = nullptr;
	if (mz_zip_reader_goto_first_entry(r) != MZ_OK) return false;
	do {
		if (mz_zip_reader_entry_get_info(r, &info) != MZ_OK) return false;
		if (mz_zip_reader_entry_is_dir(r) == MZ_OK) continue;
		const int64_t size = info->uncompressed_size;
		int64_t got = 0;
		if (mz_zip_reader_entry_open(r) != MZ_OK) return false;
		while (got < size) {
			const int32_t n = mz_zip_reader_entry_read(r, buf + got, static_cast<int32_t>(size - got));
			if (n <= 0) return false;
			got += n;
		}
		if (mz_zip_reader_entry_close(r) != MZ_OK) return false;
		Seen(c, buf, static_cast<size_t>(size));
	} while (mz_zip_reader_goto_next_entry(r) == MZ_OK);
	return true;
}
static void MzNgClose(void *r) {
	mz_zip_reader_close(r);
	mz_zip_reader_delete(&r);
}
#endif

#ifdef WITH_LIBZIP
#include <zip.h>
struct LibZip {
	zip_t *za = nullptr;
	std::vector<std::pair<zip_uint64_t, zip_uint64_t>> files;
};
static void *LibZipOpen(const pk3::Archive &ref) {
	LibZip *m = new LibZip;
	int err = 0;
	m->za = zip_open(ref.path.c_str(), ZIP_RDONLY, &err);
	if (!m->za) return nullptr;
	const zip_int64_t n = zip_get_num_entries(m->za, 0);
	for (zip_int64_t i = 0; i < n; i++) {
		zip_stat_t st;
		if (zip_stat_index(m->za, static_cast<zip_uint64_t>(i), 0, &st)) return nullptr;
		if (st.name[0] && st.name[strlen(st.name) - 1] != '/') m->files.push_back({static_cast<zip_uint64_t>(i), st.size});
	}
	return m;
}
static bool LibZipRead(void *h, uint8_t *buf, Check *c) {
	LibZip *m = static_cast<LibZip *>(h);
	for (const auto &f : m->files) {
		zip_file_t *zf = zip_fopen_index(m->za, f.first, 0);
		zip_uint64_t got = 0;
		if (!zf) return false;
		while (got < f.second) {
			const zip_int64_t n = zip_fread(zf, buf + got, f.second - got);
			if (n <= 0) return false;
			got += static_cast<zip_uint64_t>(n);
		}
		if (zip_fclose(zf)) return false;
		Seen(c, buf, static_cast<size_t>(f.second));
	}
	return true;
}
static void LibZipClose(void *h) {
	LibZip *m = static_cast<LibZip *>(h);
	zip_discard(m->za);
	delete m;
}
#endif

#ifdef WITH_PHYSFS	// PhysicsFS: mounting an archive reads its directory into a tree; the files are opened by name
#include <physfs.h>
static void *PhysFsOpen(const pk3::Archive &ref) {
	static const int init = PHYSFS_init(nullptr);
	return init && PHYSFS_mount(ref.path.c_str(), nullptr, 1) ? const_cast<pk3::Archive *>(&ref) : nullptr;
}
static bool PhysFsRead(void *h, uint8_t *buf, Check *c) {
	for (const pk3::Entry &e : static_cast<const pk3::Archive *>(h)->entries) {
		PHYSFS_File *f = PHYSFS_openRead(e.name.c_str());
		if (!f) return false;
		const PHYSFS_sint64 size = PHYSFS_fileLength(f), got = PHYSFS_readBytes(f, buf, static_cast<PHYSFS_uint64>(size));
		PHYSFS_close(f);
		if (got != size) return false;
		Seen(c, buf, static_cast<size_t>(size));
	}
	return true;
}
static void PhysFsClose(void *h) { PHYSFS_unmount(static_cast<const pk3::Archive *>(h)->path.c_str()); }
#endif

// the direct reader: pk3.h's directory reader and one decoder
struct Direct {
	FILE *f = nullptr;
	std::vector<pk3::Entry> entries;
	std::vector<uint8_t> scratch;
	Inflate inflate = nullptr;
	Crc crc = nullptr;
};
// shortcut: fseek's long limits an archive to 2 GB on Windows, enough for the game's; 64-bit offsets in an engine
static void *DirectOpen(const pk3::Archive &ref, Inflate inflate, Crc crc) {
	Direct *d = new Direct;
	d->inflate = inflate;
	d->crc = crc;
	d->f = fopen(ref.path.c_str(), "rb");
	if (!d->f || fseek(d->f, 0, SEEK_END)) return nullptr;
	const long size = ftell(d->f), tailn = std::min<long>(size, 22 + 65535);
	std::vector<uint8_t> tail(static_cast<size_t>(tailn)), cd;
	pk3::Directory dir;
	if (fseek(d->f, size - tailn, SEEK_SET) || fread(tail.data(), 1, tail.size(), d->f) != tail.size() ||
		!pk3::FindDirectory(tail.data(), tail.size(), dir))
		return nullptr;
	cd.resize(dir.size);
	if (fseek(d->f, static_cast<long>(dir.at), SEEK_SET) || fread(cd.data(), 1, cd.size(), d->f) != cd.size() ||
		!pk3::ParseDirectory(cd.data(), dir, d->entries))
		return nullptr;
	uint32_t largest = 0;
	for (const pk3::Entry &e : d->entries) largest = std::max(largest, e.csize);
	d->scratch.resize(largest);
	return d;
}
static bool DirectRead(void *h, uint8_t *buf, Check *c) {
	Direct *d = static_cast<Direct *>(h);
	for (const pk3::Entry &e : d->entries) {
		uint8_t local[30];
		if (fseek(d->f, static_cast<long>(e.local), SEEK_SET) || fread(local, 1, 30, d->f) != 30 || !pk3::DataAfter(local) ||
			fseek(d->f, static_cast<long>(pk3::DataAfter(local) - 30), SEEK_CUR))
			return false;
		if (e.method == 0) {
			if (e.csize != e.usize || fread(buf, 1, e.usize, d->f) != e.usize) return false;
		} else if (e.method != 8 || fread(d->scratch.data(), 1, e.csize, d->f) != e.csize ||
			(e.usize && !d->inflate(d->scratch.data(), e.csize, buf, e.usize))) {
			return false;
		}
		if (d->crc(buf, e.usize) != e.crc) return false;
		Seen(c, buf, e.usize);
	}
	return true;
}
static void DirectClose(void *h) {
	Direct *d = static_cast<Direct *>(h);
	fclose(d->f);
	delete d;
}
template <Inflate inflate, Crc crc> static void *DirectOpenWith(const pk3::Archive &ref) { return DirectOpen(ref, inflate, crc); }

static const Reader kReaders[] = {
#ifdef WITH_MINIZIP
	{MINIZIP_NAME, MinizipOpen, MinizipRead, MinizipClose},
#endif
#ifdef WITH_MINIZ
	{"miniz 3.1.2", MinizOpen, MinizRead, MinizClose},
#endif
#ifdef WITH_MZNG
	{"minizip-ng 4.2.2 + zlib-ng", MzNgOpen, MzNgRead, MzNgClose},
#endif
#ifdef WITH_LIBZIP
	{"libzip 1.12.1 + zlib 1.4", LibZipOpen, LibZipRead, LibZipClose},
#endif
#ifdef WITH_PHYSFS
	{"PhysicsFS 3.2.0", PhysFsOpen, PhysFsRead, PhysFsClose},
#endif
#ifdef WITH_DIRECT_ZLIB
	{"direct + " ZLIB_NAME, DirectOpenWith<InfZlib, CrcZlib>, DirectRead, DirectClose},
#endif
#ifdef WITH_ZLIBNG
	{"direct + zlib-ng", DirectOpenWith<InfZlibNg, CrcZlibNg>, DirectRead, DirectClose},
#endif
#ifdef WITH_LIBDEFLATE
	{"direct + libdeflate", DirectOpenWith<InfLibdeflate, CrcLibdeflate>, DirectRead, DirectClose},
#endif
#ifdef WITH_ISAL
	{"direct + ISA-L", DirectOpenWith<InfIsal, CrcIsal>, DirectRead, DirectClose},
#endif
#ifdef WITH_WUFFS
	{"direct + Wuffs", DirectOpenWith<InfWuffs, CrcWuffs>, DirectRead, DirectClose},
#endif
};

static double Since(std::chrono::steady_clock::time_point t0) {
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

int main(int argc, char **argv) {
	if (argc < 2) return 1;
#ifdef _WIN32
	SetThreadAffinityMask(GetCurrentThread(), 1ull << 20);
#endif
	// the reference: the directories read by pk3.h; loading the archives whole also puts them in the system's cache
	std::vector<pk3::Archive> archives = pk3::LoadAll(argv[1]);
	Check want;
	size_t largest = 0;
	for (pk3::Archive &a : archives) {
		for (const pk3::Entry &e : a.entries) {
			want.files++;
			want.bytes += e.usize;
			want.crc += e.crc;
			largest = std::max<size_t>(largest, e.usize);
		}
		std::vector<uint8_t>().swap(a.bytes);
	}
	std::vector<uint8_t> buf(largest + 64);

	printf("%-34s %7s %9s %9s %7s %s\n", "reader", "files", "open ms", "read ms", "MB/s", "check");
	for (const Reader &r : kReaders) {
		Check got;
		bool ok = true;
		for (const pk3::Archive &a : archives) {
			void *h = r.open(a);
			ok = ok && h && r.read(h, buf.data(), &got);
			if (h) r.close(h);
		}
		ok = ok && got.files == want.files && got.bytes == want.bytes && got.crc == want.crc;
		double open = 1e30, read = 1e30;
		for (int run = 0; ok && run < 5; run++) {
			double ms = 0;
			for (const pk3::Archive &a : archives) {
				const auto t0 = std::chrono::steady_clock::now();
				void *h = r.open(a);
				ms += Since(t0);
				r.close(h);
			}
			open = std::min(open, ms);
		}
		for (int run = 0; ok && run < 3; run++) {
			double ms = 0;
			for (const pk3::Archive &a : archives) {
				void *h = r.open(a);
				const auto t0 = std::chrono::steady_clock::now();
				r.read(h, buf.data(), nullptr);
				ms += Since(t0);
				r.close(h);
			}
			read = std::min(read, ms);
		}
		printf("%-34s %7llu %9.1f %9.1f %7.0f %s\n", r.name, static_cast<unsigned long long>(got.files), ok ? open : 0.0, ok ? read : 0.0,
			ok ? static_cast<double>(want.bytes) / 1e6 / (read / 1e3) : 0.0, ok ? "ok" : "FAILED");
		fflush(stdout);
	}
	return 0;
}
