// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <stdint.h>
#include <string.h>

#include <fbl/auto_lock.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/mutex.h>
#include <fbl/recycler.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_counted_upgradeable.h>
#include <fbl/ref_ptr.h>
#include <ktl/unique_ptr.h>
#include <ktl/move.h>

#include <kernel/lockdep.h>
#include <kernel/spinlock.h>
#include <object/handle.h>
#include <object/state_observer.h>

#include <zircon/compiler.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

struct CookieJar {
    zx_koid_t scope_ = ZX_KOID_INVALID;
    uint64_t cookie_ = 0u;
};

template <typename T> struct DispatchTag;
template <typename T> struct CanaryTag;

#define DECLARE_DISPTAG(T, E, M)                \
class T;                                        \
template <> struct DispatchTag<T> {             \
    static constexpr zx_obj_type_t ID = E;      \
};                                              \
template <> struct CanaryTag<T> {               \
    static constexpr uint32_t magic =           \
        fbl::magic(M);                          \
};

DECLARE_DISPTAG(ProcessDispatcher, ZX_OBJ_TYPE_PROCESS, "PROC")
DECLARE_DISPTAG(ThreadDispatcher, ZX_OBJ_TYPE_THREAD, "THRD")
DECLARE_DISPTAG(VmObjectDispatcher, ZX_OBJ_TYPE_VMO, "VMOD")
DECLARE_DISPTAG(ChannelDispatcher, ZX_OBJ_TYPE_CHANNEL, "CHAN")
DECLARE_DISPTAG(EventDispatcher, ZX_OBJ_TYPE_EVENT, "EVTD")
DECLARE_DISPTAG(PortDispatcher, ZX_OBJ_TYPE_PORT, "PORT")
DECLARE_DISPTAG(InterruptDispatcher, ZX_OBJ_TYPE_INTERRUPT, "INTD")
DECLARE_DISPTAG(PciDeviceDispatcher, ZX_OBJ_TYPE_PCI_DEVICE, "PCID")
DECLARE_DISPTAG(LogDispatcher, ZX_OBJ_TYPE_LOG, "LOGD")
DECLARE_DISPTAG(SocketDispatcher, ZX_OBJ_TYPE_SOCKET, "SOCK")
DECLARE_DISPTAG(ResourceDispatcher, ZX_OBJ_TYPE_RESOURCE, "RSRD")
DECLARE_DISPTAG(EventPairDispatcher, ZX_OBJ_TYPE_EVENTPAIR, "EPAI")
DECLARE_DISPTAG(JobDispatcher, ZX_OBJ_TYPE_JOB, "JOBD")
DECLARE_DISPTAG(VmAddressRegionDispatcher, ZX_OBJ_TYPE_VMAR, "VARD")
DECLARE_DISPTAG(FifoDispatcher, ZX_OBJ_TYPE_FIFO, "FIFO")
DECLARE_DISPTAG(GuestDispatcher, ZX_OBJ_TYPE_GUEST, "GSTD")
DECLARE_DISPTAG(VcpuDispatcher, ZX_OBJ_TYPE_VCPU, "VCPU")
DECLARE_DISPTAG(TimerDispatcher, ZX_OBJ_TYPE_TIMER, "TIMR")
DECLARE_DISPTAG(IommuDispatcher, ZX_OBJ_TYPE_IOMMU, "IOMM")
DECLARE_DISPTAG(BusTransactionInitiatorDispatcher, ZX_OBJ_TYPE_BTI, "BTID")
DECLARE_DISPTAG(ProfileDispatcher, ZX_OBJ_TYPE_PROFILE, "PROF")
DECLARE_DISPTAG(PinnedMemoryTokenDispatcher, ZX_OBJ_TYPE_PMT, "PIMT")
DECLARE_DISPTAG(SuspendTokenDispatcher, ZX_OBJ_TYPE_SUSPEND_TOKEN, "SUTD")
DECLARE_DISPTAG(PagerDispatcher, ZX_OBJ_TYPE_PAGER, "PGRD")

#undef DECLARE_DISPTAG

// Base class for all kernel objects that can be exposed to user-mode via
// the syscall API and referenced by handles.
//
// It implements RefCounted because handles are abstractions to a multiple
// references from user mode or kernel mode that control the lifetime o
// the object.
//
// It implements Recyclable because upon final Release() on the RefPtr
// it might be necessary to implement a destruction pattern that avoids
// deep recursion since the kernel stack is very limited.
//
// You don't derive directly from this class; instead derive
// from SoloDispatcher or PeeredDispatcher.
class Dispatcher : private fbl::RefCountedUpgradeable<Dispatcher>,
                   private fbl::Recyclable<Dispatcher> {
public:
    using fbl::RefCountedUpgradeable<Dispatcher>::AddRef;
    using fbl::RefCountedUpgradeable<Dispatcher>::Release;
    using fbl::RefCountedUpgradeable<Dispatcher>::Adopt;
    using fbl::RefCountedUpgradeable<Dispatcher>::AddRefMaybeInDestructor;

    // Dispatchers are either Solo or Peered. They handle refcounting
    // and locking differently.
    virtual ~Dispatcher();

    zx_koid_t get_koid() const { return koid_; }

    // Must be called under the handle table lock.
    void increment_handle_count() TA_REQ(Handle::ArenaLock::Get()) {
        ++handle_count_;
    }

    // Must be called under the handle table lock.
    // Returns true exactly when the handle count goes to zero.
    bool decrement_handle_count() TA_REQ(Handle::ArenaLock::Get()) {
        --handle_count_;
        return handle_count_ == 0u;
    }

    // Must be called under the handle table lock.
    uint32_t current_handle_count() const TA_REQ_SHARED(Handle::ArenaLock::Get()) {
        return handle_count_;
    }

    // The following are only to be called when |is_waitable| reports true.

    using ObserverList = fbl::DoublyLinkedList<StateObserver*, StateObserverListTraits>;

    // Add an observer.
    void AddObserver(StateObserver* observer, const StateObserver::CountInfo* cinfo);
    void AddObserverLocked(StateObserver* observer,
                           const StateObserver::CountInfo* cinfo) TA_REQ(get_lock());

    // Remove an observer (which must have been added).
    void RemoveObserver(StateObserver* observer);

    // Called when observers of the handle's state (e.g., waits on the handle) should be
    // "cancelled", i.e., when a handle (for the object that owns this StateTracker) is being
    // destroyed or transferred. Returns true if at least one observer was found.
    void Cancel(const Handle* handle);

    // Like Cancel() but issued via via zx_port_cancel().
    bool CancelByKey(const Handle* handle, const void* port, uint64_t key);

    // Dispatchers that support get/set cookie must provide
    // a CookieJar for those cookies to be stored in.
    virtual CookieJar* get_cookie_jar() { return nullptr; }

    // Accessors for CookieJars.
    zx_status_t SetCookie(CookieJar* cookiejar, zx_koid_t scope, uint64_t cookie);
    zx_status_t GetCookie(CookieJar* cookiejar, zx_koid_t scope, uint64_t* cookie);
    zx_status_t InvalidateCookie(CookieJar* cookiejar);
    zx_status_t InvalidateCookieLocked(CookieJar* cookiejar) TA_REQ(get_lock());

    // Interface for derived classes.

    virtual zx_obj_type_t get_type() const = 0;

    virtual zx_status_t add_observer(StateObserver* observer);

    virtual zx_status_t user_signal_self(uint32_t clear_mask, uint32_t set_mask) = 0;
    virtual zx_status_t user_signal_peer(uint32_t clear_mask, uint32_t set_mask) = 0;

    virtual void on_zero_handles() {}

    virtual zx_koid_t get_related_koid() const = 0;
    virtual bool is_waitable() const = 0;

    // get_name() will return a null-terminated name of ZX_MAX_NAME_LEN - 1 or fewer
    // characters.  For objects that don't have names it will be "".
    virtual void get_name(char out_name[ZX_MAX_NAME_LEN]) const __NONNULL((2)) {
        memset(out_name, 0, ZX_MAX_NAME_LEN);
    }

    // set_name() will truncate to ZX_MAX_NAME_LEN - 1 and ensure there is a
    // terminating null
    virtual zx_status_t set_name(const char* name, size_t len) { return ZX_ERR_NOT_SUPPORTED; }

    struct DeleterListTraits {
        static fbl::SinglyLinkedListNodeState<Dispatcher*>& node_state(
            Dispatcher& obj) {
            return obj.deleter_ll_;
        }
    };

    // Called whenever the object is bound to a new process. The |new_owner| is
    // the koid of the new process. It is only overridden for objects where a single
    // owner makes sense.
    virtual void set_owner(zx_koid_t new_owner) {}

protected:
    // At construction, the object's state tracker is asserting |signals|.
    explicit Dispatcher(zx_signals_t signals);

    // Notify others of a change in state (possibly waking them). (Clearing satisfied signals or
    // setting satisfiable signals should not wake anyone.)
    void UpdateState(zx_signals_t clear_mask, zx_signals_t set_mask);
    void UpdateStateLocked(zx_signals_t clear_mask, zx_signals_t set_mask) TA_REQ(get_lock());

    zx_signals_t GetSignalsStateLocked() const TA_REQ(get_lock()) {
        return signals_;
    }

    // Dispatcher subtypes should use this lock to protect their internal state.
    virtual Lock<fbl::Mutex>* get_lock() const = 0;

private:
    friend class fbl::Recyclable<Dispatcher>;
    void fbl_recycle();

    // The common implementation of UpdateState and UpdateStateLocked.
    template <typename LockType>
    void UpdateStateHelper(zx_signals_t clear_mask,
                           zx_signals_t set_mask,
                           Lock<LockType>* lock);

    // The common implementation of AddObserver and AddObserverLocked.
    template <typename LockType>
    void AddObserverHelper(StateObserver* observer,
                           const StateObserver::CountInfo* cinfo,
                           Lock<LockType>* lock);

    void UpdateInternalLocked(ObserverList* obs_to_remove,
                              zx_signals_t signals) TA_REQ(get_lock());

    fbl::Canary<fbl::magic("DISP")> canary_;

    const zx_koid_t koid_;
    uint32_t handle_count_ TA_GUARDED(Handle::ArenaLock::Get());

    zx_signals_t signals_ TA_GUARDED(get_lock());

    // Active observers are elements in |observers_|.
    ObserverList observers_ TA_GUARDED(get_lock());

    // Used to store this dispatcher on the dispatcher deleter list.
    fbl::SinglyLinkedListNodeState<Dispatcher*> deleter_ll_;
};

// SoloDispatchers stand alone. Since they have no peer to coordinate with, they
// directly contain their state lock. This is a CRTP template type to permit
// the lock validator to distinguish between locks in different subclasses of
// SoloDispatcher.
template <typename Self, zx_rights_t def_rights, zx_signals_t extra_signals = 0u>
class SoloDispatcher : public Dispatcher {
public:
    static constexpr zx_rights_t default_rights() { return def_rights; }

    // At construction, the object's state tracker is asserting
    // |signals|.
    explicit SoloDispatcher(zx_signals_t signals = 0u)
        : Dispatcher(signals) {}

    // Related koid is overridden by subclasses, like thread and process.
    zx_koid_t get_related_koid() const override TA_REQ(get_lock()) { return 0ULL; }
    bool is_waitable() const final { return default_rights() & ZX_RIGHT_WAIT; }

    zx_status_t user_signal_self(uint32_t clear_mask, uint32_t set_mask) final {
        if (!is_waitable())
            return ZX_ERR_NOT_SUPPORTED;
        // Generic objects can set all USER_SIGNALs. Particular object
        // types (events and eventpairs) may be able to set more.
        auto allowed_signals = ZX_USER_SIGNAL_ALL | extra_signals;
        if ((set_mask & ~allowed_signals) || (clear_mask & ~allowed_signals))
            return ZX_ERR_INVALID_ARGS;

        UpdateState(clear_mask, set_mask);
        return ZX_OK;
    }

    zx_status_t user_signal_peer(uint32_t clear_mask, uint32_t set_mask) final {
        return ZX_ERR_NOT_SUPPORTED;
    }

protected:
    Lock<fbl::Mutex>* get_lock() const final { return &lock_; }

    const fbl::Canary<CanaryTag<Self>::magic> canary_;
    mutable DECLARE_MUTEX(SoloDispatcher) lock_;
};

// PeeredDispatchers have opposing endpoints to coordinate state
// with. For example, writing into one endpoint of a Channel needs to
// modify zx_signals_t state (for the readability bit) on the opposite
// side. To coordinate their state, they share a mutex, which is held
// by the PeerHolder. Both endpoints have a RefPtr back to the
// PeerHolder; no one else ever does.

// Thus creating a pair of peered objects will typically look
// something like
//     // Make the two RefPtrs for each endpoint's handle to the mutex.
//     auto holder0 = AdoptRef(new PeerHolder<Foo>(...));
//     auto holder1 = peer_holder0;
//     // Create the opposing sides.
//     auto foo0 = AdoptRef(new Foo(std::move(holder0, ...));
//     auto foo1 = AdoptRef(new Foo(std::move(holder1, ...));
//     // Initialize the opposing sides, teaching them about each other.
//     foo0->Init(&foo1);
//     foo1->Init(&foo0);

// A PeeredDispatcher object, in its |on_zero_handles| call must clear
// out its peer's |peer_| field. This is needed to avoid leaks, and to
// ensure that |user_signal| can correctly report ZX_ERR_PEER_CLOSED.

// TODO(kulakowski) We should investigate turning this into one
// allocation. This would mean PeerHolder would have two EndPoint
// members, and that PeeredDispatcher would have custom refcounting.
template <typename Endpoint>
class PeerHolder : public fbl::RefCounted<PeerHolder<Endpoint>> {
public:
    PeerHolder() = default;
    ~PeerHolder() = default;

    Lock<fbl::Mutex>* get_lock() const { return &lock_; }

    mutable DECLARE_MUTEX(PeerHolder) lock_;
};

template <typename Self, zx_rights_t def_rights, zx_signals_t extra_signals = 0u>
class PeeredDispatcher : public Dispatcher {
public:
    static constexpr zx_rights_t default_rights() { return def_rights; }

    // At construction, the object's state tracker is asserting
    // |signals|.
    explicit PeeredDispatcher(fbl::RefPtr<PeerHolder<Self>> holder,
                              zx_signals_t signals = 0u)
        : Dispatcher(signals),
          holder_(ktl::move(holder)) {}
    virtual ~PeeredDispatcher() = default;

    zx_koid_t get_related_koid() const final TA_REQ(get_lock()) { return peer_koid_; }
    bool is_waitable() const final { return default_rights() & ZX_RIGHT_WAIT; }

    zx_status_t user_signal_self(uint32_t clear_mask, uint32_t set_mask) final
        TA_NO_THREAD_SAFETY_ANALYSIS {
        auto allowed_signals = ZX_USER_SIGNAL_ALL | extra_signals;
        if ((set_mask & ~allowed_signals) || (clear_mask & ~allowed_signals))
            return ZX_ERR_INVALID_ARGS;

        Guard<fbl::Mutex> guard{get_lock()};

        UpdateStateLocked(clear_mask, set_mask);
        return ZX_OK;
    }

    zx_status_t user_signal_peer(uint32_t clear_mask, uint32_t set_mask) final
        TA_NO_THREAD_SAFETY_ANALYSIS {
        auto allowed_signals = ZX_USER_SIGNAL_ALL | extra_signals;
        if ((set_mask & ~allowed_signals) || (clear_mask & ~allowed_signals))
            return ZX_ERR_INVALID_ARGS;

        Guard<fbl::Mutex> guard{get_lock()};
        // object_signal() may race with handle_close() on another thread.
        if (!peer_)
            return ZX_ERR_PEER_CLOSED;
        peer_->UpdateStateLocked(clear_mask, set_mask);
        return ZX_OK;
    }

    // All subclasses of PeeredDispatcher must implement a public
    // |void on_zero_handles_locked()|. The peer lifetime management
    // (i.e. the peer zeroing) is centralized here.
    void on_zero_handles() final TA_NO_THREAD_SAFETY_ANALYSIS {
        Guard<fbl::Mutex> guard{get_lock()};
        auto peer = ktl::move(peer_);
        static_cast<Self*>(this)->on_zero_handles_locked();

        // This is needed to avoid leaks, and to ensure that
        // |user_signal| can correctly report ZX_ERR_PEER_CLOSED.
        if (peer != nullptr) {
            // This defeats the lock analysis in the usual way: it
            // can't reason that the peers' get_lock() calls alias.
            peer->peer_.reset();
            static_cast<Self*>(peer.get())->OnPeerZeroHandlesLocked();
        }
    }

    Lock<fbl::Mutex>* get_lock() const final { return holder_->get_lock(); }

protected:
    const fbl::Canary<CanaryTag<Self>::magic> canary_;

    zx_koid_t peer_koid_ = 0u;
    fbl::RefPtr<Self> peer_ TA_GUARDED(get_lock());

private:
    const fbl::RefPtr<PeerHolder<Self>> holder_;
};

// DownCastDispatcher checks if a RefPtr<Dispatcher> points to a
// dispatcher of a given dispatcher subclass T and, if so, moves the
// reference to a RefPtr<T>, otherwise it leaves the
// RefPtr<Dispatcher> alone.  Must be called with a pointer to a valid
// (non-null) dispatcher.

// Note that the Dispatcher -> Dispatcher versions come up in generic
// code, and so aren't totally vacuous.

// Dispatcher -> FooDispatcher
template <typename T>
fbl::RefPtr<T> DownCastDispatcher(fbl::RefPtr<Dispatcher>* disp) {
    return (likely(DispatchTag<T>::ID == (*disp)->get_type())) ?
            fbl::RefPtr<T>::Downcast(ktl::move(*disp)) :
            nullptr;
}

// Dispatcher -> Dispatcher
template <>
inline fbl::RefPtr<Dispatcher> DownCastDispatcher(fbl::RefPtr<Dispatcher>* disp) {
    return ktl::move(*disp);
}

// const Dispatcher -> const FooDispatcher
template <typename T>
fbl::RefPtr<T> DownCastDispatcher(fbl::RefPtr<const Dispatcher>* disp) {
    static_assert(fbl::is_const<T>::value, "");
    return (likely(DispatchTag<typename fbl::remove_const<T>::type>::ID == (*disp)->get_type())) ?
            fbl::RefPtr<T>::Downcast(ktl::move(*disp)) :
            nullptr;
}

// const Dispatcher -> const Dispatcher
template <>
inline fbl::RefPtr<const Dispatcher> DownCastDispatcher(fbl::RefPtr<const Dispatcher>* disp) {
    return ktl::move(*disp);
}

// The same, but for Dispatcher* and FooDispatcher* instead of RefPtr.

// Dispatcher -> FooDispatcher
template <typename T>
T* DownCastDispatcher(Dispatcher* disp) {
    return (likely(DispatchTag<T>::ID == disp->get_type())) ?
        reinterpret_cast<T*>(disp) : nullptr;
}

// Dispatcher -> Dispatcher
template <>
inline Dispatcher* DownCastDispatcher(Dispatcher* disp) {
    return disp;
}

// const Dispatcher -> const FooDispatcher
template <typename T>
const T* DownCastDispatcher(const Dispatcher* disp) {
    static_assert(fbl::is_const<T>::value, "");
    return (likely(DispatchTag<typename fbl::remove_const<T>::type>::ID == disp->get_type())) ?
        reinterpret_cast<const T*>(disp) : nullptr;
}

// const Dispatcher -> const Dispatcher
template <>
inline const Dispatcher* DownCastDispatcher(const Dispatcher* disp) {
    return disp;
}
