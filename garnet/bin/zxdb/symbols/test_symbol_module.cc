// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/test_symbol_module.h"

#include "garnet/bin/zxdb/common/host_util.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"

namespace zxdb {

const char TestSymbolModule::kMyFunctionName[] = "MyFunction";
const int TestSymbolModule::kMyFunctionLine = 96;
const char TestSymbolModule::kNamespaceFunctionName[] =
    "my_ns::NamespaceFunction";
const char TestSymbolModule::kMyMemberOneName[] = "my_ns::MyClass::MyMemberOne";
const char TestSymbolModule::kFunctionInTest2Name[] =
    "ClassInTest2::FunctionInTest2";
const char TestSymbolModule::kMyMemberTwoName[] =
    "my_ns::MyClass::Inner::MyMemberTwo";
const char TestSymbolModule::kGlobalName[] = "my_ns::kGlobal";
const char TestSymbolModule::kClassStaticName[] =
    "my_ns::MyClass::kClassStatic";
const char TestSymbolModule::kPltFunctionName[] = "__stack_chk_fail";
const uint64_t TestSymbolModule::kPltFunctionOffset = 0x10e0U;

TestSymbolModule::TestSymbolModule() = default;
TestSymbolModule::~TestSymbolModule() = default;

namespace {

inline std::string GetTestFilePath(const std::string& rel_path) {
  std::string path = GetSelfPath();
  size_t last_slash = path.rfind('/');
  if (last_slash == std::string::npos) {
    path = "./";  // Just hope the current directory works.
  } else {
    path.resize(last_slash + 1);
  }
  return path + rel_path;
}

// The test files will be copied over to this specific location at build time.
const char kRelativeTestDataPath[] = "test_data/zxdb/";

}  // namespace

// static
std::string TestSymbolModule::GetTestFileName() {
  std::string path = GetTestFilePath(kRelativeTestDataPath);
  return path + "libzxdb_symbol_test.targetso";
}

std::string TestSymbolModule::GetCheckedInTestFileName() {
  std::string path = GetTestFilePath(kRelativeTestDataPath);
  return path + "libsymbol_test_so.targetso";
}

bool TestSymbolModule::Load(std::string* err_msg) {
  return LoadSpecific(GetTestFileName(), err_msg);
}

bool TestSymbolModule::LoadSpecific(const std::string& path,
                                    std::string* err_msg) {
  llvm::Expected<llvm::object::OwningBinary<llvm::object::Binary>> bin_or_err =
      llvm::object::createBinary(path);
  if (!bin_or_err) {
    auto err_str = llvm::toString(bin_or_err.takeError());
    *err_msg =
        "Error loading symbols for \"" + path + "\", LLVM said: " + err_str;
    return false;
  }

  auto binary_pair = bin_or_err->takeBinary();
  binary_buffer_ = std::move(binary_pair.second);
  binary_ = std::move(binary_pair.first);

  llvm::object::ObjectFile* obj =
      static_cast<llvm::object::ObjectFile*>(binary_.get());
  context_ = llvm::DWARFContext::create(
      *obj, nullptr, llvm::DWARFContext::defaultErrorHandler);

  context_->getDWARFObj().forEachInfoSections(
      [this](const llvm::DWARFSection& s) {
        compile_units_.addUnitsForSection(*context_, s, llvm::DW_SECT_INFO);
      });
  return true;
}

}  // namespace zxdb
