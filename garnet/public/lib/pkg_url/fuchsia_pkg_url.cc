// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/pkg_url/fuchsia_pkg_url.h"
#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/substitute.h"

#include <regex>
#include <string>

namespace component {

static const std::string kFuchsiaPkgPrefix = "fuchsia-pkg://";

// kFuchsiaPkgRexp has the following group matches:
// 1: user/domain/port/etc (everything after scheme, before path)
// 2: package name
// 3: package variant
// 4: resource path
static const std::regex* const kFuchsiaPkgRexp =
    new std::regex("^fuchsia-pkg://([^/]+)/([^/#]+)(?:/([^/#]+))?(?:#(.+))?$");

// static
bool FuchsiaPkgUrl::IsFuchsiaPkgScheme(const std::string& url) {
  return url.compare(0, kFuchsiaPkgPrefix.length(), kFuchsiaPkgPrefix) == 0;
}

std::string FuchsiaPkgUrl::GetDefaultComponentCmxPath() const {
  return fxl::Substitute("meta/$0.cmx", package_name());
}

std::string FuchsiaPkgUrl::GetDefaultComponentName() const {
  return package_name();
}

bool FuchsiaPkgUrl::Parse(const std::string& url) {
  package_name_.clear();
  resource_path_.clear();

  std::smatch match_data;

  if (!std::regex_match(url, match_data, *kFuchsiaPkgRexp)) {
    return false;
  }

  url_ = match_data[0].str();

  host_name_ = match_data[1].str();
  package_name_ = match_data[2].str();
  variant_ = match_data[3].str();
  if (variant_.empty()) {
    // TODO(CF-105): Currently this defaults to "0" if not present, but variant
    // will eventually be required in fuchsia-pkg URLs.
    variant_ = "0";
  }
  resource_path_ = match_data[4].str();

  return true;
}

std::string FuchsiaPkgUrl::pkgfs_dir_path() const {
  return fxl::Substitute("/pkgfs/packages/$0/$1", package_name_, variant_);
}

std::string FuchsiaPkgUrl::package_path() const {
  return fxl::Substitute("fuchsia-pkg://$0/$1/$2", host_name_, package_name_,
                         variant_);
}

const std::string& FuchsiaPkgUrl::ToString() const { return url_; }

}  // namespace component
