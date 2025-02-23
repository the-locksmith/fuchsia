// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_

#include <vector>

#include <lib/async/dispatcher.h>
#include <lib/callback/managed_container.h>
#include <lib/callback/operation_serializer.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fxl/memory/ref_ptr.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/storage/impl/live_commit_tracker.h"
#include "src/ledger/bin/storage/impl/page_db_impl.h"
#include "src/ledger/bin/storage/public/db.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/page_sync_delegate.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"

namespace storage {

class PageStorageImpl : public PageStorage {
 public:
  PageStorageImpl(ledger::Environment* environment,
                  encryption::EncryptionService* encryption_service,
                  std::unique_ptr<Db> db, PageId page_id);
  PageStorageImpl(ledger::Environment* environment,
                  encryption::EncryptionService* encryption_service,
                  std::unique_ptr<PageDb> page_db, PageId page_id);

  ~PageStorageImpl() override;

  // Initializes this PageStorageImpl. This includes initializing the underlying
  // database and adding the default page head if the page is empty.
  void Init(fit::function<void(Status)> callback);

  // Adds the given locally created |commit| in this |PageStorage|.
  void AddCommitFromLocal(std::unique_ptr<const Commit> commit,
                          std::vector<ObjectIdentifier> new_objects,
                          fit::function<void(Status)> callback);

  // Checks whether the given |object_identifier| is untracked, i.e. has been
  // created using |AddObjectFromLocal()|, but is not yet part of any commit.
  // Untracked objects are invalid after the PageStorageImpl object is
  // destroyed.
  void ObjectIsUntracked(ObjectIdentifier object_identifier,
                         fit::function<void(Status, bool)> callback);

  // PageStorage:
  PageId GetId() override;
  void SetSyncDelegate(PageSyncDelegate* page_sync) override;
  Status GetHeadCommitIds(std::vector<CommitId>* head_commit_ids) override;
  void GetMergeCommitIds(
      CommitIdView parent1_id, CommitIdView parent2_id,
      fit::function<void(Status, std::vector<CommitId>)> callback) override;
  void GetCommit(CommitIdView commit_id,
                 fit::function<void(Status, std::unique_ptr<const Commit>)>
                     callback) override;
  void AddCommitsFromSync(
      std::vector<CommitIdAndBytes> ids_and_bytes, ChangeSource source,
      fit::function<void(Status, std::vector<CommitId>)> callback) override;
  std::unique_ptr<Journal> StartCommit(const CommitId& commit_id) override;
  std::unique_ptr<Journal> StartMergeCommit(const CommitId& left,
                                            const CommitId& right) override;
  void CommitJournal(std::unique_ptr<Journal> journal,
                     fit::function<void(Status, std::unique_ptr<const Commit>)>
                         callback) override;
  Status AddCommitWatcher(CommitWatcher* watcher) override;
  Status RemoveCommitWatcher(CommitWatcher* watcher) override;
  void IsSynced(fit::function<void(Status, bool)> callback) override;
  bool IsOnline() override;
  void IsEmpty(fit::function<void(Status, bool)> callback) override;
  void GetUnsyncedCommits(
      fit::function<void(Status, std::vector<std::unique_ptr<const Commit>>)>
          callback) override;
  void MarkCommitSynced(const CommitId& commit_id,
                        fit::function<void(Status)> callback) override;
  void GetUnsyncedPieces(
      fit::function<void(Status, std::vector<ObjectIdentifier>)> callback)
      override;
  void MarkPieceSynced(ObjectIdentifier object_identifier,
                       fit::function<void(Status)> callback) override;
  void IsPieceSynced(ObjectIdentifier object_identifier,
                     fit::function<void(Status, bool)> callback) override;
  void MarkSyncedToPeer(fit::function<void(Status)> callback) override;
  void AddObjectFromLocal(
      ObjectType object_type, std::unique_ptr<DataSource> data_source,
      fit::function<void(Status, ObjectIdentifier)> callback) override;
  void GetObjectPart(
      ObjectIdentifier object_identifier, int64_t offset, int64_t max_size,
      Location location,
      fit::function<void(Status, fsl::SizedVmo)> callback) override;
  void GetObject(ObjectIdentifier object_identifier, Location location,
                 fit::function<void(Status, std::unique_ptr<const Object>)>
                     callback) override;
  void GetPiece(ObjectIdentifier object_identifier,
                fit::function<void(Status, std::unique_ptr<const Object>)>
                    callback) override;
  void SetSyncMetadata(fxl::StringView key, fxl::StringView value,
                       fit::function<void(Status)> callback) override;
  void GetSyncMetadata(
      fxl::StringView key,
      fit::function<void(Status, std::string)> callback) override;

  // Commit contents.
  void GetCommitContents(const Commit& commit, std::string min_key,
                         fit::function<bool(Entry)> on_next,
                         fit::function<void(Status)> on_done) override;
  void GetEntryFromCommit(const Commit& commit, std::string key,
                          fit::function<void(Status, Entry)> callback) override;
  void GetCommitContentsDiff(const Commit& base_commit,
                             const Commit& other_commit, std::string min_key,
                             fit::function<bool(EntryChange)> on_next_diff,
                             fit::function<void(Status)> on_done) override;
  void GetThreeWayContentsDiff(const Commit& base_commit,
                               const Commit& left_commit,
                               const Commit& right_commit, std::string min_key,
                               fit::function<bool(ThreeWayChange)> on_next_diff,
                               fit::function<void(Status)> on_done) override;

 private:
  friend class PageStorageImplAccessorForTest;

  // Marks all pieces needed for the given objects as local.
  FXL_WARN_UNUSED_RESULT Status
  MarkAllPiecesLocal(coroutine::CoroutineHandler* handler, PageDb::Batch* batch,
                     std::vector<ObjectIdentifier> object_identifiers);

  FXL_WARN_UNUSED_RESULT Status
  ContainsCommit(coroutine::CoroutineHandler* handler, CommitIdView id);

  bool IsFirstCommit(CommitIdView id);

  // Adds the given synced object. |object_identifier| is expected to match the
  // given |data|.
  void AddPiece(ObjectIdentifier object_identifier, ChangeSource source,
                IsObjectSynced is_object_synced,
                std::unique_ptr<DataSource::DataChunk> data,
                fit::function<void(Status)> callback);

  // Download all the chunks of the object with the given id.
  void DownloadFullObject(ObjectIdentifier object_identifier,
                          fit::function<void(Status)> callback);

  void GetObjectPartFromSync(
      ObjectIdentifier object_identifier, size_t offset, size_t size,
      fit::function<void(Status, fsl::SizedVmo)> callback);

  // Reads the content of an object into a provided VMO. Takes into
  // account the global offset and size in order to be able to read only the
  // requested part of an object.
  // |global_offset| is the offset from the beginning of the full object in
  // bytes. |global_size| is the maximum size requested to be read into the vmo.
  // |current_position| is the position of the currently read piece (defined by
  // |object_identifier|) in the full object. |object_size| is the size of the
  // currently read piece.
  void FillBufferWithObjectContent(ObjectIdentifier object_identifier,
                                   fsl::SizedVmo vmo, int64_t global_offset,
                                   size_t global_size, int64_t current_position,
                                   size_t object_size,
                                   fit::function<void(Status)> callback);
  void FillBufferWithObjectContent(std::unique_ptr<const Object> object,
                                   fsl::SizedVmo vmo, int64_t global_offset,
                                   size_t global_size, int64_t current_position,
                                   size_t object_size,
                                   fit::function<void(Status)> callback);

  // Notifies the registered watchers of |new_commits|.
  void NotifyWatchersOfNewCommits(

      const std::vector<std::unique_ptr<const Commit>>& new_commits,
      ChangeSource source);

  // Synchronous versions of API methods using coroutines.
  FXL_WARN_UNUSED_RESULT Status
  SynchronousInit(coroutine::CoroutineHandler* handler);

  FXL_WARN_UNUSED_RESULT Status
  SynchronousGetCommit(coroutine::CoroutineHandler* handler, CommitId commit_id,
                       std::unique_ptr<const Commit>* commit);

  FXL_WARN_UNUSED_RESULT Status
  SynchronousAddCommitFromLocal(coroutine::CoroutineHandler* handler,
                                std::unique_ptr<const Commit> commit,
                                std::vector<ObjectIdentifier> new_objects);

  FXL_WARN_UNUSED_RESULT Status SynchronousAddCommitsFromSync(
      coroutine::CoroutineHandler* handler,
      std::vector<CommitIdAndBytes> ids_and_bytes, ChangeSource source,
      std::vector<CommitId>* missing_ids);

  FXL_WARN_UNUSED_RESULT Status SynchronousGetUnsyncedCommits(
      coroutine::CoroutineHandler* handler,
      std::vector<std::unique_ptr<const Commit>>* unsynced_commits);

  FXL_WARN_UNUSED_RESULT Status SynchronousMarkCommitSynced(
      coroutine::CoroutineHandler* handler, const CommitId& commit_id);

  FXL_WARN_UNUSED_RESULT Status SynchronousMarkCommitSyncedInBatch(
      coroutine::CoroutineHandler* handler, PageDb::Batch* batch,
      const CommitId& commit_id);

  FXL_WARN_UNUSED_RESULT Status SynchronousAddCommits(
      coroutine::CoroutineHandler* handler,
      std::vector<std::unique_ptr<const Commit>> commits, ChangeSource source,
      std::vector<ObjectIdentifier> new_objects,
      std::vector<CommitId>* missing_ids);

  FXL_WARN_UNUSED_RESULT Status SynchronousAddPiece(
      coroutine::CoroutineHandler* handler, ObjectIdentifier object_identifier,
      ChangeSource source, IsObjectSynced is_object_synced,
      std::unique_ptr<DataSource::DataChunk> data);

  // Synchronous helper methods.

  // Marks this page as online.
  FXL_WARN_UNUSED_RESULT Status SynchronousMarkPageOnline(
      coroutine::CoroutineHandler* handler, PageDb::Batch* batch);

  // Updates the given |empty_node_id| to point to the empty node's
  // ObjectIdentifier.
  FXL_WARN_UNUSED_RESULT Status SynchronousGetEmptyNodeIdentifier(
      coroutine::CoroutineHandler* handler, ObjectIdentifier** empty_node_id);

  ledger::Environment* environment_;
  encryption::EncryptionService* const encryption_service_;
  const PageId page_id_;
  std::unique_ptr<PageDb> db_;
  std::vector<CommitWatcher*> watchers_;
  callback::ManagedContainer managed_container_;
  PageSyncDelegate* page_sync_;
  bool page_is_online_ = false;
  std::unique_ptr<ObjectIdentifier> empty_node_id_ = nullptr;
  LiveCommitTracker commit_tracker_;

  callback::OperationSerializer commit_serializer_;
  coroutine::CoroutineManager coroutine_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageStorageImpl);
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_
