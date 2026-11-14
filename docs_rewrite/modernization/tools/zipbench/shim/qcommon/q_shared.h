// Stand-ins for what shared/qcommon/q_pk3.cpp takes from the engine, so that pk3check.cpp and fuzz_pk3.cpp build it
// alone: this folder comes before the engine's in their include path.
#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef unsigned char byte;
typedef int qboolean;
#define qfalse 0
#define qtrue 1

#define MAX_OSPATH 260
#define S_COLOR_YELLOW "^3"
#define ERR_FATAL 0
#define TAG_FILESYS 0

enum { FS_SEEK_CUR, FS_SEEK_END, FS_SEEK_SET };

extern int pk3_warnings;	// counted, not printed

inline void Q_strncpyz( char *dest, const char *src, int destsize ) {
	strncpy( dest, src, static_cast<size_t>( destsize ) - 1 );
	dest[destsize - 1] = 0;
}
inline void Com_Printf( const char *, ... ) { pk3_warnings++; }
inline void Com_Error( int, const char *msg, ... ) {
	fprintf( stderr, "Com_Error: %s\n", msg );
	abort();
}
inline void *Z_Malloc( int size, int, qboolean zero ) { return zero ? calloc( 1, static_cast<size_t>( size ) ) : malloc( static_cast<size_t>( size ) ); }
inline void Z_Free( void *p ) { free( p ); }
