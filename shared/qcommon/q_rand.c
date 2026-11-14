/*
===========================================================================
Copyright (C) 2026, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// q_rand.c -- the functions of q_rand.h: xoshiro128++ (D. Blackman and S. Vigna, 2019, public domain,
// https://prng.di.unimi.it/), SplitMix64 for seeding (G. Steele, D. Lea, C. Flood, 2014), D. Lemire's unbiased
// integers in a range (ACM TOMACS, 2019).
#include "q_rand.h"

#include "q_libm.h"

static uint32_t Rotl( uint32_t x, int k ) {
	return ( x << k ) | ( x >> ( 32 - k ) );
}

void Q_RandSeed( qrand_t *rng, uint64_t seed ) {
	int i;

	for ( i = 0; i < 4; i += 2 ) {
		uint64_t z = ( seed += 0x9e3779b97f4a7c15ull );
		z = ( z ^ ( z >> 30 ) ) * 0xbf58476d1ce4e5b9ull;
		z = ( z ^ ( z >> 27 ) ) * 0x94d049bb133111ebull;
		z ^= z >> 31;
		rng->s[i] = (uint32_t)z;
		rng->s[i + 1] = (uint32_t)( z >> 32 );
	}
	if ( ( rng->s[0] | rng->s[1] | rng->s[2] | rng->s[3] ) == 0 ) {
		rng->s[0] = 1; // the one state xoshiro cannot leave
	}
}

uint32_t Q_RandU32( qrand_t *rng ) {
	uint32_t *s = rng->s;
	const uint32_t result = Rotl( s[0] + s[3], 7 ) + s[0];
	const uint32_t t = s[1] << 9;

	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];
	s[2] ^= t;
	s[3] = Rotl( s[3], 11 );
	return result;
}

int Q_RandInt( qrand_t *rng, int min, int max ) {
	uint32_t n;
	uint64_t m;

	if ( max < min ) {
		const int swap = min;
		min = max;
		max = swap;
	}
	n = (uint32_t)max - (uint32_t)min + 1u; // the count of values; 0 for the whole 32-bit range
	if ( n == 0 ) {
		return (int)Q_RandU32( rng );
	}
	// Lemire: the high word of value * n, rejecting the few values that would make some results more likely
	m = (uint64_t)Q_RandU32( rng ) * n;
	if ( (uint32_t)m < n ) {
		const uint32_t threshold = ( 0u - n ) % n;
		while ( (uint32_t)m < threshold ) {
			m = (uint64_t)Q_RandU32( rng ) * n;
		}
	}
	return (int)( (uint32_t)min + (uint32_t)( m >> 32 ) );
}

float Q_RandFloat( qrand_t *rng, float min, float max ) {
	const float unit = (float)( Q_RandU32( rng ) >> 8 ) * ( 1.0f / 16777216.0f ); // [0, 1) on a 2^-24 grid

	return min + ( max - min ) * unit;
}

// the module's stream; this state is SplitMix64's from 0x89abcdef, the seed holdrand had before Rand_Init
static qrand_t s_rand = { { 0xe20699a6u, 0xcb6f5641u, 0x12146e10u, 0x67debd8eu } };

void Rand_Init( int seed ) {
	Q_RandSeed( &s_rand, (uint32_t)seed );
}

int Q_irand( int min, int max ) {
	return Q_RandInt( &s_rand, min, max );
}

float Q_flrand( float min, float max ) {
	return Q_RandFloat( &s_rand, min, max );
}

float erandom( float mean ) {
	float r;

	do {
		r = Q_flrand( 0.0f, 1.0f );
	} while ( r == 0.0f );

	return -mean * Q_logf( r );
}

// a Weyl sequence through Wellons' lowbias32 hash, so that the caller's int is the whole state
float Q_random( int *seed ) {
	uint32_t x = (uint32_t)*seed + 0x9e3779b9u;

	*seed = (int)x;
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return (float)( x >> 8 ) * ( 1.0f / 16777216.0f );
}

float Q_crandom( int *seed ) {
	return 2.0f * Q_random( seed ) - 1.0f;
}
