// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cmx/sandbox.h"

#include <algorithm>
#include <map>

#include "lib/json/json_parser.h"
#include "lib/fxl/strings/substitute.h"
#include "rapidjson/document.h"

namespace component {
namespace {

template <typename Value>
void CopyArrayToVector(const std::string& name, const Value& value,
                       json::JSONParser* json_parser,
                       std::vector<std::string>* vec) {
  if (!value.IsArray()) {
    json_parser->ReportError(
        fxl::Substitute("'$0' in sandbox is not an array.", name));
    return;
  }
  for (const auto& entry : value.GetArray()) {
    if (!entry.IsString()) {
      json_parser->ReportError(
          fxl::Substitute("Entry for '$0' in sandbox is not a string.", name));
      return;
    }
    vec->push_back(entry.GetString());
  }
}

}  // namespace

constexpr char kDev[] = "dev";
constexpr char kSystem[] = "system";
constexpr char kServices[] = "services";
constexpr char kPkgfs[] = "pkgfs";
constexpr char kFeatures[] = "features";
constexpr char kBoot[] = "boot";

bool SandboxMetadata::Parse(const rapidjson::Value& sandbox_value,
                            json::JSONParser* json_parser) {
  const std::map<std::string, std::vector<std::string>*> name_to_vec = {
      {kDev, &dev_},     {kSystem, &system_},     {kServices, &services_},
      {kPkgfs, &pkgfs_}, {kFeatures, &features_}, {kBoot, &boot_}};

  for (const auto& entry : name_to_vec) {
    entry.second->clear();
  }
  null_ = true;

  if (!sandbox_value.IsObject()) {
    json_parser->ReportError("Sandbox is not an object.");
    return false;
  }

  for (const auto& entry : name_to_vec) {
    const std::string& name = entry.first;
    auto* vec = entry.second;
    auto member = sandbox_value.FindMember(name);
    if (member != sandbox_value.MemberEnd()) {
      CopyArrayToVector(name, member->value, json_parser, vec);
    }
  }

  if (!json_parser->HasError()) {
    null_ = false;
  }
  return !json_parser->HasError();
}

bool SandboxMetadata::HasFeature(const std::string& feature) const {
  return std::find(features_.begin(), features_.end(), feature) !=
         features_.end();
}

void SandboxMetadata::AddFeature(std::string feature) {
  features_.push_back(std::move(feature));
}

}  // namespace component
