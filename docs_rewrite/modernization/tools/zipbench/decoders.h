// One raw-deflate decoder and one CRC-32 function per library built in (WITH_...), each inflating a whole entry held
// in memory into a buffer of its size, as a reader of pk3 files that knows both sizes from the directory would.
#pragma once
#include <cstddef>
#include <cstdint>

using Inflate = bool (*)(const uint8_t *in, size_t n, uint8_t *out, size_t outn);
using Crc = uint32_t (*)(const uint8_t *p, size_t n);

#ifdef WITH_ZLIB	// zlib and the forks that keep its names: one of them per program
#include <zlib.h>
static bool InfZlib(const uint8_t *in, size_t n, uint8_t *out, size_t outn) {
	z_stream s{};
	if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return false;
	s.next_in = const_cast<Bytef *>(in);
	s.avail_in = static_cast<uInt>(n);
	s.next_out = out;
	s.avail_out = static_cast<uInt>(outn);
	const bool ok = inflate(&s, Z_FINISH) == Z_STREAM_END && s.total_out == outn;
	inflateEnd(&s);
	return ok;
}
static uint32_t CrcZlib(const uint8_t *p, size_t n) {
	static const uLong start = crc32(0, Z_NULL, 0);	// zlib's documented first call; Chromium's fork detects the processor in it
	return static_cast<uint32_t>(crc32(start, p, static_cast<uInt>(n)));
}
#endif

#ifdef WITH_ZLIBNG
#include <zlib-ng.h>
static bool InfZlibNg(const uint8_t *in, size_t n, uint8_t *out, size_t outn) {
	zng_stream s{};
	if (zng_inflateInit2(&s, -15) != Z_OK) return false;
	s.next_in = in;
	s.avail_in = static_cast<uint32_t>(n);
	s.next_out = out;
	s.avail_out = static_cast<uint32_t>(outn);
	const bool ok = zng_inflate(&s, Z_FINISH) == Z_STREAM_END && s.total_out == outn;
	zng_inflateEnd(&s);
	return ok;
}
static uint32_t CrcZlibNg(const uint8_t *p, size_t n) { return zng_crc32(0, p, static_cast<uint32_t>(n)); }
#endif

#ifdef WITH_LIBDEFLATE
#include <libdeflate.h>
static bool InfLibdeflate(const uint8_t *in, size_t n, uint8_t *out, size_t outn) {
	static libdeflate_decompressor *d = libdeflate_alloc_decompressor();
	size_t got = 0;
	return libdeflate_deflate_decompress(d, in, n, out, outn, &got) == LIBDEFLATE_SUCCESS && got == outn;
}
static uint32_t CrcLibdeflate(const uint8_t *p, size_t n) { return libdeflate_crc32(0, p, n); }
#endif

#ifdef WITH_MINIZ
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>
static bool InfMiniz(const uint8_t *in, size_t n, uint8_t *out, size_t outn) { return tinfl_decompress_mem_to_mem(out, outn, in, n, 0) == outn; }
static uint32_t CrcMiniz(const uint8_t *p, size_t n) { return static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, p, n)); }
#endif

#ifdef WITH_ISAL
extern "C" {
#include <crc.h>
#include <igzip_lib.h>
}
static bool InfIsal(const uint8_t *in, size_t n, uint8_t *out, size_t outn) {
	static inflate_state st;
	isal_inflate_init(&st);
	st.next_in = const_cast<uint8_t *>(in);
	st.avail_in = static_cast<uint32_t>(n);
	st.next_out = out;
	st.avail_out = static_cast<uint32_t>(outn);
	st.crc_flag = ISAL_DEFLATE;
	return isal_inflate_stateless(&st) == ISAL_DECOMP_OK && st.total_out == outn;
}
static uint32_t CrcIsal(const uint8_t *p, size_t n) { return crc32_gzip_refl(0, p, n); }
#endif

#ifdef WITH_STB
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
static bool InfStb(const uint8_t *in, size_t n, uint8_t *out, size_t outn) {
	return stbi_zlib_decode_noheader_buffer(reinterpret_cast<char *>(out), static_cast<int>(outn), reinterpret_cast<const char *>(in),
		static_cast<int>(n)) == static_cast<int>(outn);
}
#endif

#ifdef WITH_WUFFS
#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__STATIC_FUNCTIONS
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#include "wuffs-v0.4.c"
static bool InfWuffs(const uint8_t *in, size_t n, uint8_t *out, size_t outn) {
	static wuffs_deflate__decoder dec;
	static uint8_t work[WUFFS_DEFLATE__DECODER_WORKBUF_LEN_MAX_INCL_WORST_CASE];
	if (wuffs_deflate__decoder__initialize(&dec, sizeof dec, WUFFS_VERSION, WUFFS_INITIALIZE__LEAVE_INTERNAL_BUFFERS_UNINITIALIZED).repr) return false;
	wuffs_base__io_buffer dst = wuffs_base__ptr_u8__writer(out, outn);
	wuffs_base__io_buffer src = wuffs_base__ptr_u8__reader(const_cast<uint8_t *>(in), n, true);
	return !wuffs_deflate__decoder__transform_io(&dec, &dst, &src, wuffs_base__make_slice_u8(work, sizeof work)).repr && dst.meta.wi == outn;
}
static uint32_t CrcWuffs(const uint8_t *p, size_t n) {
	wuffs_crc32__ieee_hasher h;
	if (wuffs_crc32__ieee_hasher__initialize(&h, sizeof h, WUFFS_VERSION, 0).repr) return 0;
	return wuffs_crc32__ieee_hasher__update_u32(&h, wuffs_base__make_slice_u8(const_cast<uint8_t *>(p), n));
}
#endif
