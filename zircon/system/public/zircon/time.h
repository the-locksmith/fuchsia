// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// absolute time in nanoseconds (generally with respect to the monotonic clock)
typedef int64_t zx_time_t;
// a duration in nanoseconds
typedef int64_t zx_duration_t;
// a duration in hardware ticks
typedef uint64_t zx_ticks_t;

#define ZX_TIME_INFINITE INT64_MAX
#define ZX_TIME_INFINITE_PAST INT64_MIN

// These functions perform overflow-safe time arithmetic and unit conversion, clamping to
// ZX_TIME_INFINITE in case of overflow and ZX_TIME_INFINITE_PAST in case of underflow.
//
// C++ code should use zx::time and zx::duration instead.
//
// For arithmetic the naming scheme is:
//     zx_<first argument>_<operation>_<second argument>
//
// For unit conversion the naming scheme is:
//     zx_duration_from_<unit of argument>
//
// TODO(maniscalco): Consider expanding the set of operations to include division, modulo, and
// floating point math.

__CONSTEXPR static inline zx_time_t zx_time_add_duration(zx_time_t time, zx_duration_t duration) {
    zx_time_t x = 0;
    if (unlikely(add_overflow(time, duration, &x))) {
        if (x >= 0) {
            return ZX_TIME_INFINITE_PAST;
        } else {
            return ZX_TIME_INFINITE;
        }
    }
    return x;
}

__CONSTEXPR static inline zx_time_t zx_time_sub_duration(zx_time_t time, zx_duration_t duration) {
    zx_time_t x = 0;
    if (unlikely(sub_overflow(time, duration, &x))) {
        if (x >= 0) {
            return ZX_TIME_INFINITE_PAST;
        } else {
            return ZX_TIME_INFINITE;
        }

    }
    return x;
}

__CONSTEXPR static inline zx_duration_t zx_time_sub_time(zx_time_t time1, zx_time_t time2) {
    zx_duration_t x = 0;
    if (unlikely(sub_overflow(time1, time2, &x))) {
        if (x >= 0) {
            return ZX_TIME_INFINITE_PAST;
        } else {
            return ZX_TIME_INFINITE;
        }
    }
    return x;
}

__CONSTEXPR static inline zx_duration_t zx_duration_add_duration(zx_duration_t dur1,
                                                                 zx_duration_t dur2) {
    zx_duration_t x = 0;
    if (unlikely(add_overflow(dur1, dur2, &x))) {
        if (x >= 0) {
            return ZX_TIME_INFINITE_PAST;
        } else {
            return ZX_TIME_INFINITE;
        }
    }
    return x;
}

__CONSTEXPR static inline zx_duration_t zx_duration_sub_duration(zx_duration_t dur1,
                                                                 zx_duration_t dur2) {
    zx_duration_t x = 0;
    if (unlikely(sub_overflow(dur1, dur2, &x))) {
        if (x >= 0) {
            return ZX_TIME_INFINITE_PAST;
        } else {
            return ZX_TIME_INFINITE;
        }
    }
    return x;
}

__CONSTEXPR static inline zx_duration_t zx_duration_mul_int64(zx_duration_t duration,
                                                              int64_t multiplier) {
    zx_duration_t x = 0;
    if (unlikely(mul_overflow(duration, multiplier, &x))) {
        if ((duration > 0 && multiplier > 0) || (duration < 0 && multiplier < 0)) {
            return ZX_TIME_INFINITE;
        } else {
            return ZX_TIME_INFINITE_PAST;
        }
    }
    return x;
}

__CONSTEXPR static inline zx_duration_t zx_duration_from_nsec(int64_t n) {
    return zx_duration_mul_int64(1, n);
}

__CONSTEXPR static inline zx_duration_t zx_duration_from_usec(int64_t n) {
    return zx_duration_mul_int64(1000, n);
}

__CONSTEXPR static inline zx_duration_t zx_duration_from_msec(int64_t n) {
    return zx_duration_mul_int64(1000000, n);
}

__CONSTEXPR static inline zx_duration_t zx_duration_from_sec(int64_t n) {
    return zx_duration_mul_int64(1000000000, n);
}

__CONSTEXPR static inline zx_duration_t zx_duration_from_min(int64_t n) {
    return zx_duration_mul_int64(60000000000, n);
}

__CONSTEXPR static inline zx_duration_t zx_duration_from_hour(int64_t n) {
    return zx_duration_mul_int64(3600000000000, n);
}

// Similar to the functions above, these macros perform overflow-safe unit conversion. Prefer to use
// the functions above instead of these macros.
#define ZX_NSEC(n) \
    (__ISCONSTANT(n) ? ((zx_duration_t)(1LL * (n))) : (zx_duration_from_nsec(n)))
#define ZX_USEC(n) \
    (__ISCONSTANT(n) ? ((zx_duration_t)(1000LL * (n))) : (zx_duration_from_usec(n)))
#define ZX_MSEC(n) \
    (__ISCONSTANT(n) ? ((zx_duration_t)(1000000LL * (n))) : (zx_duration_from_msec(n)))
#define ZX_SEC(n) \
    (__ISCONSTANT(n) ? ((zx_duration_t)(1000000000LL * (n))) : (zx_duration_from_sec(n)))
#define ZX_MIN(n) \
    (__ISCONSTANT(n) ? ((zx_duration_t)(60LL * 1000000000LL * (n))) : (zx_duration_from_min(n)))
#define ZX_HOUR(n) \
    (__ISCONSTANT(n) ? ((zx_duration_t)(3600LL * 1000000000LL * (n))) : (zx_duration_from_hour(n)))

__END_CDECLS
