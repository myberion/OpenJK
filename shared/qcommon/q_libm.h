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

// q_libm.h -- the only entry point to the math functions: every module and the engine call these Q_ functions, never
// the C library's. Three kinds:
// - functions whose result IEEE 754 does not fix (sine, cosine, tangent, their inverses, logarithms, power, inverse
//   square root, the sine, cosine and tangent of pi times x): the C library's differ between glibc versions, Windows
//   versions, 32 and 64-bit processes and CPUs, so q_libm.cpp implements them with correctly rounded code (LLVM libc),
//   the same bits on every platform and compiler;
// - functions whose result IEEE 754 fixes (square root, floor, ceiling, absolute value, remainder, rounding, tests):
//   inline calls to the C library, which every compiler turns into the same instruction or the same exact result;
// - text to numbers (strtof, strtod, atof, the sscanf %f of the game's files): correctly rounded (LLVM libc), since
//   the UCRT reads some decimals differently from glibc.
// Changing an implementation only changes this header or q_libm.cpp.
// Survey and measurements: docs_rewrite/netcode/12-deterministic-math-survey.md.
#pragma once

#include <math.h>

#include "q_platform.h"

#if defined(__cplusplus)
#define Q_LIBM_NOEXCEPT noexcept
extern "C" {
#else
#define Q_LIBM_NOEXCEPT
#endif

float Q_sinf( float x ) Q_LIBM_NOEXCEPT;
float Q_cosf( float x ) Q_LIBM_NOEXCEPT;
float Q_tanf( float x ) Q_LIBM_NOEXCEPT;
float Q_asinf( float x ) Q_LIBM_NOEXCEPT;
float Q_acosf( float x ) Q_LIBM_NOEXCEPT;
float Q_atanf( float x ) Q_LIBM_NOEXCEPT;
float Q_atan2f( float y, float x ) Q_LIBM_NOEXCEPT;
float Q_logf( float x ) Q_LIBM_NOEXCEPT;
float Q_log2f( float x ) Q_LIBM_NOEXCEPT;
float Q_powf( float x, float y ) Q_LIBM_NOEXCEPT;
float Q_rsqrtf( float x ) Q_LIBM_NOEXCEPT; // 1 / sqrt( x )
// sin( pi * x ), cos( pi * x ), tan( pi * x ): an angle of d degrees is Q_sinpif( d / 180 ), exact at multiples of
// 90 degrees and with one rounding fewer than through radians
float Q_sinpif( float x ) Q_LIBM_NOEXCEPT;
float Q_cospif( float x ) Q_LIBM_NOEXCEPT;
float Q_tanpif( float x ) Q_LIBM_NOEXCEPT;
// atan2( y, x ), atan( x ) and acos( x ) in degrees: computed in double, rounded to float once
float Q_atan2degf( float y, float x ) Q_LIBM_NOEXCEPT;
float Q_atandegf( float x ) Q_LIBM_NOEXCEPT;
float Q_acosdegf( float x ) Q_LIBM_NOEXCEPT;

double Q_sin( double x ) Q_LIBM_NOEXCEPT;
double Q_cos( double x ) Q_LIBM_NOEXCEPT;
double Q_tan( double x ) Q_LIBM_NOEXCEPT;
double Q_asin( double x ) Q_LIBM_NOEXCEPT;
double Q_acos( double x ) Q_LIBM_NOEXCEPT;
double Q_atan( double x ) Q_LIBM_NOEXCEPT;
double Q_atan2( double y, double x ) Q_LIBM_NOEXCEPT;
double Q_log( double x ) Q_LIBM_NOEXCEPT;
double Q_log2( double x ) Q_LIBM_NOEXCEPT;
double Q_pow( double x, double y ) Q_LIBM_NOEXCEPT;

// Text to numbers, correctly rounded and the same on every platform (the UCRT's strtof and sscanf %f round a decimal
// exactly halfway between two floats away from even). As strtof and strtod: white space skipped first, *end set
// after the number (to str when there is none) unless end is NULL, errno set to ERANGE on overflow and underflow; the
// C locale's decimal point. Q_atof is Q_strtod( str, NULL ).
float Q_strtof( const char *str, const char **end ) Q_LIBM_NOEXCEPT;
double Q_strtod( const char *str, const char **end ) Q_LIBM_NOEXCEPT;
double Q_atof( const char *str ) Q_LIBM_NOEXCEPT;
// count floats separated by white space into out, as sscanf( str, "%f %f ...", ... ) does: returns how many were
// read, or -1 when str holds nothing but white space
int Q_ScanFloats( const char *str, float *out, int count ) Q_LIBM_NOEXCEPT;

static QINLINE float Q_sqrtf( float x ) { return sqrtf( x ); }
static QINLINE float Q_floorf( float x ) { return floorf( x ); }
static QINLINE float Q_ceilf( float x ) { return ceilf( x ); }
static QINLINE float Q_fabsf( float x ) { return fabsf( x ); }
static QINLINE float Q_fmodf( float x, float y ) { return fmodf( x, y ); }
static QINLINE float Q_roundf( float x ) { return roundf( x ); }
static QINLINE long Q_lrintf( float x ) { return lrintf( x ); } // in the current rounding mode

static QINLINE double Q_sqrt( double x ) { return sqrt( x ); }
static QINLINE double Q_floor( double x ) { return floor( x ); }
static QINLINE double Q_ceil( double x ) { return ceil( x ); }
static QINLINE double Q_fabs( double x ) { return fabs( x ); }
static QINLINE double Q_fmod( double x, double y ) { return fmod( x, y ); }
static QINLINE double Q_round( double x ) { return round( x ); }

// a float argument converts exactly
static QINLINE int Q_isnan( double x ) { return isnan( x ) != 0; }
static QINLINE int Q_isfinite( double x ) { return isfinite( x ) != 0; }

#if defined(__cplusplus)
} // extern "C"
#endif
