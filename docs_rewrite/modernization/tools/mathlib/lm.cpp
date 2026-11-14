// LLVM libc's header-only math functions (libc/shared/math) under names of our own, for mathbench.c.
// Built as C++17 with the include path <mathlibs>/llvm-project/libc.
#include "shared/math/acos.h"
#include "shared/math/acosf.h"
#include "shared/math/asinf.h"
#include "shared/math/atan.h"
#include "shared/math/atan2.h"
#include "shared/math/atan2f.h"
#include "shared/math/atanf.h"
#include "shared/math/cos.h"
#include "shared/math/cosf.h"
#include "shared/math/cospif.h"
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

extern "C" {
float lm_sinf( float x ) noexcept { return LIBC_NAMESPACE::shared::sinf( x ); }
float lm_cosf( float x ) noexcept { return LIBC_NAMESPACE::shared::cosf( x ); }
float lm_tanf( float x ) noexcept { return LIBC_NAMESPACE::shared::tanf( x ); }
float lm_asinf( float x ) noexcept { return LIBC_NAMESPACE::shared::asinf( x ); }
float lm_acosf( float x ) noexcept { return LIBC_NAMESPACE::shared::acosf( x ); }
float lm_atanf( float x ) noexcept { return LIBC_NAMESPACE::shared::atanf( x ); }
float lm_atan2f( float y, float x ) noexcept { return LIBC_NAMESPACE::shared::atan2f( y, x ); }
float lm_logf( float x ) noexcept { return LIBC_NAMESPACE::shared::logf( x ); }
float lm_powf( float x, float y ) noexcept { return LIBC_NAMESPACE::shared::powf( x, y ); }
float lm_sinpif( float x ) noexcept { return LIBC_NAMESPACE::shared::sinpif( x ); }
float lm_cospif( float x ) noexcept { return LIBC_NAMESPACE::shared::cospif( x ); }
float lm_tanpif( float x ) noexcept { return LIBC_NAMESPACE::shared::tanpif( x ); }
float lm_rsqrtf( float x ) noexcept { return LIBC_NAMESPACE::shared::rsqrtf( x ); }
double lm_sin( double x ) noexcept { return LIBC_NAMESPACE::shared::sin( x ); }
double lm_cos( double x ) noexcept { return LIBC_NAMESPACE::shared::cos( x ); }
double lm_tan( double x ) noexcept { return LIBC_NAMESPACE::shared::tan( x ); }
double lm_acos( double x ) noexcept { return LIBC_NAMESPACE::shared::acos( x ); }
double lm_atan( double x ) noexcept { return LIBC_NAMESPACE::shared::atan( x ); }
double lm_atan2( double y, double x ) noexcept { return LIBC_NAMESPACE::shared::atan2( y, x ); }
double lm_pow( double x, double y ) noexcept { return LIBC_NAMESPACE::shared::pow( x, y ); }
}
