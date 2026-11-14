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

// q_rand.h -- the only entry point to random numbers: every module and the engine draw through these, never through
// the C library's rand(), whose sequence differs between glibc and the UCRT. One generator, xoshiro128++ (Blackman
// and Vigna, public domain), seeded through SplitMix64; integers in a range by Lemire's unbiased method, floats from
// the top 24 bits: the same numbers on every platform and compiler. Changing the generator only changes q_rand.c.
// Survey and measurements: docs_rewrite/modernization/surveys/random-numbers.md.
#pragma once

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

// a generator's state, one per independent stream; all zero is not a valid state: seed it with Q_RandSeed
typedef struct qrand_s {
	uint32_t s[4];
} qrand_t;

void Q_RandSeed( qrand_t *rng, uint64_t seed );
uint32_t Q_RandU32( qrand_t *rng );
int Q_RandInt( qrand_t *rng, int min, int max ); // min <= x <= max, unbiased; the bounds in either order
float Q_RandFloat( qrand_t *rng, float min, float max ); // min <= x < max, as far as float rounding allows

// the module's own stream: the game, cgame, the UI and the engine each have one
void Rand_Init( int seed );
int Q_irand( int min, int max );
float Q_flrand( float min, float max );
float erandom( float mean ); // exponential distribution of this mean

// a sequence the caller keeps in an int, to draw the same numbers again from the same seed: [0, 1) and [-1, 1)
float Q_random( int *seed );
float Q_crandom( int *seed );

#if defined(__cplusplus)
} // extern "C"
#endif
