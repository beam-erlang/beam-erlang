/* Compile-only consumers: no config.h, VM types or compiler-injected sizes. */
#ifdef HAVE_CONFIG_H
#  error Public-header checks must not consume the internal configuration
#endif
#if defined(SIZEOF_CHAR) || defined(SIZEOF_SHORT) || defined(SIZEOF_INT) \
    || defined(SIZEOF_LONG) || defined(SIZEOF_LONG_LONG) || defined(SIZEOF_VOID_P)
#  error Public-header checks must not inherit size definitions
#endif

/* Each target starts with one public header, independently of the others. */
#if defined(BEAM_PUBLIC_HEADER_NIF)
#  include "erl_nif.h"
typedef ErlNifSInt64 BeamPublicSigned64;
typedef ErlNifUInt64 BeamPublicUnsigned64;
#elif defined(BEAM_PUBLIC_HEADER_DRIVER)
#  include "erl_driver.h"
typedef ErlDrvSInt64 BeamPublicSigned64;
typedef ErlDrvUInt64 BeamPublicUnsigned64;
#elif defined(BEAM_PUBLIC_HEADER_FIXED)
#  include "erl_fixed_size_int_types.h"
typedef sgnd_int_64 BeamPublicSigned64;
typedef usgnd_int_64 BeamPublicUnsigned64;
#else
#  error Select a public header to check
#endif

#include <limits.h>

#ifdef __cplusplus
#  define BEAM_PUBLIC_ASSERT(Condition) static_assert(Condition, #Condition)
#else
#  define BEAM_PUBLIC_ASSERT(Condition) _Static_assert(Condition, #Condition)
#endif

BEAM_PUBLIC_ASSERT(sizeof(BeamPublicSigned64) * CHAR_BIT == 64);
BEAM_PUBLIC_ASSERT(sizeof(BeamPublicUnsigned64) * CHAR_BIT == 64);
BEAM_PUBLIC_ASSERT((BeamPublicSigned64)-1 < 0);
BEAM_PUBLIC_ASSERT((BeamPublicUnsigned64)-1 > 0);

/* The public headers clear sizes before reincluding the sizes header.
 * Repeating that sequence must restore all six after an earlier inclusion.
 */
#undef SIZEOF_CHAR
#undef SIZEOF_SHORT
#undef SIZEOF_INT
#undef SIZEOF_LONG
#undef SIZEOF_LONG_LONG
#undef SIZEOF_VOID_P
#include "erl_int_sizes_config.h"

BEAM_PUBLIC_ASSERT(SIZEOF_CHAR == sizeof(char));
BEAM_PUBLIC_ASSERT(SIZEOF_SHORT == sizeof(short));
BEAM_PUBLIC_ASSERT(SIZEOF_INT == sizeof(int));
BEAM_PUBLIC_ASSERT(SIZEOF_LONG == sizeof(long));
BEAM_PUBLIC_ASSERT(SIZEOF_LONG_LONG == sizeof(long long));
BEAM_PUBLIC_ASSERT(SIZEOF_VOID_P == sizeof(void *));
