// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_COMMIT_H_
#define SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_COMMIT_H_

#include <memory>
#include <string>

#include "src/ledger/bin/storage/fake/fake_journal.h"
#include "src/ledger/bin/storage/fake/fake_journal_delegate.h"
#include "src/ledger/bin/storage/public/commit.h"

namespace storage {
namespace fake {

// A |FakeCommit| is a commit based on a |FakeJournalDelegate|.
class FakeCommit : public Commit {
 public:
  explicit FakeCommit(FakeJournalDelegate* journal);
  ~FakeCommit() override;

  // Commit:
  std::unique_ptr<const Commit> Clone() const override;

  const CommitId& GetId() const override;

  std::vector<CommitIdView> GetParentIds() const override;

  zx::time_utc GetTimestamp() const override;

  uint64_t GetGeneration() const override;

  ObjectIdentifier GetRootIdentifier() const override;

  fxl::StringView GetStorageBytes() const override;

 private:
  FakeJournalDelegate* journal_;
  FXL_DISALLOW_COPY_AND_ASSIGN(FakeCommit);
};

}  // namespace fake
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_COMMIT_H_
