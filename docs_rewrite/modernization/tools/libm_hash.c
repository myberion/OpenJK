/* Hashes the C library's math functions over the input ranges the game uses, to compare platforms (plan step 14:
   identical results on Linux and Windows). Prints one FNV-1a hash per function; equal hashes mean bit-identical
   results on every input tested. Build: gcc -O2 -o libm_hash libm_hash.c -lm ; clang -O2 -o libm_hash.exe libm_hash.c */
#define _USE_MATH_DEFINES	/* M_PI with MSVC */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t fnv( uint64_t h, const void *p, size_t n ) {
	const unsigned char *b = (const unsigned char *)p;
	for ( size_t i = 0; i < n; i++ ) {
		h ^= b[i];
		h *= 1099511628211ull;
	}
	return h;
}

#define N 2097152	/* inputs per function */

static volatile float sink;

int main( void ) {
	const uint64_t basis = 14695981039346656037ull;
	uint64_t hsin = basis, hcos = basis, hsinf = basis, hcosf = basis, hatan2 = basis, hatan2f = basis;
	uint64_t hacos = basis, hacosf = basis, hasinf = basis, hlogf = basis, hfmodf = basis, hatan2d = basis;
	uint64_t htan = basis, htanf = basis, hatan = basis, hpow = basis, hpowf = basis;
	int i;

	for ( i = 0; i < N; i++ ) {
		/* angles in degrees over [-720, 720) on a 1/1456 grid, to radians as AngleVectors does (float * double) */
		const float deg = -720.0f + (float)i * (1440.0f / N);
		const float rad = (float)( deg * ( M_PI * 2 / 360 ) );
		const double ds = sin( rad ), dc = cos( rad );
		const float fs = sinf( rad ), fc = cosf( rad );
		/* direction components over [-4096, 4096) for vectoangles' atan2 */
		const float y = -4096.0f + (float)i * ( 8192.0f / N );
		const float x = 4096.0f - (float)( i * 7 % N ) * ( 8192.0f / N );
		const double a2 = atan2( y, x );
		const float a2f = atan2f( y, x );
		const double a2d = atan2( (double)y, (double)x ) * 180 / M_PI;
		/* cosines over [-1, 1] for acos and asin */
		const float c = -1.0f + (float)i * ( 2.0f / N );
		const double ac = acos( c );
		const float acf = acosf( c ), asf = asinf( c );
		/* positive values for logf, and fmodf of angles by 360 */
		const float lf = logf( 1e-6f + (float)i * ( 100000.0f / N ) );
		const float fm = fmodf( deg * 3.7f, 360.0f );
		/* tan of angles within (-80, 80) degrees, atan of ratios, pow of positive bases and fractional exponents */
		const float trad = (float)( ( deg / 9.0f ) * ( M_PI * 2 / 360 ) );
		const double tn = tan( trad );
		const float tnf = tanf( trad );
		const double at = atan( y / 64.0f );
		const float pb = 1e-3f + (float)i * ( 64.0f / N ), pe = -2.0f + (float)( i * 13 % N ) * ( 4.0f / N );
		const double pw = pow( pb, pe );
		const float pwf = powf( pb, pe );
		hsin = fnv( hsin, &ds, sizeof ds );
		hcos = fnv( hcos, &dc, sizeof dc );
		hsinf = fnv( hsinf, &fs, sizeof fs );
		hcosf = fnv( hcosf, &fc, sizeof fc );
		hatan2 = fnv( hatan2, &a2, sizeof a2 );
		hatan2f = fnv( hatan2f, &a2f, sizeof a2f );
		hatan2d = fnv( hatan2d, &a2d, sizeof a2d );
		hacos = fnv( hacos, &ac, sizeof ac );
		hacosf = fnv( hacosf, &acf, sizeof acf );
		hasinf = fnv( hasinf, &asf, sizeof asf );
		hlogf = fnv( hlogf, &lf, sizeof lf );
		hfmodf = fnv( hfmodf, &fm, sizeof fm );
		htan = fnv( htan, &tn, sizeof tn );
		htanf = fnv( htanf, &tnf, sizeof tnf );
		hatan = fnv( hatan, &at, sizeof at );
		hpow = fnv( hpow, &pw, sizeof pw );
		hpowf = fnv( hpowf, &pwf, sizeof pwf );
	}
	sink = 0;
	printf( "sin(float)    %016llx\n", (unsigned long long)hsin );
	printf( "cos(float)    %016llx\n", (unsigned long long)hcos );
	printf( "sinf          %016llx\n", (unsigned long long)hsinf );
	printf( "cosf          %016llx\n", (unsigned long long)hcosf );
	printf( "atan2(float)  %016llx\n", (unsigned long long)hatan2 );
	printf( "atan2f        %016llx\n", (unsigned long long)hatan2f );
	printf( "atan2 degrees %016llx\n", (unsigned long long)hatan2d );
	printf( "acos(float)   %016llx\n", (unsigned long long)hacos );
	printf( "acosf         %016llx\n", (unsigned long long)hacosf );
	printf( "asinf         %016llx\n", (unsigned long long)hasinf );
	printf( "logf          %016llx\n", (unsigned long long)hlogf );
	printf( "fmodf         %016llx\n", (unsigned long long)hfmodf );
	printf( "tan(float)    %016llx\n", (unsigned long long)htan );
	printf( "tanf          %016llx\n", (unsigned long long)htanf );
	printf( "atan(float)   %016llx\n", (unsigned long long)hatan );
	printf( "pow(float)    %016llx\n", (unsigned long long)hpow );
	printf( "powf          %016llx\n", (unsigned long long)hpowf );
	return 0;
}
