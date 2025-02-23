// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/escher/test/gtest_vulkan.h"

#include "garnet/public/lib/escher/test/gtest_vulkan_internal.h"

#include <vulkan/vulkan.hpp>

namespace testing {
namespace internal {
namespace escher {

std::string PrependDisabledIfNecessary(const std::string& test_case) {
  if (::escher::VulkanIsSupported())
    return test_case;
  return "DISABLED_" + test_case;
}

}  // namespace escher
}  // namespace internal
}  // namespace testing
