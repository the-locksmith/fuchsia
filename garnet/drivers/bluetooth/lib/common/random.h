// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_RANDOM_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_RANDOM_H_

#include <zircon/syscalls.h>

namespace btlib {
namespace common {

template <typename T>
T Random() {
  static_assert(std::is_trivial_v<T> && !std::is_pointer_v<T>,
                "Type cannot be filled with random bytes");
  T t;
  zx_cprng_draw(&t, sizeof(T));
  return t;
}

}  // namespace common
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_RANDOM_H_
