/* Compares math libraries over the input ranges the game uses (plan step 14: identical results on Linux and Windows,
   survey 12-deterministic-math-survey.md). For each function and library: an FNV-1a hash of the results (equal hashes
   on two builds mean bit-identical results), the number of results that differ from CORE-MATH (correctly rounded, the
   reference) and the largest difference in ulp, and the time per call (independent calls, best of several runs).
   Libraries: the C library, LLVM libc (lm.cpp), CORE-MATH when built with HAVE_CM (GCC and Clang only). For sinpif,
   cospif and tanpif (sin, cos, tan of pi x) the C library column is the radians path the game used (x times pi in
   float, then sinf), and for rsqrtf it is 1 / sqrtf. Then the C library's text conversions: parsing (strtof, strtod,
   atof, sscanf %f) of decimal strings built from integers, and printing (printf %f, %g, %.9g, %.0f of halves, %.1f of
   quarters, %.17g), one hash each.
   Build: build.sh (GCC, Clang) or build.bat (MSVC). */
#define _USE_MATH_DEFINES /* M_PI with MSVC */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

typedef float ( *f1f_t )( float );
typedef float ( *f2f_t )( float, float );
typedef double ( *f1d_t )( double );
typedef double ( *f2d_t )( double, double );

#define DECL( p ) \
	float p##_sinf( float ); float p##_cosf( float ); float p##_tanf( float ); float p##_asinf( float ); \
	float p##_acosf( float ); float p##_atanf( float ); float p##_atan2f( float, float ); float p##_logf( float ); \
	float p##_powf( float, float ); double p##_sin( double ); double p##_cos( double ); double p##_tan( double ); \
	double p##_acos( double ); double p##_atan( double ); double p##_atan2( double, double ); \
	double p##_pow( double, double ); float p##_sinpif( float ); float p##_cospif( float ); 	float p##_tanpif( float ); float p##_rsqrtf( float );
DECL( lm )
/* the game's entry point (shared/qcommon/q_libm.cpp) */
float Q_strtof( const char *str, const char **end );
int Q_ScanFloats( const char *str, float *out, int count );
#ifdef HAVE_CM
DECL( cr )
#define NLIB 3
#else
#define NLIB 2
#endif
static const char *const libName[3] = { "libc", "llvm", "core" };

#if defined( HAVE_CM ) && defined( _WIN32 )
/* CORE-MATH's __builtin_roundeven becomes a call to C23 roundeven without SSE4.1, which the UCRT lacks; rint rounds
   to nearest even in the default rounding mode, exactly */
double roundeven( double x ) { return rint( x ); }
float roundevenf( float x ) { return rintf( x ); }
#endif

/* the C library's functions through wrappers, so that no compiler replaces or evaluates the calls itself */
static float c_sinf( float x ) { return sinf( x ); }
static float c_cosf( float x ) { return cosf( x ); }
static float c_tanf( float x ) { return tanf( x ); }
static float c_asinf( float x ) { return asinf( x ); }
static float c_acosf( float x ) { return acosf( x ); }
static float c_atanf( float x ) { return atanf( x ); }
static float c_atan2f( float y, float x ) { return atan2f( y, x ); }
static float c_logf( float x ) { return logf( x ); }
static float c_powf( float x, float y ) { return powf( x, y ); }
static double c_sin( double x ) { return sin( x ); }
static double c_cos( double x ) { return cos( x ); }
static double c_tan( double x ) { return tan( x ); }
static double c_acos( double x ) { return acos( x ); }
static double c_atan( double x ) { return atan( x ); }
static double c_atan2( double y, double x ) { return atan2( y, x ); }
static double c_pow( double x, double y ) { return pow( x, y ); }
static float c_sinpif( float x ) { return sinf( x * (float)M_PI ); }
static float c_cospif( float x ) { return cosf( x * (float)M_PI ); }
static float c_tanpif( float x ) { return tanf( x * (float)M_PI ); }
static float c_rsqrtf( float x ) { return 1.0f / sqrtf( x ); }

enum { K_F1, K_F2, K_D1, K_D2 };
enum { R_ANGLE, R_TAN, R_UNIT, R_RATIO, R_LOG, R_DIR2, R_POW2, R_HALF, R_TANHALF, R_LEN2, R_TIME };

typedef struct {
	f1f_t f1f;
	f2f_t f2f;
	f1d_t f1d;
	f2d_t f2d;
} impl_t;

typedef struct {
	const char *name;
	int kind, range;
	impl_t lib[3];
} func_t;

#ifdef HAVE_CM
#define CR( ... ) , __VA_ARGS__
#else
#define CR( ... )
#endif
#define F1( n, r ) { #n, K_F1, r, { { c_##n }, { lm_##n } CR( { cr_##n } ) } }
#define F2( n, r ) { #n, K_F2, r, { { 0, c_##n }, { 0, lm_##n } CR( { 0, cr_##n } ) } }
#define D1( n, r, label ) { label, K_D1, r, { { 0, 0, c_##n }, { 0, 0, lm_##n } CR( { 0, 0, cr_##n } ) } }
#define D2( n, r ) { #n, K_D2, r, { { 0, 0, 0, c_##n }, { 0, 0, 0, lm_##n } CR( { 0, 0, 0, cr_##n } ) } }

static const func_t funcs[] = {
	F1( sinf, R_ANGLE ), F1( cosf, R_ANGLE ), F1( tanf, R_TAN ), F1( asinf, R_UNIT ), F1( acosf, R_UNIT ),
	F1( atanf, R_RATIO ), F2( atan2f, R_DIR2 ), F1( logf, R_LOG ), F2( powf, R_POW2 ),
	D1( sin, R_ANGLE, "sin" ), D1( cos, R_ANGLE, "cos" ), D1( tan, R_TAN, "tan" ), D1( acos, R_UNIT, "acos" ),
	D1( atan, R_RATIO, "atan" ), D2( atan2, R_DIR2 ), D2( pow, R_POW2 ),
	D1( sin, R_TIME, "sin(time)" ), D1( cos, R_TIME, "cos(time)" ),
	F1( sinpif, R_HALF ), F1( cospif, R_HALF ), F1( tanpif, R_TANHALF ), F1( rsqrtf, R_LEN2 ),
};
#define NFUNC ( (int)( sizeof funcs / sizeof funcs[0] ) )

#define N 2097152 /* inputs per function */

/* input i of a range; float ranges hold float values (the game passes floats, promoted for the double functions) */
static void input( int range, int i, double *a, double *b ) {
	const float deg = -720.0f + (float)i * ( 1440.0f / N );
	const float y = -4096.0f + (float)i * ( 8192.0f / N );
	*b = 0;
	switch ( range ) {
	case R_ANGLE: /* degrees over [-720, 720) to radians, as AngleVectors does (float * double) */
		*a = (float)( deg * ( M_PI * 2 / 360 ) );
		break;
	case R_TAN: /* within (-80, 80) degrees */
		*a = (float)( ( deg / 9.0f ) * ( M_PI * 2 / 360 ) );
		break;
	case R_UNIT: /* cosines over [-1, 1] */
		*a = -1.0f + (float)i * ( 2.0f / N );
		break;
	case R_RATIO: /* direction ratios over [-64, 64) */
		*a = y / 64.0f;
		break;
	case R_LOG: /* positive values */
		*a = 1e-6f + (float)i * ( 100000.0f / N );
		break;
	case R_DIR2: /* direction components over [-4096, 4096) for vectoangles' atan2 */
		*a = y;
		*b = 4096.0f - (float)( i * 7 % N ) * ( 8192.0f / N );
		break;
	case R_POW2: /* positive bases and fractional exponents */
		*a = 1e-3f + (float)i * ( 64.0f / N );
		*b = -2.0f + (float)( i * 13 % N ) * ( 4.0f / N );
		break;
	case R_HALF: /* degrees over [-720, 720) as half turns, as AngleVectors now does */
		*a = deg / 180.0f;
		break;
	case R_TANHALF: /* within (-80, 80) degrees, as half turns */
		*a = ( deg / 9.0f ) / 180.0f;
		break;
	case R_LEN2: /* squared lengths */
		*a = 1e-6f + (float)i * ( 1e8f / N );
		break;
	default: /* R_TIME: a time in seconds over [0, 86100), as sin( serverTime * 0.001 ) */
		*a = (double)( i * 41 ) * 0.001;
		break;
	}
}

static double call( const func_t *f, int lib, double a, double b ) {
	const impl_t *m = &f->lib[lib];
	switch ( f->kind ) {
	case K_F1: return m->f1f( (float)a );
	case K_F2: return m->f2f( (float)a, (float)b );
	case K_D1: return m->f1d( a );
	default: return m->f2d( a, b );
	}
}

static uint64_t fnv( uint64_t h, const void *p, size_t n ) {
	const unsigned char *c = (const unsigned char *)p;
	size_t i;
	for ( i = 0; i < n; i++ ) {
		h ^= c[i];
		h *= 1099511628211ull;
	}
	return h;
}

/* distance in ulp between two results of the same format, through their ordered bit patterns */
static uint64_t ulps( int isFloat, double x, double y ) {
	if ( isFloat ) {
		const float fx = (float)x, fy = (float)y;
		int32_t ix, iy;
		int64_t d;
		memcpy( &ix, &fx, 4 );
		memcpy( &iy, &fy, 4 );
		if ( ix < 0 ) ix = INT32_MIN - ix;
		if ( iy < 0 ) iy = INT32_MIN - iy;
		d = (int64_t)ix - iy;
		return (uint64_t)( d < 0 ? -d : d );
	} else {
		int64_t ix, iy;
		memcpy( &ix, &x, 8 );
		memcpy( &iy, &y, 8 );
		if ( ix < 0 ) ix = INT64_MIN - ix;
		if ( iy < 0 ) iy = INT64_MIN - iy;
		return ix > iy ? (uint64_t)ix - (uint64_t)iy : (uint64_t)iy - (uint64_t)ix;
	}
}

static double now( void ) {
#ifdef _WIN32
	LARGE_INTEGER c, f;
	QueryPerformanceCounter( &c );
	QueryPerformanceFrequency( &f );
	return (double)c.QuadPart / (double)f.QuadPart;
#else
	struct timespec t;
	clock_gettime( CLOCK_MONOTONIC, &t );
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
#endif
}

#define TN 4096 /* inputs in the timing loop */
#define TREPS 256
#define TRUNS 7
static volatile double sink;

/* nanoseconds per call over TN inputs spread over the range, best of TRUNS runs */
static double timeCalls( const func_t *f, int lib ) {
	static float fa[TN], fb[TN];
	static double da[TN], db[TN];
	const impl_t *m = &f->lib[lib];
	double best = 1e30, acc = 0;
	int i, r, run;
	for ( i = 0; i < TN; i++ ) {
		input( f->range, i * ( N / TN ), &da[i], &db[i] );
		fa[i] = (float)da[i];
		fb[i] = (float)db[i];
	}
	for ( run = 0; run < TRUNS; run++ ) {
		const double t0 = now();
		double t;
		for ( r = 0; r < TREPS; r++ ) {
			switch ( f->kind ) {
			case K_F1: for ( i = 0; i < TN; i++ ) acc += m->f1f( fa[i] ); break;
			case K_F2: for ( i = 0; i < TN; i++ ) acc += m->f2f( fa[i], fb[i] ); break;
			case K_D1: for ( i = 0; i < TN; i++ ) acc += m->f1d( da[i] ); break;
			default: for ( i = 0; i < TN; i++ ) acc += m->f2d( da[i], db[i] ); break;
			}
		}
		t = now() - t0;
		if ( t < best ) best = t;
	}
	sink = acc;
	return best * 1e9 / ( (double)TREPS * TN );
}

static uint64_t splitmix( uint64_t *s ) {
	uint64_t z = ( *s += 0x9e3779b97f4a7c15ull );
	z = ( z ^ ( z >> 30 ) ) * 0xbf58476d1ce4e5b9ull;
	z = ( z ^ ( z >> 27 ) ) * 0x94d049bb133111ebull;
	return z ^ ( z >> 31 );
}

#define TEXT_N 1048576 /* strings and values per text conversion */

/* the C library's text conversions: one hash per conversion over TEXT_N inputs */
static void textConversions( void ) {
	static const long pow10[10] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000 };
	enum { P_STRTOF, P_STRTOD, P_ATOF, P_SSCANF, F_F, F_G, F_9G, F_HALF0F, F_QUARTER1F, F_17G, NTEXT };
	static const char *const name[NTEXT] = { "strtof", "strtod", "atof", "sscanf %f", "printf %f", "printf %g",
		"printf %.9g", "printf %.0f halves", "printf %.1f quarters", "printf %.17g" };
	uint64_t hash[NTEXT], seed = 1;
	char buf[128];
	int i, k;
	for ( k = 0; k < NTEXT; k++ ) hash[k] = 14695981039346656037ull;
	for ( i = 0; i < TEXT_N; i++ ) {
		/* a decimal string built from integers (integer printing is exact everywhere): sign, integer part, 1 to 9
		   fraction digits, an exponent one time in four */
		const uint64_t r = splitmix( &seed );
		const int digits = 1 + (int)( ( r >> 18 ) % 9 );
		int len = snprintf( buf, sizeof buf, "%s%d.%0*ld", ( r & 1 ) ? "-" : "", (int)( ( r >> 1 ) % 100000 ), digits,
			(long)( ( r >> 22 ) % (uint64_t)pow10[digits] ) );
		float f;
		double d;
		uint32_t bits;
		if ( ( ( r >> 60 ) & 3 ) == 0 ) {
			snprintf( buf + len, sizeof buf - (size_t)len, "e%d", (int)( ( r >> 40 ) % 61 ) - 30 );
		}
		f = strtof( buf, NULL );
		hash[P_STRTOF] = fnv( hash[P_STRTOF], &f, sizeof f );
		d = strtod( buf, NULL );
		hash[P_STRTOD] = fnv( hash[P_STRTOD], &d, sizeof d );
		d = atof( buf );
		hash[P_ATOF] = fnv( hash[P_ATOF], &d, sizeof d );
		f = 0;
		sscanf( buf, "%f", &f );
		hash[P_SSCANF] = fnv( hash[P_SSCANF], &f, sizeof f );
		/* a float of either sign with an exponent between 2^-17 and 2^23, any mantissa */
		bits = (uint32_t)( splitmix( &seed ) >> 32 );
		bits = ( bits & 0x807fffffu ) | ( ( 110u + ( bits >> 23 & 0xff ) % 41u ) << 23 );
		memcpy( &f, &bits, 4 );
		len = snprintf( buf, sizeof buf, "%f", f );
		hash[F_F] = fnv( hash[F_F], buf, (size_t)len );
		len = snprintf( buf, sizeof buf, "%g", f );
		hash[F_G] = fnv( hash[F_G], buf, (size_t)len );
		len = snprintf( buf, sizeof buf, "%.9g", f );
		hash[F_9G] = fnv( hash[F_9G], buf, (size_t)len );
		len = snprintf( buf, sizeof buf, "%.17g", (double)f );
		hash[F_17G] = fnv( hash[F_17G], buf, (size_t)len );
		/* exact ties: k + 0.5 to no decimal, k / 4 to one decimal (the rule for halves differs between libraries) */
		len = snprintf( buf, sizeof buf, "%.0f", (double)( i % 200001 - 100000 ) + 0.5 );
		hash[F_HALF0F] = fnv( hash[F_HALF0F], buf, (size_t)len );
		len = snprintf( buf, sizeof buf, "%.1f", (double)( i % 400001 - 200000 ) * 0.25 );
		hash[F_QUARTER1F] = fnv( hash[F_QUARTER1F], buf, (size_t)len );
	}
	for ( k = 0; k < NTEXT; k++ ) printf( "text %-20s %016llx\n", name[k], (unsigned long long)hash[k] );
	/* the game's parser (shared/qcommon/q_libm.cpp, correctly rounded) over the same strings: glibc's strtof hash */
	{
		uint64_t h = 14695981039346656037ull;
		seed = 1;
		for ( i = 0; i < TEXT_N; i++ ) {
			const uint64_t r = splitmix( &seed );
			const int digits = 1 + (int)( ( r >> 18 ) % 9 );
			const int len = snprintf( buf, sizeof buf, "%s%d.%0*ld", ( r & 1 ) ? "-" : "", (int)( ( r >> 1 ) % 100000 ),
				digits, (long)( ( r >> 22 ) % (uint64_t)pow10[digits] ) );
			float f;
			if ( ( ( r >> 60 ) & 3 ) == 0 ) {
				snprintf( buf + len, sizeof buf - (size_t)len, "e%d", (int)( ( r >> 40 ) % 61 ) - 30 );
			}
			splitmix( &seed ); /* the float drawn for printing above */
			f = Q_strtof( buf, NULL );
			h = fnv( h, &f, sizeof f );
		}
		printf( "text %-20s %016llx\n", "Q_strtof", (unsigned long long)h );
	}
	snprintf( buf, sizeof buf, "%.0f %.0f %.0f %.0f %.1f %.1f", 0.5, 1.5, 2.5, -0.5, 0.25, 0.35 );
	printf( "text sample: %s\n", buf );
}

/* Q_ScanFloats as sscanf( s, "%f %f %f", ... ): the count, and correctly rounded values (16777217 and
   83034.89453125 lie exactly halfway between two floats: the even one, 0x4b800000 and 0x47a22d72) */
static void scanCheck( void ) {
	static const struct {
		const char *s;
		int n;
		uint32_t bits[3];
	} cases[] = {
		{ "", -1, { 0 } }, { " \t\n", -1, { 0 } }, { "x", 0, { 0 } }, { "1", 1, { 0x3f800000u } },
		{ " 1 2", 2, { 0x3f800000u, 0x40000000u } }, { "1 x 3", 1, { 0x3f800000u } },
		{ "1 2 3 4", 3, { 0x3f800000u, 0x40000000u, 0x40400000u } },
		{ "-1.5e3\t.25\n7", 3, { 0xc4bb8000u, 0x3e800000u, 0x40e00000u } },
		{ "16777217 83034.89453125 0.1", 3, { 0x4b800000u, 0x47a22d72u, 0x3dcccccdu } },
	};
	int c, k, failures = 0;
	for ( c = 0; c < (int)( sizeof cases / sizeof cases[0] ); c++ ) {
		float out[3] = { 0, 0, 0 };
		const int n = Q_ScanFloats( cases[c].s, out, 3 );
		int ok = n == cases[c].n;
		for ( k = 0; k < n && k < 3; k++ ) {
			uint32_t b;
			memcpy( &b, &out[k], 4 );
			ok = ok && b == cases[c].bits[k];
		}
		if ( !ok ) {
			printf( "scan check FAIL: \"%s\" read %d\n", cases[c].s, n );
			failures++;
		}
	}
	printf( "scan check %s\n", failures ? "FAIL" : "ok" );
}

int main( void ) {
	int fi, lib, i;
#if defined( _MSC_VER ) && !defined( __clang__ )
	printf( "compiler MSVC %d", _MSC_FULL_VER );
#elif defined( __clang__ )
	printf( "compiler clang %s", __clang_version__ );
#else
	printf( "compiler GCC %s", __VERSION__ );
#endif
#if defined( __FMA__ ) || defined( __AVX2__ )
	printf( ", %d-bit, FMA allowed\n", (int)sizeof( void * ) * 8 );
#else
	printf( ", %d-bit, no FMA\n", (int)sizeof( void * ) * 8 );
#endif
	printf( "%-10s %-5s %-16s %9s %8s %8s\n", "function", "lib", "hash", "!=core", "max ulp", "ns/call" );
	for ( fi = 0; fi < NFUNC; fi++ ) {
		const func_t *f = &funcs[fi];
		const int isFloat = f->kind == K_F1 || f->kind == K_F2;
		uint64_t hash[3], diff[3] = { 0, 0, 0 }, maxu[3] = { 0, 0, 0 };
		for ( lib = 0; lib < NLIB; lib++ ) hash[lib] = 14695981039346656037ull;
		for ( i = 0; i < N; i++ ) {
			double a, b, r[3];
			input( f->range, i, &a, &b );
			for ( lib = 0; lib < NLIB; lib++ ) {
				r[lib] = call( f, lib, a, b );
				if ( isFloat ) {
					const float v = (float)r[lib];
					hash[lib] = fnv( hash[lib], &v, sizeof v );
				} else {
					hash[lib] = fnv( hash[lib], &r[lib], sizeof r[lib] );
				}
			}
#ifdef HAVE_CM
			for ( lib = 0; lib < 2; lib++ ) {
				const uint64_t u = ulps( isFloat, r[lib], r[2] );
				if ( u ) diff[lib]++;
				if ( u > maxu[lib] ) maxu[lib] = u;
			}
#endif
		}
		for ( lib = 0; lib < NLIB; lib++ ) {
			const double ns = timeCalls( f, lib );
#ifdef HAVE_CM
			printf( "%-10s %-5s %016llx %9llu %8llu %8.2f\n", f->name, libName[lib], (unsigned long long)hash[lib],
				(unsigned long long)diff[lib], (unsigned long long)maxu[lib], ns );
#else
			printf( "%-10s %-5s %016llx %9s %8s %8.2f\n", f->name, libName[lib], (unsigned long long)hash[lib], "-", "-",
				ns );
#endif
		}
		(void)maxu;
		(void)diff;
	}
	(void)ulps;
	textConversions();
	scanCheck();
	return 0;
}
