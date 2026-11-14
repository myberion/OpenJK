// The engine's own TGA loader (codemp/rd-common/tr_image_tga.cpp until it was removed, built here with stand-ins for the engine's
// functions: tga_shim/tr_common.h) against Wuffs, on every .tga of a folder: the same pixels or not, and the time of
// each (the best of 5 decodings per file, summed; the file already in memory for both).
// Usage: tga_check <corpus folder>   (built by imagebench.sh, which takes the loader from the last commit that holds it)
#include "tr_image_tga.cpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__STATIC_FUNCTIONS
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__AUX__BASE
#define WUFFS_CONFIG__MODULE__AUX__IMAGE
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__TARGA
#include "wuffs-v0.4.c"
std::vector<unsigned char> g_file;
static int ReadFile(const char *, void **out) {
	*out = g_file.data();	// the loader only rewrites its header's fields with their own values
	return (int)g_file.size();
}
static void FreeFile(void *) {}
Ri ri = {ReadFile, FreeFile};
static double Now() {
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
class Rgba : public wuffs_aux::DecodeImageCallbacks {
	wuffs_base__pixel_format SelectPixfmt(const wuffs_base__image_config &) override {
		return wuffs_base__make_pixel_format(WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL);
	}
};
int main(int argc, char **argv) {
	int n = 0, same = 0, engineFail = 0, wuffsFail = 0, differ = 0;
	double engineMs = 0, wuffsMs = 0, pixels = 0;
	for (const auto &e : std::filesystem::directory_iterator(argv[1])) {
		if (e.path().extension() != ".tga") continue;
		std::ifstream f(e.path(), std::ios::binary);
		g_file.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		n++;
		byte *pic = nullptr;
		int w = 0, h = 0;
		bool engineOk = true;
		try { LoadTGA("x", &pic, &w, &h); } catch (...) { engineOk = false; }
		engineOk = engineOk && pic;
		Rgba cb;
		wuffs_aux::sync_io::MemoryInput in(g_file.data(), g_file.size());
		wuffs_aux::DecodeImageResult res = wuffs_aux::DecodeImage(cb, in);
		const bool wuffsOk = res.error_message.empty();
		engineFail += !engineOk;
		wuffsFail += !wuffsOk;
		if (!engineOk || !wuffsOk) { printf("%s: engine %s, wuffs %s %s\n", e.path().filename().string().c_str(), engineOk ? "ok" : "FAIL", wuffsOk ? "ok" : "FAIL", res.error_message.c_str()); continue; }
		wuffs_base__table_u8 t = res.pixbuf.plane(0);
		bool eq = (int)res.pixbuf.pixcfg.width() == w && (int)res.pixbuf.pixcfg.height() == h;
		int maxd = 0; long long nd = 0;
		for (int y = 0; eq && y < h; y++)
			for (int x = 0; x < w * 4; x++) {
				int d = abs((int)t.ptr[y * t.stride + x] - (int)pic[y * w * 4 + x]);
				if (d) { nd++; if (d > maxd) maxd = d; }
			}
		if (!eq || nd) { differ++; if (differ <= 12) printf("%s: differs (%dx%d, %lld bytes, max %d)\n", e.path().filename().string().c_str(), w, h, nd, maxd); }
		else same++;
		free(pic);
		double be = 1e30, bw = 1e30;
		for (int r = 0; r < 5; r++) {
			byte *p2 = nullptr;
			double t0 = Now();
			LoadTGA("x", &p2, &w, &h);
			be = std::min(be, Now() - t0);
			free(p2);
			Rgba cb2;
			wuffs_aux::sync_io::MemoryInput in2(g_file.data(), g_file.size());
			t0 = Now();
			wuffs_aux::DecodeImageResult res2 = wuffs_aux::DecodeImage(cb2, in2);
			bw = std::min(bw, Now() - t0);
		}
		engineMs += be;
		wuffsMs += bw;
		pixels += (double)w * h;
	}
	printf("TGA files %d: same %d, differ %d, engine failed %d, wuffs failed %d\n", n, same, differ, engineFail, wuffsFail);
	if (same + differ) printf("engine's loader %.1f ms (%.0f MP/s), Wuffs %.1f ms (%.0f MP/s)\n", engineMs, pixels / 1e3 / engineMs, wuffsMs, pixels / 1e3 / wuffsMs);
}
