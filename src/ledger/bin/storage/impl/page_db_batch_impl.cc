// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/page_db_batch_impl.h"

#include <memory>

#include <lib/fxl/strings/concatenate.h>

#include "src/ledger/bin/storage/impl/data_serialization.h"
#include "src/ledger/bin/storage/impl/db_serialization.h"

#define RETURN_ON_ERROR(expr)   \
  do {                          \
    Status status = (expr);     \
    if (status != Status::OK) { \
      return status;            \
    }                           \
  } while (0)

namespace storage {

using coroutine::CoroutineHandler;

PageDbBatchImpl::PageDbBatchImpl(std::unique_ptr<Db::Batch> batch, PageDb* db)
    : batch_(std::move(batch)), db_(db) {}

PageDbBatchImpl::~PageDbBatchImpl() {}

Status PageDbBatchImpl::AddHead(CoroutineHandler* handler, CommitIdView head,
                                zx::time_utc timestamp) {
  return batch_->Put(handler, HeadRow::GetKeyFor(head),
                     SerializeData(timestamp));
}

Status PageDbBatchImpl::RemoveHead(CoroutineHandler* handler,
                                   CommitIdView head) {
  return batch_->Delete(handler, HeadRow::GetKeyFor(head));
}

Status PageDbBatchImpl::AddMerge(coroutine::CoroutineHandler* handler,
                                 CommitIdView parent1_id,
                                 CommitIdView parent2_id,
                                 CommitIdView merge_commit_id) {
  return batch_->Put(
      handler, MergeRow::GetKeyFor(merge_commit_id, parent1_id, parent2_id),
      "");
}

Status PageDbBatchImpl::AddCommitStorageBytes(CoroutineHandler* handler,
                                              const CommitId& commit_id,
                                              fxl::StringView storage_bytes) {
  return batch_->Put(handler, CommitRow::GetKeyFor(commit_id), storage_bytes);
}

Status PageDbBatchImpl::RemoveCommit(CoroutineHandler* handler,
                                     const CommitId& commit_id) {
  return batch_->Delete(handler, CommitRow::GetKeyFor(commit_id));
}

Status PageDbBatchImpl::WriteObject(
    CoroutineHandler* handler, const ObjectIdentifier& object_identifier,
    std::unique_ptr<DataSource::DataChunk> content,
    PageDbObjectStatus object_status) {
  FXL_DCHECK(object_status > PageDbObjectStatus::UNKNOWN);

  Status status = db_->HasObject(handler, object_identifier);
  if (status == Status::OK) {
    if (object_status == PageDbObjectStatus::TRANSIENT) {
      return Status::OK;
    }
    return SetObjectStatus(handler, std::move(object_identifier),
                           object_status);
  }
  if (status != Status::NOT_FOUND) {
    return status;
  }

  RETURN_ON_ERROR(batch_->Put(
      handler, ObjectRow::GetKeyFor(object_identifier.object_digest()),
      content->Get()));
  return batch_->Put(
      handler, ObjectStatusRow::GetKeyFor(object_status, object_identifier),
      "");
}

Status PageDbBatchImpl::SetObjectStatus(
    CoroutineHandler* handler, const ObjectIdentifier& object_identifier,
    PageDbObjectStatus object_status) {
  FXL_DCHECK(object_status >= PageDbObjectStatus::LOCAL);
  RETURN_ON_ERROR(DCheckHasObject(handler, object_identifier));

  PageDbObjectStatus previous_object_status;
  RETURN_ON_ERROR(db_->GetObjectStatus(handler, object_identifier,
                                       &previous_object_status));
  if (previous_object_status >= object_status) {
    return Status::OK;
  }
  RETURN_ON_ERROR(batch_->Delete(
      handler,
      ObjectStatusRow::GetKeyFor(previous_object_status, object_identifier)));
  return batch_->Put(
      handler, ObjectStatusRow::GetKeyFor(object_status, object_identifier),
      "");
}

Status PageDbBatchImpl::MarkCommitIdSynced(CoroutineHandler* handler,
                                           const CommitId& commit_id) {
  return batch_->Delete(handler, UnsyncedCommitRow::GetKeyFor(commit_id));
}

Status PageDbBatchImpl::MarkCommitIdUnsynced(CoroutineHandler* handler,
                                             const CommitId& commit_id,
                                             uint64_t generation) {
  return batch_->Put(handler, UnsyncedCommitRow::GetKeyFor(commit_id),
                     SerializeData(generation));
}

Status PageDbBatchImpl::SetSyncMetadata(CoroutineHandler* handler,
                                        fxl::StringView key,
                                        fxl::StringView value) {
  return batch_->Put(handler, SyncMetadataRow::GetKeyFor(key), value);
}

Status PageDbBatchImpl::MarkPageOnline(coroutine::CoroutineHandler* handler) {
  return batch_->Put(handler, PageIsOnlineRow::kKey, "");
}

Status PageDbBatchImpl::Execute(CoroutineHandler* handler) {
  return batch_->Execute(handler);
}

Status PageDbBatchImpl::DCheckHasObject(CoroutineHandler* handler,
                                        const ObjectIdentifier& key) {
#ifdef NDEBUG
  return Status::OK;
#else
  Status status = db_->HasObject(handler, key);
  if (status == Status::INTERRUPTED) {
    return status;
  }
  FXL_DCHECK(status == Status::OK);
  return Status::OK;
#endif
}

}  // namespace storage
