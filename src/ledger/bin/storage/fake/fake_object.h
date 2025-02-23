// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_OBJECT_H_
#define SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_OBJECT_H_

#include <lib/fxl/strings/string_view.h>

#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {
namespace fake {

class FakeObject : public Object {
 public:
  FakeObject(ObjectIdentifier identifier, fxl::StringView content);
  ~FakeObject() override;
  ObjectIdentifier GetIdentifier() const override;
  Status GetData(fxl::StringView* data) const override;

 private:
  ObjectIdentifier identifier_;
  std::string content_;
};

}  // namespace fake
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_OBJECT_H_
