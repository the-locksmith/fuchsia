// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocol.primative banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations









typedef struct synchronous_primative_protocol synchronous_primative_protocol_t;
typedef struct async_primative_protocol async_primative_protocol_t;

typedef void (*async_primative_bool_callback)(void* ctx, bool b, bool b_2);
typedef void (*async_primative_int8_callback)(void* ctx, int8_t i8, int8_t i8_2);
typedef void (*async_primative_int16_callback)(void* ctx, int16_t i16, int16_t i16_2);
typedef void (*async_primative_int32_callback)(void* ctx, int32_t i32, int32_t i32_2);
typedef void (*async_primative_int64_callback)(void* ctx, int64_t i64, int64_t i64_2);
typedef void (*async_primative_uint8_callback)(void* ctx, uint8_t u8, uint8_t u8_2);
typedef void (*async_primative_uint16_callback)(void* ctx, uint16_t u16, uint16_t u16_2);
typedef void (*async_primative_uint32_callback)(void* ctx, uint32_t u32, uint32_t u32_2);
typedef void (*async_primative_uint64_callback)(void* ctx, uint64_t u64, uint64_t u64_2);
typedef void (*async_primative_float32_callback)(void* ctx, float f32, float f32_2);
typedef void (*async_primative_float64_callback)(void* ctx, double f64, double f64_2);

// Declarations









typedef struct synchronous_primative_protocol_ops {
    bool (*bool)(void* ctx, bool b, bool* out_b_2);
    int8_t (*int8)(void* ctx, int8_t i8, int8_t* out_i8_2);
    int16_t (*int16)(void* ctx, int16_t i16, int16_t* out_i16_2);
    int32_t (*int32)(void* ctx, int32_t i32, int32_t* out_i32_2);
    int64_t (*int64)(void* ctx, int64_t i64, int64_t* out_i64_2);
    uint8_t (*uint8)(void* ctx, uint8_t u8, uint8_t* out_u8_2);
    uint16_t (*uint16)(void* ctx, uint16_t u16, uint16_t* out_u16_2);
    uint32_t (*uint32)(void* ctx, uint32_t u32, uint32_t* out_u32_2);
    uint64_t (*uint64)(void* ctx, uint64_t u64, uint64_t* out_u64_2);
    float (*float32)(void* ctx, float f32, float* out_f32_2);
    double (*float64)(void* ctx, double u64, double* out_f64_2);
} synchronous_primative_protocol_ops_t;


struct synchronous_primative_protocol {
    synchronous_primative_protocol_ops_t* ops;
    void* ctx;
};

static inline bool synchronous_primative_bool(const synchronous_primative_protocol_t* proto, bool b, bool* out_b_2) {
    return proto->ops->bool(proto->ctx, b, out_b_2);
}

static inline int8_t synchronous_primative_int8(const synchronous_primative_protocol_t* proto, int8_t i8, int8_t* out_i8_2) {
    return proto->ops->int8(proto->ctx, i8, out_i8_2);
}

static inline int16_t synchronous_primative_int16(const synchronous_primative_protocol_t* proto, int16_t i16, int16_t* out_i16_2) {
    return proto->ops->int16(proto->ctx, i16, out_i16_2);
}

static inline int32_t synchronous_primative_int32(const synchronous_primative_protocol_t* proto, int32_t i32, int32_t* out_i32_2) {
    return proto->ops->int32(proto->ctx, i32, out_i32_2);
}

static inline int64_t synchronous_primative_int64(const synchronous_primative_protocol_t* proto, int64_t i64, int64_t* out_i64_2) {
    return proto->ops->int64(proto->ctx, i64, out_i64_2);
}

static inline uint8_t synchronous_primative_uint8(const synchronous_primative_protocol_t* proto, uint8_t u8, uint8_t* out_u8_2) {
    return proto->ops->uint8(proto->ctx, u8, out_u8_2);
}

static inline uint16_t synchronous_primative_uint16(const synchronous_primative_protocol_t* proto, uint16_t u16, uint16_t* out_u16_2) {
    return proto->ops->uint16(proto->ctx, u16, out_u16_2);
}

static inline uint32_t synchronous_primative_uint32(const synchronous_primative_protocol_t* proto, uint32_t u32, uint32_t* out_u32_2) {
    return proto->ops->uint32(proto->ctx, u32, out_u32_2);
}

static inline uint64_t synchronous_primative_uint64(const synchronous_primative_protocol_t* proto, uint64_t u64, uint64_t* out_u64_2) {
    return proto->ops->uint64(proto->ctx, u64, out_u64_2);
}

static inline float synchronous_primative_float32(const synchronous_primative_protocol_t* proto, float f32, float* out_f32_2) {
    return proto->ops->float32(proto->ctx, f32, out_f32_2);
}

static inline double synchronous_primative_float64(const synchronous_primative_protocol_t* proto, double u64, double* out_f64_2) {
    return proto->ops->float64(proto->ctx, u64, out_f64_2);
}


typedef struct async_primative_protocol_ops {
    void (*bool)(void* ctx, bool b, async_primative_bool_callback callback, void* cookie);
    void (*int8)(void* ctx, int8_t i8, async_primative_int8_callback callback, void* cookie);
    void (*int16)(void* ctx, int16_t i16, async_primative_int16_callback callback, void* cookie);
    void (*int32)(void* ctx, int32_t i32, async_primative_int32_callback callback, void* cookie);
    void (*int64)(void* ctx, int64_t i64, async_primative_int64_callback callback, void* cookie);
    void (*uint8)(void* ctx, uint8_t u8, async_primative_uint8_callback callback, void* cookie);
    void (*uint16)(void* ctx, uint16_t u16, async_primative_uint16_callback callback, void* cookie);
    void (*uint32)(void* ctx, uint32_t u32, async_primative_uint32_callback callback, void* cookie);
    void (*uint64)(void* ctx, uint64_t u64, async_primative_uint64_callback callback, void* cookie);
    void (*float32)(void* ctx, float f32, async_primative_float32_callback callback, void* cookie);
    void (*float64)(void* ctx, double u64, async_primative_float64_callback callback, void* cookie);
} async_primative_protocol_ops_t;


struct async_primative_protocol {
    async_primative_protocol_ops_t* ops;
    void* ctx;
};

static inline void async_primative_bool(const async_primative_protocol_t* proto, bool b, async_primative_bool_callback callback, void* cookie) {
    proto->ops->bool(proto->ctx, b, callback, cookie);
}

static inline void async_primative_int8(const async_primative_protocol_t* proto, int8_t i8, async_primative_int8_callback callback, void* cookie) {
    proto->ops->int8(proto->ctx, i8, callback, cookie);
}

static inline void async_primative_int16(const async_primative_protocol_t* proto, int16_t i16, async_primative_int16_callback callback, void* cookie) {
    proto->ops->int16(proto->ctx, i16, callback, cookie);
}

static inline void async_primative_int32(const async_primative_protocol_t* proto, int32_t i32, async_primative_int32_callback callback, void* cookie) {
    proto->ops->int32(proto->ctx, i32, callback, cookie);
}

static inline void async_primative_int64(const async_primative_protocol_t* proto, int64_t i64, async_primative_int64_callback callback, void* cookie) {
    proto->ops->int64(proto->ctx, i64, callback, cookie);
}

static inline void async_primative_uint8(const async_primative_protocol_t* proto, uint8_t u8, async_primative_uint8_callback callback, void* cookie) {
    proto->ops->uint8(proto->ctx, u8, callback, cookie);
}

static inline void async_primative_uint16(const async_primative_protocol_t* proto, uint16_t u16, async_primative_uint16_callback callback, void* cookie) {
    proto->ops->uint16(proto->ctx, u16, callback, cookie);
}

static inline void async_primative_uint32(const async_primative_protocol_t* proto, uint32_t u32, async_primative_uint32_callback callback, void* cookie) {
    proto->ops->uint32(proto->ctx, u32, callback, cookie);
}

static inline void async_primative_uint64(const async_primative_protocol_t* proto, uint64_t u64, async_primative_uint64_callback callback, void* cookie) {
    proto->ops->uint64(proto->ctx, u64, callback, cookie);
}

static inline void async_primative_float32(const async_primative_protocol_t* proto, float f32, async_primative_float32_callback callback, void* cookie) {
    proto->ops->float32(proto->ctx, f32, callback, cookie);
}

static inline void async_primative_float64(const async_primative_protocol_t* proto, double u64, async_primative_float64_callback callback, void* cookie) {
    proto->ops->float64(proto->ctx, u64, callback, cookie);
}


__END_CDECLS;
