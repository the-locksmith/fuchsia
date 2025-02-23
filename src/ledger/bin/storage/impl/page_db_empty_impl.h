// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_DB_EMPTY_IMPL_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_DB_EMPTY_IMPL_H_

#include "src/ledger/bin/storage/impl/page_db.h"

namespace storage {

class PageDbEmptyImpl : public PageDb, public PageDb::Batch {
 public:
  PageDbEmptyImpl() {}
  ~PageDbEmptyImpl() override {}

  // PageDb:
  Status StartBatch(coroutine::CoroutineHandler* handler,
                    std::unique_ptr<PageDb::Batch>* batch) override;
  Status GetHeads(
      coroutine::CoroutineHandler* handler,
      std::vector<std::pair<zx::time_utc, CommitId>>* heads) override;
  Status GetMerges(coroutine::CoroutineHandler* handler,
                   CommitIdView parent1_id, CommitIdView parent2_id,
                   std::vector<CommitId>* merges) override;
  Status GetCommitStorageBytes(coroutine::CoroutineHandler* handler,
                               CommitIdView commit_id,
                               std::string* storage_bytes) override;

  // PageDb and PageDb::Batch:
  Status ReadObject(coroutine::CoroutineHandler* handler,
                    const ObjectIdentifier& object_identifier,
                    std::unique_ptr<const Object>* object) override;
  Status HasObject(coroutine::CoroutineHandler* handler,
                   const ObjectIdentifier& object_identifier) override;
  Status GetUnsyncedCommitIds(coroutine::CoroutineHandler* handler,
                              std::vector<CommitId>* commit_ids) override;
  Status IsCommitSynced(coroutine::CoroutineHandler* handler,
                        const CommitId& commit_id, bool* is_synced) override;
  Status GetUnsyncedPieces(
      coroutine::CoroutineHandler* handler,
      std::vector<ObjectIdentifier>* object_identifiers) override;
  Status GetObjectStatus(coroutine::CoroutineHandler* handler,
                         const ObjectIdentifier& object_identifier,
                         PageDbObjectStatus* object_status) override;
  Status GetSyncMetadata(coroutine::CoroutineHandler* handler,
                         fxl::StringView key, std::string* value) override;

  Status IsPageOnline(coroutine::CoroutineHandler* handler,
                      bool* page_is_online) override;

  Status AddHead(coroutine::CoroutineHandler* handler, CommitIdView head,
                 zx::time_utc timestamp) override;
  Status RemoveHead(coroutine::CoroutineHandler* handler,
                    CommitIdView head) override;
  Status AddMerge(coroutine::CoroutineHandler* handler, CommitIdView parent1_id,
                  CommitIdView parent2_id,
                  CommitIdView merge_commit_id) override;
  Status AddCommitStorageBytes(coroutine::CoroutineHandler* handler,
                               const CommitId& commit_id,
                               fxl::StringView storage_bytes) override;
  Status RemoveCommit(coroutine::CoroutineHandler* handler,
                      const CommitId& commit_id) override;
  Status WriteObject(coroutine::CoroutineHandler* handler,
                     const ObjectIdentifier& object_identifier,
                     std::unique_ptr<DataSource::DataChunk> content,
                     PageDbObjectStatus object_status) override;
  Status MarkCommitIdSynced(coroutine::CoroutineHandler* handler,
                            const CommitId& commit_id) override;
  Status MarkCommitIdUnsynced(coroutine::CoroutineHandler* handler,
                              const CommitId& commit_id,
                              uint64_t generation) override;
  Status SetObjectStatus(coroutine::CoroutineHandler* handler,
                         const ObjectIdentifier& object_identifier,
                         PageDbObjectStatus object_status) override;
  Status SetSyncMetadata(coroutine::CoroutineHandler* handler,
                         fxl::StringView key, fxl::StringView value) override;

  Status MarkPageOnline(coroutine::CoroutineHandler* handler) override;

  // PageDb::Batch:
  Status Execute(coroutine::CoroutineHandler* handler) override;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_DB_EMPTY_IMPL_H_
