// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_TRAITS_H_
#define LIB_FIDL_CPP_TRAITS_H_

#include <stdint.h>

#include <type_traits>

namespace fidl {

// A type trait that indiciates whether the given type is a primitive FIDL
// type.
template <typename T>
struct IsPrimitive : public std::false_type {};

// clang-format off
template <> struct IsPrimitive<bool> : public std::true_type {};
template <> struct IsPrimitive<uint8_t> : public std::true_type {};
template <> struct IsPrimitive<uint16_t> : public std::true_type {};
template <> struct IsPrimitive<uint32_t> : public std::true_type {};
template <> struct IsPrimitive<uint64_t> : public std::true_type {};
template <> struct IsPrimitive<int8_t> : public std::true_type {};
template <> struct IsPrimitive<int16_t> : public std::true_type {};
template <> struct IsPrimitive<int32_t> : public std::true_type {};
template <> struct IsPrimitive<int64_t> : public std::true_type {};
template <> struct IsPrimitive<float> : public std::true_type {};
template <> struct IsPrimitive<double> : public std::true_type {};
// clang-format on

template <typename T>
struct IsStdVector : public std::false_type {};

template <typename V, typename A>
struct IsStdVector<std::vector<V, A>> : public std::true_type{};

template <typename T>
struct IsStdString : public std::false_type {};

template <>
struct IsStdString<std::string> : public std::true_type{};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_TRAITS_H_
