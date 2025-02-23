// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <fbl/array.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/name.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <lib/user_copy/user_ptr.h>
#include <list.h>
#include <stdint.h>
#include <vm/page.h>
#include <vm/vm.h>
#include <vm/vm_page_list.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

class VmMapping;
class PageRequest;

typedef zx_status_t (*vmo_lookup_fn_t)(void* context, size_t offset, size_t index, paddr_t pa);

class VmObjectChildObserver {
public:
    virtual void OnZeroChild() = 0;
    virtual void OnOneChild() = 0;
};

// The base vm object that holds a range of bytes of data
//
// Can be created without mapping and used as a container of data, or mappable
// into an address space via VmAddressRegion::CreateVmMapping
class VmObject : public fbl::RefCounted<VmObject>,
                 public fbl::DoublyLinkedListable<VmObject*> {
public:
    // public API
    virtual zx_status_t Resize(uint64_t size) { return ZX_ERR_NOT_SUPPORTED; }

    virtual uint64_t size() const { return 0; }
    virtual uint32_t create_options() const { return 0; }

    // Returns true if the object is backed by RAM.
    virtual bool is_paged() const { return false; }
    // Returns true if the object is backed by a contiguous range of physical
    // memory.
    virtual bool is_contiguous() const { return false; }
    // Returns true if the object size can be changed.
    virtual bool is_resizable() const { return false; }
    // Returns true if the VMO was created vma CreatePagerVmo().
    virtual bool is_pager_backed() const { return false; }

    // Returns the number of physical pages currently allocated to the
    // object where (offset <= page_offset < offset+len).
    // |offset| and |len| are in bytes.
    virtual size_t AllocatedPagesInRange(uint64_t offset, uint64_t len) const {
        return 0;
    }
    // Returns the number of physical pages currently allocated to the object.
    size_t AllocatedPages() const {
        return AllocatedPagesInRange(0, size());
    }

    // find physical pages to back the range of the object
    virtual zx_status_t CommitRange(uint64_t offset, uint64_t len) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // free a range of the vmo back to the default state
    virtual zx_status_t DecommitRange(uint64_t offset, uint64_t len) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Pin the given range of the vmo.  If any pages are not committed, this
    // returns a ZX_ERR_NO_MEMORY.
    virtual zx_status_t Pin(uint64_t offset, uint64_t len) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Unpin the given range of the vmo.  This asserts if it tries to unpin a
    // page that is already not pinned (do not expose this function to
    // usermode).
    virtual void Unpin(uint64_t offset, uint64_t len) {
        panic("Unpin should only be called on a pinned range");
    }

    // read/write operators against kernel pointers only
    virtual zx_status_t Read(void* ptr, uint64_t offset, size_t len) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t Write(const void* ptr, uint64_t offset, size_t len) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // execute lookup_fn on a given range of physical addresses within the vmo
    virtual zx_status_t Lookup(uint64_t offset, uint64_t len,
                               vmo_lookup_fn_t lookup_fn, void* context) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // read/write operators against user space pointers only
    virtual zx_status_t ReadUser(user_out_ptr<void> ptr, uint64_t offset, size_t len) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t WriteUser(user_in_ptr<const void> ptr, uint64_t offset, size_t len) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Removes the pages from this vmo in the range [offset, offset + len) and returns
    // them in pages.  This vmo must be a paged vmo with no parent, and it cannot have any
    // pinned pages in the source range.
    virtual zx_status_t TakePages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Supplies this vmo with pages for the range [offset, offset + len). If this vmo
    // already has pages in the target range, the corresponding pages in |pages| will be
    // freed, instead of being moved into this vmo.
    virtual zx_status_t SupplyPages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // The associated VmObjectDispatcher will set an observer to notify user mode.
    void SetChildObserver(VmObjectChildObserver* child_observer);

    // Returns a null-terminated name, or the empty string if set_name() has not
    // been called.
    void get_name(char* out_name, size_t len) const;

    // Sets the name of the object. May truncate internally. |len| is the size
    // of the buffer pointed to by |name|.
    zx_status_t set_name(const char* name, size_t len);

    // Returns a user ID associated with this VMO, or zero.
    // Typically used to hold a zircon koid for Dispatcher-wrapped VMOs.
    uint64_t user_id() const;

    // Returns the parent's user_id() if this VMO has a parent,
    // otherwise returns zero.
    uint64_t parent_user_id() const;

    // Sets the value returned by |user_id()|. May only be called once.
    void set_user_id(uint64_t user_id);

    virtual void Dump(uint depth, bool verbose) = 0;

    // cache maintenance operations.
    virtual zx_status_t InvalidateCache(const uint64_t offset, const uint64_t len) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t CleanCache(const uint64_t offset, const uint64_t len) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t CleanInvalidateCache(const uint64_t offset, const uint64_t len) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    virtual zx_status_t SyncCache(const uint64_t offset, const uint64_t len) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    virtual uint32_t GetMappingCachePolicy() const = 0;
    virtual zx_status_t SetMappingCachePolicy(const uint32_t cache_policy) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // create a copy-on-write clone vmo at the page-aligned offset and length
    // note: it's okay to start or extend past the size of the parent
    virtual zx_status_t CloneCOW(bool resizable,
                                 uint64_t offset, uint64_t size, bool copy_name,
                                 fbl::RefPtr<VmObject>* clone_vmo) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Returns true if this VMO was created via CloneCOW().
    // TODO: If more types of clones appear, replace this with a method that
    // returns an enum rather than adding a new method for each clone type.
    bool is_cow_clone() const;

    // Get a pointer to the page structure and/or physical address at the specified offset.
    // valid flags are VMM_PF_FLAG_*.
    //
    // |page_request| must be non-null if any flags in VMM_PF_FLAG_FAULT_MASK are set, unless
    // the caller knows that the vm object is not paged.
    //
    // Returns ZX_ERR_SHOULD_WAIT if the caller should try again after waiting on the
    // PageRequest.
    //
    // Returns ZX_ERR_NEXT if |page_request| supports batching and the current request
    // can be batched. The caller should continue to make successive GetPage requests
    // until this returns ZX_ERR_SHOULD_WAIT. If the caller runs out of requests, it
    // should finalize the request with PageSource::FinalizeRequest.
    //
    // TODO: Currently the caller can also pass null if it knows that the vm object has no
    // page source. This will no longer be the case once page allocations can be delayed.
    zx_status_t GetPage(uint64_t offset, uint pf_flags, list_node* free_list,
                        PageRequest* page_request, vm_page_t** page, paddr_t* pa) {
        Guard<fbl::Mutex> guard{&lock_};
        return GetPageLocked(offset, pf_flags, free_list, page_request, page, pa);
    }

    // See VmObject::GetPage
    virtual zx_status_t GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                                      PageRequest* page_request,
                                      vm_page_t** page, paddr_t* pa) TA_REQ(lock_) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    Lock<fbl::Mutex>* lock() TA_RET_CAP(lock_) { return &lock_; }
    Lock<fbl::Mutex>& lock_ref() TA_RET_CAP(lock_) { return lock_; }

    void AddMappingLocked(VmMapping* r) TA_REQ(lock_);
    void RemoveMappingLocked(VmMapping* r) TA_REQ(lock_);
    uint32_t num_mappings() const;

    // Returns true if this VMO is mapped into any VmAspace whose is_user()
    // returns true.
    bool IsMappedByUser() const;

    // Returns an estimate of the number of unique VmAspaces that this object
    // is mapped into.
    uint32_t share_count() const;

    // Returns the number of children post-add.
    uint32_t AddChildLocked(VmObject* r) TA_REQ(lock_);

    // Notifies the chlid observer that there is one child.
    void NotifyOneChild() TA_EXCL(lock_);

    void RemoveChild(VmObject* r);
    uint32_t num_children() const;

    // Calls the provided |func(const VmObject&)| on every VMO in the system,
    // from oldest to newest. Stops if |func| returns an error, returning the
    // error value.
    template <typename T>
    static zx_status_t ForEach(T func) {
        Guard<fbl::Mutex> guard{AllVmosLock::Get()};
        for (const auto& iter : all_vmos_) {
            zx_status_t s = func(iter);
            if (s != ZX_OK) {
                return s;
            }
        }
        return ZX_OK;
    }

    // Detaches the underlying page source, if present. Can be called multiple times.
    virtual void DetachSource() {}

protected:
    // private constructor (use Create())
    explicit VmObject(fbl::RefPtr<VmObject> parent);
    VmObject()
        : VmObject(nullptr) {}

    // private destructor, only called from refptr
    virtual ~VmObject();
    friend fbl::RefPtr<VmObject>;

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmObject);

    // inform all mappings and children that a range of this vmo's pages were added or removed.
    void RangeChangeUpdateLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

    // above call but called from a parent
    virtual void RangeChangeUpdateFromParentLocked(uint64_t offset, uint64_t len)
        // Called under the parent's lock, which confuses analysis.
        TA_NO_THREAD_SAFETY_ANALYSIS { RangeChangeUpdateLocked(offset, len); }

    // magic value
    fbl::Canary<fbl::magic("VMO_")> canary_;

    // members

    // declare a local mutex and default to pointing at it
    // if constructed with a parent vmo, point lock_ at the parent's lock
private:
    DECLARE_MUTEX(VmObject) local_lock_;

protected:
    Lock<fbl::Mutex>& lock_;

    // list of every mapping
    fbl::DoublyLinkedList<VmMapping*> mapping_list_ TA_GUARDED(lock_);

    // list of every child
    fbl::DoublyLinkedList<VmObject*> children_list_ TA_GUARDED(lock_);

    // parent pointer (may be null)
    fbl::RefPtr<VmObject> parent_ TA_GUARDED(lock_);

    // lengths of corresponding lists
    uint32_t mapping_list_len_ TA_GUARDED(lock_) = 0;
    uint32_t children_list_len_ TA_GUARDED(lock_) = 0;

    uint64_t user_id_ TA_GUARDED(lock_) = 0;

    // The user-friendly VMO name. For debug purposes only. That
    // is, there is no mechanism to get access to a VMO via this name.
    fbl::Name<ZX_MAX_NAME_LEN> name_;

private:
    mutable DECLARE_MUTEX(VmObject) child_observer_lock_;

    // This member, if not null, is used to signal the user facing Dispatcher.
    VmObjectChildObserver* child_observer_ TA_GUARDED(child_observer_lock_) = nullptr;

    // Per-node state for the global VMO list.
    using NodeState = fbl::DoublyLinkedListNodeState<VmObject*>;
    NodeState global_list_state_;

    // The global VMO list.
    struct GlobalListTraits {
        static NodeState& node_state(VmObject& vmo) {
            return vmo.global_list_state_;
        }
    };
    using GlobalList = fbl::DoublyLinkedList<VmObject*, GlobalListTraits>;
    DECLARE_SINGLETON_MUTEX(AllVmosLock);
    static GlobalList all_vmos_ TA_GUARDED(AllVmosLock::Get());
};
