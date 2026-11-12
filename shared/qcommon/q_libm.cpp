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

// q_libm.cpp -- the out-of-line functions of q_libm.h on LLVM libc's header-only math (lib/llvm-libc, C++17): correctly rounded
// for every float function and every double one except atan and atan2 (within 1 ulp, the same bits everywhere),
// identical on every compiler and platform measured (docs_rewrite/netcode/12-deterministic-math-survey.md, 12.10).
#include "q_libm.h"

#include <cctype>
#include <cerrno>

#include "shared/math/acos.h"
#include "shared/math/acosf.h"
#include "shared/math/asin.h"
#include "shared/math/asinf.h"
#include "shared/math/atan.h"
#include "shared/math/atan2.h"
#include "shared/math/atan2f.h"
#include "shared/math/atanf.h"
#include "shared/math/cos.h"
#include "shared/math/cosf.h"
#include "shared/math/cospif.h"
#include "shared/math/log.h"
#include "shared/math/log2.h"
#include "shared/math/log2f.h"
#include "shared/math/logf.h"
#include "shared/math/pow.h"
#include "shared/math/powf.h"
#include "shared/math/rsqrtf.h"
#include "shared/math/sin.h"
#include "shared/math/sinf.h"
#include "shared/math/sinpif.h"
#include "shared/math/tan.h"
#include "shared/math/tanf.h"
#include "shared/math/tanpif.h"
#include "src/__support/str_to_float.h"

float Q_sinf( float x ) noexcept { return LIBC_NAMESPACE::shared::sinf( x ); }
float Q_cosf( float x ) noexcept { return LIBC_NAMESPACE::shared::cosf( x ); }
float Q_tanf( float x ) noexcept { return LIBC_NAMESPACE::shared::tanf( x ); }
float Q_asinf( float x ) noexcept { return LIBC_NAMESPACE::shared::asinf( x ); }
float Q_acosf( float x ) noexcept { return LIBC_NAMESPACE::shared::acosf( x ); }
float Q_atanf( float x ) noexcept { return LIBC_NAMESPACE::shared::atanf( x ); }
float Q_atan2f( float y, float x ) noexcept { return LIBC_NAMESPACE::shared::atan2f( y, x ); }
float Q_logf( float x ) noexcept { return LIBC_NAMESPACE::shared::logf( x ); }
float Q_log2f( float x ) noexcept { return LIBC_NAMESPACE::shared::log2f( x ); }
float Q_powf( float x, float y ) noexcept { return LIBC_NAMESPACE::shared::powf( x, y ); }
float Q_rsqrtf( float x ) noexcept { return LIBC_NAMESPACE::shared::rsqrtf( x ); }
float Q_sinpif( float x ) noexcept { return LIBC_NAMESPACE::shared::sinpif( x ); }
float Q_cospif( float x ) noexcept { return LIBC_NAMESPACE::shared::cospif( x ); }
float Q_tanpif( float x ) noexcept { return LIBC_NAMESPACE::shared::tanpif( x ); }

namespace {
constexpr double kDegreesPerRadian = 57.295779513082320876798154814105; // 180 / pi
}

float Q_atan2degf( float y, float x ) noexcept
{
	return static_cast<float>( LIBC_NAMESPACE::shared::atan2( y, x ) * kDegreesPerRadian );
}
float Q_atandegf( float x ) noexcept
{
	return static_cast<float>( LIBC_NAMESPACE::shared::atan( x ) * kDegreesPerRadian );
}
float Q_acosdegf( float x ) noexcept
{
	return static_cast<float>( LIBC_NAMESPACE::shared::acos( x ) * kDegreesPerRadian );
}

double Q_sin( double x ) noexcept { return LIBC_NAMESPACE::shared::sin( x ); }
double Q_cos( double x ) noexcept { return LIBC_NAMESPACE::shared::cos( x ); }
double Q_tan( double x ) noexcept { return LIBC_NAMESPACE::shared::tan( x ); }
double Q_asin( double x ) noexcept { return LIBC_NAMESPACE::shared::asin( x ); }
double Q_acos( double x ) noexcept { return LIBC_NAMESPACE::shared::acos( x ); }
double Q_atan( double x ) noexcept { return LIBC_NAMESPACE::shared::atan( x ); }
double Q_atan2( double y, double x ) noexcept { return LIBC_NAMESPACE::shared::atan2( y, x ); }
double Q_log( double x ) noexcept { return LIBC_NAMESPACE::shared::log( x ); }
double Q_log2( double x ) noexcept { return LIBC_NAMESPACE::shared::log2( x ); }
double Q_pow( double x, double y ) noexcept { return LIBC_NAMESPACE::shared::pow( x, y ); }

namespace {
// LLVM libc's parser as the C library's strtof and strtod
template <typename T>
T StrToFloat( const char *str, const char **end ) noexcept
{
	const auto result = LIBC_NAMESPACE::internal::strtofloatingpoint<T>( str );
	if ( result.error != 0 ) {
		errno = result.error;
	}
	if ( end != nullptr ) {
		*end = str + result.parsed_len;
	}
	return result.value;
}

bool OnlySpace( const char *p ) noexcept
{
	while ( *p != '\0' && std::isspace( static_cast<unsigned char>( *p ) ) != 0 ) {
		++p;
	}
	return *p == '\0';
}
} // namespace

float Q_strtof( const char *str, const char **end ) noexcept { return StrToFloat<float>( str, end ); }
double Q_strtod( const char *str, const char **end ) noexcept { return StrToFloat<double>( str, end ); }
double Q_atof( const char *str ) noexcept { return StrToFloat<double>( str, nullptr ); }

int Q_ScanFloats( const char *str, float *out, int count ) noexcept
{
	const char *p = str;
	for ( int i = 0; i < count; ++i ) {
		const char *end = nullptr;
		const float value = Q_strtof( p, &end );
		if ( end == p ) {
			// as sscanf: the end of the input before the first number gives -1
			return ( i == 0 && OnlySpace( p ) ) ? -1 : i;
		}
		out[i] = value;
		p = end;
	}
	return count;
}
