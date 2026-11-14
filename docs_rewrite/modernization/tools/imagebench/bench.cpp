// Decoding benchmark of image libraries on the game's own images: every file of a folder decoded to RGBA8 by each
// decoder built in, the best of RUNS decodings per file, and each result compared with a reference decoding.
// Usage: bench <corpus folder> <reference folder> [write]
//   write: the first decoder of each format writes its pixels to the reference folder (the reference of every build)
//   hash: the same with a hash of the pixels instead (a large corpus): equal or not, no difference measured
#include <algorithm>
#include <chrono>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

struct Image {
	int w = 0, h = 0;
	std::vector<uint8_t> rgba;
};

// decodes; fills *out when it is not null (the timed runs pass null: the pixels are produced and dropped)
using Decoder = bool (*)(const uint8_t *data, size_t size, Image *out);

static void Keep(Image *out, int w, int h, const uint8_t *rgba, size_t stride) {
	if (!out) return;
	out->w = w;
	out->h = h;
	out->rgba.resize(static_cast<size_t>(w) * h * 4);
	for (int y = 0; y < h; y++) memcpy(&out->rgba[static_cast<size_t>(y) * w * 4], rgba + y * stride, static_cast<size_t>(w) * 4);
}

#ifdef WITH_LIBJPEG
extern "C" {
#include <jpeglib.h>
}
struct JpegError {
	jpeg_error_mgr mgr;
	jmp_buf jump;
};
static void JpegExit(j_common_ptr cinfo) { longjmp(reinterpret_cast<JpegError *>(cinfo->err)->jump, 1); }
static void JpegQuiet(j_common_ptr) {}
static bool DecLibjpeg(const uint8_t *data, size_t size, Image *out) {
	jpeg_decompress_struct cinfo;
	JpegError err;
	std::vector<uint8_t> pixels, row;
	cinfo.err = jpeg_std_error(&err.mgr);
	err.mgr.error_exit = JpegExit;
	err.mgr.output_message = JpegQuiet;
	if (setjmp(err.jump)) {
		jpeg_destroy_decompress(&cinfo);
		return false;
	}
	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, const_cast<unsigned char *>(data), static_cast<unsigned long>(size));
	jpeg_read_header(&cinfo, TRUE);
#ifdef JCS_EXTENSIONS
	cinfo.out_color_space = JCS_EXT_RGBA;	// libjpeg-turbo writes RGBA itself
#else
	cinfo.out_color_space = JCS_RGB;		// as the engine's LoadJPG: RGB rows, widened to RGBA after
#endif
	jpeg_start_decompress(&cinfo);
	const int w = static_cast<int>(cinfo.output_width), h = static_cast<int>(cinfo.output_height);
	pixels.resize(static_cast<size_t>(w) * h * 4);
#ifdef JCS_EXTENSIONS
	while (cinfo.output_scanline < cinfo.output_height) {
		JSAMPROW r = &pixels[static_cast<size_t>(cinfo.output_scanline) * w * 4];
		jpeg_read_scanlines(&cinfo, &r, 1);
	}
#else
	row.resize(static_cast<size_t>(w) * 3);
	while (cinfo.output_scanline < cinfo.output_height) {
		uint8_t *dst = &pixels[static_cast<size_t>(cinfo.output_scanline) * w * 4];
		JSAMPROW r = row.data();
		jpeg_read_scanlines(&cinfo, &r, 1);
		for (int x = 0; x < w; x++) {
			dst[x * 4 + 0] = row[x * 3 + 0];
			dst[x * 4 + 1] = row[x * 3 + 1];
			dst[x * 4 + 2] = row[x * 3 + 2];
			dst[x * 4 + 3] = 255;
		}
	}
#endif
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	Keep(out, w, h, pixels.data(), static_cast<size_t>(w) * 4);
	return true;
}
#endif

#ifdef WITH_LIBPNG
#include <png.h>
struct PngSource {
	const uint8_t *data;
	size_t size, at;
};
static void PngRead(png_structp png, png_bytep dst, png_size_t n) {
	PngSource *s = static_cast<PngSource *>(png_get_io_ptr(png));
	if (s->at + n > s->size) png_error(png, "short");
	memcpy(dst, s->data + s->at, n);
	s->at += n;
}
static bool DecLibpng(const uint8_t *data, size_t size, Image *out) {
	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	png_infop info = png_create_info_struct(png);
	std::vector<uint8_t> pixels;
	std::vector<png_bytep> rows;
	PngSource src{data, size, 0};
	if (setjmp(png_jmpbuf(png))) {
		png_destroy_read_struct(&png, &info, nullptr);
		return false;
	}
	png_set_read_fn(png, &src, PngRead);
	png_read_info(png, info);
	png_uint_32 w, h;
	int depth, type;
	png_get_IHDR(png, info, &w, &h, &depth, &type, nullptr, nullptr, nullptr);
	// the engine's LoadPNG takes 8-bit RGB and RGBA only; the other kinds are widened here so that every decoder
	// decodes every file
	png_set_expand(png);
	png_set_strip_16(png);
	png_set_gray_to_rgb(png);
	png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
	png_read_update_info(png, info);
	pixels.resize(static_cast<size_t>(w) * h * 4);
	rows.resize(h);
	for (png_uint_32 y = 0; y < h; y++) rows[y] = &pixels[static_cast<size_t>(y) * w * 4];
	png_read_image(png, rows.data());
	png_read_end(png, nullptr);
	png_destroy_read_struct(&png, &info, nullptr);
	Keep(out, static_cast<int>(w), static_cast<int>(h), pixels.data(), static_cast<size_t>(w) * 4);
	return true;
}
#endif

#ifdef WITH_SPNG
#include "spng.h"
static bool DecSpng(const uint8_t *data, size_t size, Image *out) {
	spng_ctx *ctx = spng_ctx_new(0);
	spng_ihdr ihdr;
	size_t n = 0;
	std::vector<uint8_t> pixels;
	bool ok = !spng_set_png_buffer(ctx, data, size) && !spng_get_ihdr(ctx, &ihdr) && !spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &n);
	if (ok) {
		pixels.resize(n);
		ok = !spng_decode_image(ctx, pixels.data(), n, SPNG_FMT_RGBA8, 0);
	}
	spng_ctx_free(ctx);
	if (ok) Keep(out, static_cast<int>(ihdr.width), static_cast<int>(ihdr.height), pixels.data(), static_cast<size_t>(ihdr.width) * 4);
	return ok;
}
#endif

#ifdef WITH_STB
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#include "stb_image.h"
static bool DecStb(const uint8_t *data, size_t size, Image *out) {
	int w, h, c;
	uint8_t *p = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &c, 4);
	if (!p) return false;
	Keep(out, w, h, p, static_cast<size_t>(w) * 4);
	stbi_image_free(p);
	return true;
}
#endif

#ifdef WITH_WUFFS
#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__STATIC_FUNCTIONS
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__AUX__BASE
#define WUFFS_CONFIG__MODULE__AUX__IMAGE
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__JPEG
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__MODULE__TARGA
#define WUFFS_CONFIG__MODULE__ZLIB
#include "wuffs-v0.4.c"
class RgbaCallbacks : public wuffs_aux::DecodeImageCallbacks {
	wuffs_base__pixel_format SelectPixfmt(const wuffs_base__image_config &) override {
		return wuffs_base__make_pixel_format(WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL);
	}
};
static bool DecWuffs(const uint8_t *data, size_t size, Image *out) {
	RgbaCallbacks callbacks;
	wuffs_aux::sync_io::MemoryInput input(data, size);
	wuffs_aux::DecodeImageResult res = wuffs_aux::DecodeImage(callbacks, input);
	if (!res.error_message.empty()) return false;
	wuffs_base__table_u8 t = res.pixbuf.plane(0);
	Keep(out, static_cast<int>(res.pixbuf.pixcfg.width()), static_cast<int>(res.pixbuf.pixcfg.height()), t.ptr, t.stride);
	return true;
}
#endif

struct Entry {
	const char *name, *ext;
	Decoder fn;
};
static const Entry kDecoders[] = {
#ifdef WITH_LIBJPEG
	{JPEG_NAME, "jpg", DecLibjpeg},
#endif
#ifdef WITH_LIBPNG
	{"libpng (tree)", "png", DecLibpng},
#endif
#ifdef WITH_SPNG
	{"libspng 0.7.4", "png", DecSpng},
#endif
#ifdef WITH_STB
	{"stb_image 2.32", "jpg", DecStb},
	{"stb_image 2.32", "png", DecStb},
	{"stb_image 2.32", "tga", DecStb},
#endif
#ifdef WITH_WUFFS
	{"Wuffs 0.4 alpha 12", "jpg", DecWuffs},
	{"Wuffs 0.4 alpha 12", "png", DecWuffs},
	{"Wuffs 0.4 alpha 12", "tga", DecWuffs},
#endif
};

static std::vector<uint8_t> ReadAll(const fs::path &p) {
	std::ifstream f(p, std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static uint64_t Hash(const std::vector<uint8_t> &v) {
	uint64_t h = 1469598103934665603ull;
	for (uint8_t b : v) h = (h ^ b) * 1099511628211ull;
	return h;
}

int main(int argc, char **argv) {
	const int RUNS = 5;
	if (argc < 3) return 1;
	const fs::path corpus = argv[1], refdir = argv[2];
	const bool hashed = argc > 3 && !strcmp(argv[3], "hash");
	const bool write = hashed || (argc > 3 && !strcmp(argv[3], "write"));
#ifdef _WIN32
	SetThreadAffinityMask(GetCurrentThread(), 1ull << 20);
#endif
	std::vector<fs::path> files;
	for (const auto &e : fs::directory_iterator(corpus)) files.push_back(e.path());
	std::sort(files.begin(), files.end());
	std::vector<std::string> written;

	printf("%-20s %-4s %5s %5s %10s %8s %9s %8s %8s\n", "decoder", "fmt", "files", "fail", "ms", "MP/s", "same", "max", "mean");
	for (const Entry &d : kDecoders) {
		int n = 0, fail = 0, same = 0, compared = 0, maxdiff = 0;
		double ms = 0, pixels = 0, sumdiff = 0, sumcount = 0;
		const bool isref = write && std::find(written.begin(), written.end(), d.ext) == written.end();
		for (const fs::path &p : files) {
			if (p.extension() != std::string(".") + d.ext) continue;
			const std::vector<uint8_t> data = ReadAll(p);
			Image img;
			n++;
			if (!d.fn(data.data(), data.size(), &img)) {
				fail++;
				continue;
			}
			double best = 1e30;
			for (int r = 0; r < RUNS; r++) {
				const auto t0 = std::chrono::steady_clock::now();
				d.fn(data.data(), data.size(), nullptr);
				best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
			}
			ms += best;
			pixels += static_cast<double>(img.w) * img.h;
			const fs::path refp = refdir / (p.filename().string() + ".raw");
			if (isref) {
				std::ofstream f(refp, std::ios::binary);
				f.write(reinterpret_cast<const char *>(&img.w), 4);
				f.write(reinterpret_cast<const char *>(&img.h), 4);
				if (hashed) {
					const uint64_t hsh = Hash(img.rgba);
					f.write(reinterpret_cast<const char *>(&hsh), 8);
				} else {
					f.write(reinterpret_cast<const char *>(img.rgba.data()), static_cast<std::streamsize>(img.rgba.size()));
				}
			}
			const std::vector<uint8_t> ref = ReadAll(refp);
			if (ref.size() == 16 && !memcmp(ref.data(), &img.w, 4) && !memcmp(ref.data() + 4, &img.h, 4)) {
				const uint64_t hsh = Hash(img.rgba);
				compared++;
				same += memcmp(ref.data() + 8, &hsh, 8) ? 0 : 1;
				continue;
			}
			if (ref.size() != img.rgba.size() + 8 || memcmp(ref.data(), &img.w, 4) || memcmp(ref.data() + 4, &img.h, 4)) continue;
			compared++;
			bool equal = true;
			for (size_t i = 0; i < img.rgba.size(); i++) {
				const int diff = abs(static_cast<int>(img.rgba[i]) - static_cast<int>(ref[i + 8]));
				if (diff) equal = false;
				maxdiff = std::max(maxdiff, diff);
				sumdiff += diff;
			}
			sumcount += static_cast<double>(img.rgba.size());
			same += equal ? 1 : 0;
		}
		if (isref) written.push_back(d.ext);
		if (!n) continue;
		printf("%-20s %-4s %5d %5d %10.1f %8.1f %4d/%-4d %8d %8.4f\n", d.name, d.ext, n, fail, ms, ms > 0 ? pixels / 1e6 / (ms / 1e3) : 0.0,
			same, compared, maxdiff, sumcount > 0 ? sumdiff / sumcount : 0.0);
	}
	return 0;
}
