// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_EVICTION_POLICIES_H_
#define SRC_LEDGER_BIN_APP_PAGE_EVICTION_POLICIES_H_

#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>

#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/iterator.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine.h"

namespace ledger {

using PageWasEvicted = bool;

// The policy for evicting pages.
class PageEvictionPolicy {
 public:
  PageEvictionPolicy() {}
  virtual ~PageEvictionPolicy() {}

  // Given an iterator over all pages currently stored on disk, chooses and
  // tries to evict those that match the implementing policy. The status
  // returned through the |callback| will be |IO_ERROR| in case of failure
  // during trying to evict a page; |OK| otherwise. It is not an error if no
  // page was evited.
  virtual void SelectAndEvict(
      std::unique_ptr<storage::Iterator<const PageInfo>> pages,
      fit::function<void(Status)> callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageEvictionPolicy);
};

// The condition to be checked before evicting a page.
enum PageEvictionCondition {
  // Indicates the page should be deleted if possible.
  IF_POSSIBLE,
  // Indicates the page should be deleted only if it is empty and offline.
  IF_EMPTY,
};

// The delegate used for the |PageEvictionPolicy|. Provides the methods
// necessary to evict pages.
class PageEvictionDelegate {
 public:
  PageEvictionDelegate() {}
  virtual ~PageEvictionDelegate() {}

  // Checks whether the given page can be evicted based on the given
  // |condition| and if it can, evicts it. Note that evicting a page with
  // |IF_EMPTY| has no observable effect for the user, i.e. doesn't break the
  // offline case. |IF_POSSIBLE| on the other hand means that a completely
  // synced page might be evicted, and thus become unavailable to the user, if
  // offline. Returns |IO_ERROR| through the callback in case of failure while
  // retrieving information on the page, or when trying to evict it; |OK|
  // otherwise. The boolean in the callback indicates whether the page was
  // evicted.
  virtual void TryEvictPage(
      fxl::StringView ledger_name, storage::PageIdView page_id,
      PageEvictionCondition condition,
      fit::function<void(Status, PageWasEvicted)> callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageEvictionDelegate);
};

// Creates and returns a new Least-Recently-Used policy, which evicts the least
// recently used page among those that can be evicted. The given delegate should
// outlive the returned object.
std::unique_ptr<PageEvictionPolicy> NewLeastRecentyUsedPolicy(
    coroutine::CoroutineService* corroutine_service,
    PageEvictionDelegate* delegate);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_EVICTION_POLICIES_H_
