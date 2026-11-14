// libFuzzer target of the engine's pk3 reader (shared/qcommon/q_pk3.cpp), built alone with the stand-ins of shim/:
// the input is the bytes of an archive, written to a file, opened, and every file of it read and sought in. A pk3
// comes from anyone (a server sends it): nothing in it may make the reader read or write outside its buffers, loop
// or give back more than the size it announced. Built with AddressSanitizer and UBSan by zipbench.sh.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

#include "qcommon/q_shared.h"
#include "qcommon/q_pk3.h"

int pk3_warnings;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	static const std::string path = (std::filesystem::temp_directory_path() / ("pk3fuzz_" + std::to_string(getpid()) + ".pk3")).string();
	static uint8_t buf[1 << 16];
	FILE *out = fopen(path.c_str(), "wb");
	if (!out || fwrite(data, 1, size, out) != size) abort();
	fclose(out);

	pk3Archive_t *archive = PK3_Open(path.c_str());
	if (!archive) return 0;
	uint32_t rng = static_cast<uint32_t>(size) * 2654435761u + 1;
	const int count = PK3_NumEntries(archive);
	for (int i = 0; i < count && i < 32; i++) {
		pk3Entry_t e;
		PK3_GetEntry(archive, i, &e);
		if (strlen(e.name) > 65535) abort();
		pk3File_t *file = PK3_OpenFile(archive, i);
		if (!file) continue;
		// reads of 1 byte to 64 KB, one in five after a seek from any origin, chosen from the input's size
		for (int k = 0; k < 48; k++) {
			rng = rng * 1664525u + 1013904223u;
			if ((rng >> 29) == 0) PK3_Seek(file, static_cast<long>((rng >> 6) % 200001) - 100000, static_cast<int>((rng >> 3) % 3));
			const int len = 1 + static_cast<int>((rng >> 12) % sizeof buf);
			const int got = PK3_Read(file, buf, len);
			const int at = PK3_Tell(file);
			if (got > len || at < 0 || static_cast<uint32_t>(at) > e.size) abort();
		}
		PK3_CloseFile(file);
	}
	PK3_Close(archive);
	return 0;
}
