// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_manager.h"

#include <algorithm>

#include <lib/callback/trace_callback.h>
#include <lib/fit/function.h>
#include <lib/fxl/logging.h>
#include <zircon/syscalls.h>

#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/impl/data_serialization.h"

namespace ledger {

PageManager::PageManager(Environment* environment,
                         std::unique_ptr<storage::PageStorage> page_storage,
                         std::unique_ptr<sync_coordinator::PageSync> page_sync,
                         std::unique_ptr<MergeResolver> merge_resolver,
                         PageManager::PageStorageState state,
                         zx::duration sync_timeout)
    : environment_(environment),
      page_storage_(std::move(page_storage)),
      page_sync_(std::move(page_sync)),
      merge_resolver_(std::move(merge_resolver)),
      sync_timeout_(sync_timeout),
      task_runner_(environment->dispatcher()) {
  pages_.set_on_empty([this] { CheckEmpty(); });
  snapshots_.set_on_empty([this] { CheckEmpty(); });

  if (page_sync_) {
    page_sync_->SetSyncWatcher(&watchers_);
    page_sync_->SetOnIdle([this] { CheckEmpty(); });
    page_sync_->SetOnBacklogDownloaded([this] { OnSyncBacklogDownloaded(); });
    page_sync_->Start();
    if (state == PageManager::PageStorageState::NEEDS_SYNC) {
      // The page storage was created locally. We wait a bit in order to get the
      // initial state from the network before accepting requests.
      task_runner_.PostDelayedTask(
          [this] {
            if (!sync_backlog_downloaded_) {
              FXL_LOG(INFO) << "Initial sync will continue in background, "
                            << "in the meantime binding to local page data "
                            << "(might be stale or empty).";
              OnSyncBacklogDownloaded();
            }
          },
          sync_timeout_);
    } else {
      sync_backlog_downloaded_ = true;
    }
  } else {
    sync_backlog_downloaded_ = true;
  }
  merge_resolver_->set_on_empty([this] { CheckEmpty(); });
  merge_resolver_->SetPageManager(this);
}

PageManager::~PageManager() {
  for (const auto& delaying_facade : delaying_facades_) {
    delaying_facade.second(Status::INTERNAL_ERROR);
  }
  delaying_facades_.clear();
}

void PageManager::AddPageDelayingFacade(
    std::unique_ptr<PageDelayingFacade> delaying_facade,
    fit::function<void(Status)> on_done) {
  auto traced_on_done = TRACE_CALLBACK(std::move(on_done), "ledger",
                                       "page_manager_add_page_delaying_facade");
  if (!sync_backlog_downloaded_) {
    delaying_facades_.emplace_back(std::move(delaying_facade),
                                   std::move(traced_on_done));
    return;
  }
  pages_
      .emplace(environment_->coroutine_service(), this, page_storage_.get(),
               merge_resolver_.get(), &watchers_, std::move(delaying_facade))
      .Init(std::move(traced_on_done));
}

void PageManager::BindPageSnapshot(
    std::unique_ptr<const storage::Commit> commit,
    fidl::InterfaceRequest<PageSnapshot> snapshot_request,
    std::string key_prefix) {
  snapshots_.emplace(std::move(snapshot_request), page_storage_.get(),
                     std::move(commit), std::move(key_prefix));
}

Reference PageManager::CreateReference(
    storage::ObjectIdentifier object_identifier) {
  uint64_t index = environment_->random()->Draw<uint64_t>();
  FXL_DCHECK(references_.find(index) == references_.end());
  references_[index] = std::move(object_identifier);
  Reference reference;
  reference.opaque_id = convert::ToArray(storage::SerializeData(index));
  return reference;
}

Status PageManager::ResolveReference(
    Reference reference, storage::ObjectIdentifier* object_identifier) {
  if (reference.opaque_id.size() != sizeof(uint64_t)) {
    return Status::REFERENCE_NOT_FOUND;
  }
  uint64_t index = storage::DeserializeData<uint64_t>(
      convert::ToStringView(reference.opaque_id));
  auto iterator = references_.find(index);
  if (iterator == references_.end()) {
    return Status::REFERENCE_NOT_FOUND;
  }
  *object_identifier = iterator->second;
  return Status::OK;
}

void PageManager::IsSynced(fit::function<void(Status, bool)> callback) {
  page_storage_->IsSynced(
      [callback = std::move(callback)](storage::Status status, bool is_synced) {
        callback(PageUtils::ConvertStatus(status), is_synced);
      });
}

void PageManager::IsOfflineAndEmpty(
    fit::function<void(Status, bool)> callback) {
  if (page_storage_->IsOnline()) {
    callback(Status::OK, false);
    return;
  }
  // The page is offline. Check and return if it's also empty.
  page_storage_->IsEmpty(
      [callback = std::move(callback)](storage::Status status, bool is_empty) {
        callback(PageUtils::ConvertStatus(status), is_empty);
      });
}

bool PageManager::IsEmpty() {
  return pages_.empty() && snapshots_.empty() && delaying_facades_.empty() &&
         merge_resolver_->IsEmpty() && (!page_sync_ || page_sync_->IsIdle());
}

void PageManager::CheckEmpty() {
  if (on_empty_callback_ && IsEmpty()) {
    on_empty_callback_();
  }
}

void PageManager::OnSyncBacklogDownloaded() {
  if (sync_backlog_downloaded_) {
    FXL_LOG(INFO) << "Initial sync in background finished. "
                  << "Clients will receive a change notification.";
  }
  sync_backlog_downloaded_ = true;
  for (auto& delaying_facade : delaying_facades_) {
    AddPageDelayingFacade(std::move(delaying_facade.first),
                          std::move(delaying_facade.second));
  }
  delaying_facades_.clear();
}

}  // namespace ledger
