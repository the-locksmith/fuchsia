// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/dispatcher.h>

#include <inttypes.h>

#include <arch/ops.h>
#include <lib/ktrace.h>
#include <lib/counters.h>
#include <fbl/mutex.h>
#include <ktl/atomic.h>

#include <object/tls_slots.h>


// kernel counters. The following counters never decrease.
// counts the number of times a dispatcher has been created and destroyed.
KCOUNTER(dispatcher_create_count, "dispatcher.create");
KCOUNTER(dispatcher_destroy_count, "dispatcher.destroy");
// counts the number of times observers have been added to a kernel object.
KCOUNTER(dispatcher_observe_count, "dispatcher.observer.add");
// counts the number of times observers have been canceled.
KCOUNTER(dispatcher_cancel_bh_count, "dispatcher.observer.cancel.byhandle");
KCOUNTER(dispatcher_cancel_bk_count, "dispatcher.observer.cancel.bykey");
// counts the number of cookies set or changed (reset).
KCOUNTER(dispatcher_cookie_set_count, "dispatcher.cookie.set");
KCOUNTER(dispatcher_cookie_reset_count, "dispatcher.cookie.reset");

namespace {
// The first 1K koids are reserved.
ktl::atomic<zx_koid_t> global_koid(1024ULL);

zx_koid_t GenerateKernelObjectId() {
    return global_koid.fetch_add(1ULL, ktl::memory_order_relaxed);
}

// Helper class that safely allows deleting Dispatchers without
// risk of blowing up the kernel stack. It uses one TLS slot to
// unwind the recursion.
class SafeDeleter {
public:
    static void Delete(Dispatcher* kobj) {
        auto deleter = reinterpret_cast<SafeDeleter*>(tls_get(TLS_ENTRY_KOBJ_DELETER));
        if (deleter) {
            // Delete() was called recursively.
            deleter->pending_.push_front(kobj);
        } else {
            SafeDeleter deleter;
            tls_set(TLS_ENTRY_KOBJ_DELETER, &deleter);

            do {
                // This delete call can cause recursive calls to
                // Dispatcher::fbl_recycle() and hence to Delete().
                delete kobj;

                kobj = deleter.pending_.pop_front();
            } while (kobj);

            tls_set(TLS_ENTRY_KOBJ_DELETER, nullptr);
        }
    }

private:
    fbl::SinglyLinkedList<Dispatcher*, Dispatcher::DeleterListTraits> pending_;
};

}  // namespace

Dispatcher::Dispatcher(zx_signals_t signals)
    : koid_(GenerateKernelObjectId()),
      handle_count_(0u),
      signals_(signals) {

    kcounter_add(dispatcher_create_count, 1);
}

Dispatcher::~Dispatcher() {
    ktrace(TAG_OBJECT_DELETE, (uint32_t)koid_, 0, 0, 0);
    kcounter_add(dispatcher_destroy_count, 1);
}

// The refcount of this object has reached zero: delete self
// using the SafeDeleter to avoid potential recursion hazards.
// TODO(cpu): Not all object need the SafeDeleter. Only objects
// that can control the lifetime of dispatchers that in turn
// can control the lifetime of others. For example events do
// not fall in this category.
void Dispatcher::fbl_recycle() {
    canary_.Assert();

    SafeDeleter::Delete(this);
}

zx_status_t Dispatcher::add_observer(StateObserver* observer) {
    canary_.Assert();

    if (!is_waitable())
        return ZX_ERR_NOT_SUPPORTED;
    AddObserver(observer, nullptr);
    return ZX_OK;
}

namespace {

template <typename Func, typename LockType>
StateObserver::Flags CancelWithFunc(Dispatcher::ObserverList* observers,
                                    Lock<LockType>* observer_lock, Func f) {
    StateObserver::Flags flags = 0;

    Dispatcher::ObserverList obs_to_remove;

    {
        Guard<LockType> guard{observer_lock};
        for (auto it = observers->begin(); it != observers->end();) {
            StateObserver::Flags it_flags = f(it.CopyPointer());
            flags |= it_flags;
            if (it_flags & StateObserver::kNeedRemoval) {
                auto to_remove = it;
                ++it;
                obs_to_remove.push_back(observers->erase(to_remove));
            } else {
                ++it;
            }
        }
    }

    while (!obs_to_remove.is_empty()) {
        obs_to_remove.pop_front()->OnRemoved();
    }

    // We've processed the removal flag, so strip it
    return flags & (~StateObserver::kNeedRemoval);
}

}  // namespace

// Since this conditionally takes the dispatcher's |lock_|, based on
// the type of Mutex (either fbl::Mutex or fbl::NullLock), the thread
// safety analysis is unable to prove that the accesses to |signals_|
// and to |observers_| are always protected.
template <typename LockType>
void Dispatcher::AddObserverHelper(StateObserver* observer,
                                   const StateObserver::CountInfo* cinfo,
                                   Lock<LockType>* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();
    ZX_DEBUG_ASSERT(is_waitable());
    DEBUG_ASSERT(observer != nullptr);

    StateObserver::Flags flags;
    {
        Guard<LockType> guard{lock};

        flags = observer->OnInitialize(signals_, cinfo);
        if (!(flags & StateObserver::kNeedRemoval))
            observers_.push_front(observer);
    }
    if (flags & StateObserver::kNeedRemoval)
        observer->OnRemoved();

    kcounter_add(dispatcher_observe_count, 1);
}

void Dispatcher::AddObserver(StateObserver* observer, const StateObserver::CountInfo* cinfo) {
    canary_.Assert();

    AddObserverHelper(observer, cinfo, get_lock());
}

void Dispatcher::AddObserverLocked(StateObserver* observer, const StateObserver::CountInfo* cinfo) {
    canary_.Assert();

    // Type tag and local NullLock to make lockdep happy.
    struct DispatcherAddObserverLocked {};
    DECLARE_LOCK(DispatcherAddObserverLocked, fbl::NullLock) lock;

    AddObserverHelper(observer, cinfo, &lock);
}

void Dispatcher::RemoveObserver(StateObserver* observer) {
    canary_.Assert();
    ZX_DEBUG_ASSERT(is_waitable());

    Guard<fbl::Mutex> guard{get_lock()};
    DEBUG_ASSERT(observer != nullptr);
    observers_.erase(*observer);
}

void Dispatcher::Cancel(const Handle* handle) {
    canary_.Assert();
    ZX_DEBUG_ASSERT(is_waitable());

    CancelWithFunc(&observers_, get_lock(), [handle](StateObserver* obs) {
        return obs->OnCancel(handle);
    });

    kcounter_add(dispatcher_cancel_bh_count, 1);
}

bool Dispatcher::CancelByKey(const Handle* handle, const void* port, uint64_t key) {
    canary_.Assert();
    ZX_DEBUG_ASSERT(is_waitable());

    StateObserver::Flags flags = CancelWithFunc(&observers_, get_lock(),
                                                [handle, port, key](StateObserver* obs) {
        return obs->OnCancelByKey(handle, port, key);
    });

    kcounter_add(dispatcher_cancel_bk_count, 1);

    return flags & StateObserver::kHandled;
}

// Since this conditionally takes the dispatcher's |lock_|, based on
// the type of Mutex (either fbl::Mutex or fbl::NullLock), the thread
// safety analysis is unable to prove that the accesses to |signals_|
// are always protected.
template <typename LockType>
void Dispatcher::UpdateStateHelper(zx_signals_t clear_mask,
                                   zx_signals_t set_mask,
                                   Lock<LockType>* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    Dispatcher::ObserverList obs_to_remove;
    {
        Guard<LockType> guard{lock};

        auto previous_signals = signals_;
        signals_ &= ~clear_mask;
        signals_ |= set_mask;

        if (previous_signals == signals_)
            return;

        UpdateInternalLocked(&obs_to_remove, signals_);
    }

    while (!obs_to_remove.is_empty()) {
        obs_to_remove.pop_front()->OnRemoved();
    }
}

void Dispatcher::UpdateState(zx_signals_t clear_mask,
                             zx_signals_t set_mask) {
    canary_.Assert();

    UpdateStateHelper(clear_mask, set_mask, get_lock());
}

void Dispatcher::UpdateStateLocked(zx_signals_t clear_mask,
                                   zx_signals_t set_mask) {
    canary_.Assert();

    // Type tag and local NullLock to make lockdep happy.
    struct DispatcherUpdateStateLocked {};
    DECLARE_LOCK(DispatcherUpdateStateLocked, fbl::NullLock) lock;
    UpdateStateHelper(clear_mask, set_mask, &lock);
}

void Dispatcher::UpdateInternalLocked(ObserverList* obs_to_remove, zx_signals_t signals) {
    canary_.Assert();
    ZX_DEBUG_ASSERT(is_waitable());

    for (auto it = observers_.begin(); it != observers_.end();) {
        StateObserver::Flags it_flags = it->OnStateChange(signals);
        if (it_flags & StateObserver::kNeedRemoval) {
            auto to_remove = it;
            ++it;
            obs_to_remove->push_back(observers_.erase(to_remove));
        } else {
            ++it;
        }
    }
}

zx_status_t Dispatcher::SetCookie(CookieJar* cookiejar, zx_koid_t scope, uint64_t cookie) {
    canary_.Assert();

    if (cookiejar == nullptr)
        return ZX_ERR_NOT_SUPPORTED;

    Guard<fbl::Mutex> guard{get_lock()};

    if (cookiejar->scope_ == ZX_KOID_INVALID) {
        cookiejar->scope_ = scope;
        cookiejar->cookie_ = cookie;

        kcounter_add(dispatcher_cookie_set_count, 1);
        return ZX_OK;
    }

    if (cookiejar->scope_ == scope) {
        cookiejar->cookie_ = cookie;

        kcounter_add(dispatcher_cookie_reset_count, 1);
        return ZX_OK;
    }

    return ZX_ERR_ACCESS_DENIED;
}

zx_status_t Dispatcher::GetCookie(CookieJar* cookiejar, zx_koid_t scope, uint64_t* cookie) {
    canary_.Assert();

    if (cookiejar == nullptr)
        return ZX_ERR_NOT_SUPPORTED;

    Guard<fbl::Mutex> guard{get_lock()};

    if (cookiejar->scope_ == scope) {
        *cookie = cookiejar->cookie_;
        return ZX_OK;
    }

    return ZX_ERR_ACCESS_DENIED;
}

zx_status_t Dispatcher::InvalidateCookieLocked(CookieJar* cookiejar) {
    canary_.Assert();

    if (cookiejar == nullptr)
        return ZX_ERR_NOT_SUPPORTED;

    cookiejar->scope_ = ZX_KOID_KERNEL;
    return ZX_OK;
}

zx_status_t Dispatcher::InvalidateCookie(CookieJar* cookiejar) {
    canary_.Assert();

    Guard<fbl::Mutex> guard{get_lock()};
    return InvalidateCookieLocked(cookiejar);
}

