// stand-ins for the engine's functions the TGA loader uses
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
typedef unsigned char byte;
typedef unsigned short word;
enum { qfalse, qtrue };
enum { TAG_TEMP_WORKSPACE, ERR_DROP };
#define Z_Malloc(n, tag, zero) malloc(n)
#define LittleShort(x) (x)
extern std::vector<unsigned char> g_file;
struct Ri {
	int (*FS_ReadFile)(const char *, void **);
	void (*FS_FreeFile)(void *);
};
extern Ri ri;
[[noreturn]] inline void Com_Error(int, const char *, ...) { throw std::runtime_error("tga"); }
