/* Random number generators for the game (plan step 14, survey random-numbers.md): for each candidate, the
   time per 32-bit value and an FNV-1a hash of its first outputs (equal hashes on two builds mean the same sequence).
   Stream generators are timed through their state (each value depends on the previous state); counter-based hashes
   on independent inputs (the command time and the draw index of Pmove's draws). Also the cost of a draw in [0, n):
   the game's current method, a multiply-shift, and Lemire's unbiased method.
   Build: cc -O2 -o rngbench rngbench.c (GCC, Clang); cl /O2 rngbench.c (MSVC). Run: rngbench */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#define N 100000000u /* values per timing run */
#define HASH_N 1000000u /* values hashed */
#define RUNS 5

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

static uint64_t fnv( uint64_t h, uint32_t v ) {
	int i;
	for ( i = 0; i < 4; i++ ) {
		h ^= ( v >> ( 8 * i ) ) & 0xff;
		h *= 1099511628211ull;
	}
	return h;
}

static uint32_t rotl32( uint32_t x, int k ) { return ( x << k ) | ( x >> ( 32 - k ) ); }
static uint64_t rotl64( uint64_t x, int k ) { return ( x << k ) | ( x >> ( 64 - k ) ); }

/* 64 x 64 -> 128-bit product, high and low words, in portable C (what wyrand needs) */
static uint64_t mul128( uint64_t a, uint64_t b, uint64_t *lo ) {
	const uint64_t al = (uint32_t)a, ah = a >> 32, bl = (uint32_t)b, bh = b >> 32;
	const uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
	const uint64_t mid = ( ll >> 32 ) + (uint32_t)lh + (uint32_t)hl;
	*lo = ( mid << 32 ) | (uint32_t)ll;
	return hh + ( lh >> 32 ) + ( hl >> 32 ) + ( mid >> 32 );
}

static uint64_t splitmix64( uint64_t *s ) {
	uint64_t z = ( *s += 0x9e3779b97f4a7c15ull );
	z = ( z ^ ( z >> 30 ) ) * 0xbf58476d1ce4e5b9ull;
	z = ( z ^ ( z >> 27 ) ) * 0x94d049bb133111ebull;
	return z ^ ( z >> 31 );
}

/* ------------------------------------------------------------------ stream generators, one 32-bit value per call */

typedef struct {
	uint32_t s32[16];
	uint64_t s64[4];
	uint32_t out[16];
	int used;
} state_t;

static void seed( state_t *st, uint64_t seedValue ) {
	int i;
	memset( st, 0, sizeof *st );
	for ( i = 0; i < 4; i++ ) st->s64[i] = splitmix64( &seedValue );
	for ( i = 0; i < 16; i++ ) st->s32[i] = (uint32_t)( splitmix64( &seedValue ) >> 32 );
	st->used = 16;
}

/* the game today: holdrand of q_math.c (MSVC's LCG), 15 bits per value; two values give 30 bits */
static uint32_t gen_qirand( state_t *st ) {
	st->s32[0] = st->s32[0] * 214013u + 2531011u;
	return st->s32[0] >> 17;
}
static uint32_t gen_crand( state_t *st ) { (void)st; return (uint32_t)rand(); }
static uint32_t gen_xoshiro128ss( state_t *st ) {
	uint32_t *s = st->s32;
	const uint32_t result = rotl32( s[1] * 5, 7 ) * 9, t = s[1] << 9;
	s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl32( s[3], 11 );
	return result;
}
static uint32_t gen_xoshiro128pp( state_t *st ) {
	uint32_t *s = st->s32;
	const uint32_t result = rotl32( s[0] + s[3], 7 ) + s[0], t = s[1] << 9;
	s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl32( s[3], 11 );
	return result;
}
static uint32_t gen_xoshiro256ss( state_t *st ) {
	uint64_t *s = st->s64;
	const uint64_t result = rotl64( s[1] * 5, 7 ) * 9, t = s[1] << 17;
	s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl64( s[3], 45 );
	return (uint32_t)( result >> 32 );
}
static uint32_t gen_xoshiro256pp( state_t *st ) {
	uint64_t *s = st->s64;
	const uint64_t result = rotl64( s[0] + s[3], 23 ) + s[0], t = s[1] << 17;
	s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl64( s[3], 45 );
	return (uint32_t)( result >> 32 );
}
static uint32_t gen_pcg32( state_t *st ) { /* XSH RR 64/32, increment from the seed */
	const uint64_t old = st->s64[0];
	uint32_t xorshifted, rot;
	st->s64[0] = old * 6364136223846793005ull + ( st->s64[1] | 1 );
	xorshifted = (uint32_t)( ( ( old >> 18 ) ^ old ) >> 27 );
	rot = (uint32_t)( old >> 59 );
	return ( xorshifted >> rot ) | ( xorshifted << ( ( 32 - rot ) & 31 ) );
}
static uint32_t gen_splitmix64( state_t *st ) { return (uint32_t)( splitmix64( &st->s64[0] ) >> 32 ); }
static uint32_t gen_sfc32( state_t *st ) {
	uint32_t *s = st->s32;
	const uint32_t t = s[0] + s[1] + s[3]++;
	s[0] = s[1] ^ ( s[1] >> 9 );
	s[1] = s[2] + ( s[2] << 3 );
	s[2] = rotl32( s[2], 21 ) + t;
	return t;
}
static uint32_t gen_romutrio32( state_t *st ) {
	uint32_t *s = st->s32;
	const uint32_t xp = s[0], yp = s[1], zp = s[2];
	s[0] = 3323815723u * zp;
	s[1] = rotl32( yp - xp, 6 );
	s[2] = rotl32( zp - yp, 22 );
	return xp;
}
static uint32_t gen_wyrand( state_t *st ) {
	uint64_t lo, hi;
	st->s64[0] += 0xa0761d6478bd642full;
	hi = mul128( st->s64[0], st->s64[0] ^ 0xe7037ed1a0b428dbull, &lo );
	return (uint32_t)( ( hi ^ lo ) >> 32 );
}
/* ChaCha with 8 rounds over a key from the seed and a 64-bit block counter: 16 values per block */
#define QR( a, b, c, d ) \
	a += b; d ^= a; d = rotl32( d, 16 ); c += d; b ^= c; b = rotl32( b, 12 ); \
	a += b; d ^= a; d = rotl32( d, 8 ); c += d; b ^= c; b = rotl32( b, 7 );
static uint32_t gen_chacha8( state_t *st ) {
	if ( st->used == 16 ) {
		uint32_t x[16];
		int i;
		st->s32[0] = 0x61707865u; st->s32[1] = 0x3320646eu; st->s32[2] = 0x79622d32u; st->s32[3] = 0x6b206574u;
		memcpy( x, st->s32, sizeof x );
		for ( i = 0; i < 8; i += 2 ) {
			QR( x[0], x[4], x[8], x[12] ) QR( x[1], x[5], x[9], x[13] ) QR( x[2], x[6], x[10], x[14] )
			QR( x[3], x[7], x[11], x[15] ) QR( x[0], x[5], x[10], x[15] ) QR( x[1], x[6], x[11], x[12] )
			QR( x[2], x[7], x[8], x[13] ) QR( x[3], x[4], x[9], x[14] )
		}
		for ( i = 0; i < 16; i++ ) st->out[i] = x[i] + st->s32[i];
		if ( ++st->s32[12] == 0 ) st->s32[13]++;
		st->used = 0;
	}
	return st->out[st->used++];
}

typedef uint32_t ( *gen_t )( state_t * );
static const struct {
	const char *name;
	gen_t f;
} gens[] = {
	{ "Q_irand LCG (15 bits)", gen_qirand }, { "C rand()", gen_crand }, { "xoshiro128**", gen_xoshiro128ss },
	{ "xoshiro128++", gen_xoshiro128pp }, { "xoshiro256**", gen_xoshiro256ss }, { "xoshiro256++", gen_xoshiro256pp },
	{ "pcg32", gen_pcg32 }, { "splitmix64", gen_splitmix64 }, { "sfc32", gen_sfc32 }, { "romutrio32", gen_romutrio32 },
	{ "wyrand", gen_wyrand }, { "chacha8", gen_chacha8 },
};

/* ------------------------------------------------------------------ hashes of (command time, draw index) */

static uint32_t h_pcg( uint32_t t, uint32_t i ) { /* Jarzynski and Olano's PCG hash over t combined with i */
	uint32_t state = ( t ^ ( i * 0x9e3779b9u ) ) * 747796405u + 2891336453u;
	const uint32_t word = ( ( state >> ( ( state >> 28 ) + 4 ) ) ^ state ) * 277803737u;
	return ( word >> 22 ) ^ word;
}
static uint32_t h_lowbias32( uint32_t t, uint32_t i ) { /* Wellons' lowbias32 */
	uint32_t x = t ^ ( i * 0x9e3779b9u );
	x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
	return x;
}
static uint32_t h_fmix32( uint32_t t, uint32_t i ) { /* MurmurHash3's finalizer */
	uint32_t x = t ^ ( i * 0x9e3779b9u );
	x ^= x >> 16; x *= 0x85ebca6bu; x ^= x >> 13; x *= 0xc2b2ae35u; x ^= x >> 16;
	return x;
}
static uint32_t h_splitmix64( uint32_t t, uint32_t i ) { /* SplitMix64's finalizer of the 64-bit (t, i) */
	uint64_t z = ( (uint64_t)t << 32 | i ) * 0x9e3779b97f4a7c15ull;
	z = ( z ^ ( z >> 30 ) ) * 0xbf58476d1ce4e5b9ull;
	z = ( z ^ ( z >> 27 ) ) * 0x94d049bb133111ebull;
	return (uint32_t)( ( z ^ ( z >> 31 ) ) >> 32 );
}
static uint32_t h_philox( uint32_t t, uint32_t i ) { /* Philox4x32-10, counter (t, i, 0, 0), key 0: first word */
	uint32_t c0 = t, c1 = i, c2 = 0, c3 = 0, k0 = 0, k1 = 0;
	int r;
	for ( r = 0; r < 10; r++ ) {
		const uint64_t p0 = (uint64_t)0xD2511F53u * c0, p1 = (uint64_t)0xCD9E8D57u * c2;
		const uint32_t n0 = (uint32_t)( p1 >> 32 ) ^ c1 ^ k0, n2 = (uint32_t)( p0 >> 32 ) ^ c3 ^ k1;
		c1 = (uint32_t)p1; c3 = (uint32_t)p0; c0 = n0; c2 = n2;
		k0 += 0x9E3779B9u; k1 += 0xBB67AE85u;
	}
	return c0;
}

typedef uint32_t ( *hash_t )( uint32_t, uint32_t );
static const struct {
	const char *name;
	hash_t f;
} hashes[] = {
	{ "pcg hash", h_pcg }, { "lowbias32", h_lowbias32 }, { "murmur3 fmix32", h_fmix32 },
	{ "splitmix64 finalizer", h_splitmix64 }, { "philox4x32-10", h_philox },
};

static volatile uint32_t sink;

/* the generators as the game would call them: inlined, the state a global variable */
static uint32_t g128[4], g32;
static uint64_t g256[4];
static inline uint32_t inl_xoshiro128pp( void ) {
	const uint32_t result = rotl32( g128[0] + g128[3], 7 ) + g128[0], t = g128[1] << 9;
	g128[2] ^= g128[0]; g128[3] ^= g128[1]; g128[1] ^= g128[2]; g128[0] ^= g128[3]; g128[2] ^= t;
	g128[3] = rotl32( g128[3], 11 );
	return result;
}
static inline uint32_t inl_xoshiro256pp( void ) {
	const uint64_t result = rotl64( g256[0] + g256[3], 23 ) + g256[0], t = g256[1] << 17;
	g256[2] ^= g256[0]; g256[3] ^= g256[1]; g256[1] ^= g256[2]; g256[0] ^= g256[3]; g256[2] ^= t;
	g256[3] = rotl64( g256[3], 45 );
	return (uint32_t)( result >> 32 );
}
static inline uint32_t inl_holdrand( void ) {
	g32 = g32 * 214013u + 2531011u;
	return g32 >> 17;
}

static void inlineTimings( void ) {
	static const char *const name[3] = { "inline xoshiro128++", "inline xoshiro256++", "inline holdrand" };
	int m;
	for ( m = 0; m < 3; m++ ) {
		double best = 1e30;
		unsigned run, k;
		for ( run = 0; run < RUNS; run++ ) {
			uint64_t s = 5;
			uint32_t acc = 0;
			double t0, t;
			for ( k = 0; k < 4; k++ ) {
				g256[k] = splitmix64( &s );
				g128[k] = (uint32_t)( g256[k] >> 32 );
			}
			g32 = (uint32_t)s;
			t0 = now();
			if ( m == 0 ) {
				for ( k = 0; k < N; k++ ) acc += inl_xoshiro128pp();
			} else if ( m == 1 ) {
				for ( k = 0; k < N; k++ ) acc += inl_xoshiro256pp();
			} else {
				for ( k = 0; k < N; k++ ) acc += inl_holdrand();
			}
			t = now() - t0;
			sink = acc;
			if ( t < best ) best = t;
		}
		printf( "%-24s %-16s %8.2f\n", name[m], "", best * 1e9 / N );
	}
}

int main( void ) {
	unsigned g, h, k, run;
#if defined( _MSC_VER ) && !defined( __clang__ )
	printf( "compiler MSVC %d, %d-bit\n", _MSC_FULL_VER, (int)sizeof( void * ) * 8 );
#elif defined( __clang__ )
	printf( "compiler clang %s, %d-bit\n", __clang_version__, (int)sizeof( void * ) * 8 );
#else
	printf( "compiler GCC %s, %d-bit\n", __VERSION__, (int)sizeof( void * ) * 8 );
#endif
	printf( "%-24s %-16s %8s\n", "stream generator", "hash", "ns/value" );
	for ( g = 0; g < sizeof gens / sizeof gens[0]; g++ ) {
		state_t st;
		uint64_t hash = 14695981039346656037ull;
		double best = 1e30;
		seed( &st, 1 );
		srand( 1 );
		for ( k = 0; k < HASH_N; k++ ) hash = fnv( hash, gens[g].f( &st ) );
		for ( run = 0; run < RUNS; run++ ) {
			uint32_t acc = 0;
			double t0, t;
			seed( &st, 2 );
			t0 = now();
			for ( k = 0; k < N; k++ ) acc += gens[g].f( &st );
			t = now() - t0;
			sink = acc;
			if ( t < best ) best = t;
		}
		printf( "%-24s %016llx %8.2f\n", gens[g].name, (unsigned long long)hash, best * 1e9 / N );
	}
	printf( "%-24s %-16s %8s\n", "counter hash", "hash", "ns/value" );
	for ( h = 0; h < sizeof hashes / sizeof hashes[0]; h++ ) {
		uint64_t hash = 14695981039346656037ull;
		double best = 1e30;
		for ( k = 0; k < HASH_N; k++ ) hash = fnv( hash, hashes[h].f( k / 8, k % 8 ) );
		for ( run = 0; run < RUNS; run++ ) {
			uint32_t acc = 0;
			double t0, t;
			t0 = now();
			for ( k = 0; k < N; k++ ) acc += hashes[h].f( k >> 3, k & 7 );
			t = now() - t0;
			sink = acc;
			if ( t < best ) best = t;
		}
		printf( "%-24s %016llx %8.2f\n", hashes[h].name, (unsigned long long)hash, best * 1e9 / N );
	}
	/* a draw in [0, n) for n = 1 + k % 1000 from xoshiro128**: modulo (biased), multiply-shift (biased by at most
	   n / 2^32), Lemire's (unbiased: rejects the rare values of the biased low part) */
	{
		static const char *const method[3] = { "range: modulo", "range: multiply-shift", "range: Lemire unbiased" };
		int m;
		for ( m = 0; m < 3; m++ ) {
			double best = 1e30;
			for ( run = 0; run < RUNS; run++ ) {
				state_t st;
				uint32_t acc = 0;
				double t0, t;
				seed( &st, 3 );
				t0 = now();
				for ( k = 0; k < N; k++ ) {
					const uint32_t n = 1 + k % 1000, x = gen_xoshiro128ss( &st );
					if ( m == 0 ) {
						acc += x % n;
					} else if ( m == 1 ) {
						acc += (uint32_t)( ( (uint64_t)x * n ) >> 32 );
					} else {
						uint64_t p = (uint64_t)x * n;
						uint32_t low = (uint32_t)p;
						if ( low < n ) {
							const uint32_t threshold = (uint32_t)( -n ) % n;
							while ( low < threshold ) {
								p = (uint64_t)gen_xoshiro128ss( &st ) * n;
								low = (uint32_t)p;
							}
						}
						acc += (uint32_t)( p >> 32 );
					}
				}
				t = now() - t0;
				sink = acc;
				if ( t < best ) best = t;
			}
			printf( "%-24s %-16s %8.2f\n", method[m], "", best * 1e9 / N );
		}
	}
	inlineTimings();
	return 0;
}
