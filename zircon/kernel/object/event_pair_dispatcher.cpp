// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/event_pair_dispatcher.h>

#include <assert.h>
#include <err.h>

#include <fbl/alloc_checker.h>
#include <lib/counters.h>
#include <zircon/rights.h>

KCOUNTER(dispatcher_eventpair_create_count, "dispatcher.eventpair.create");
KCOUNTER(dispatcher_eventpair_destroy_count, "dispatcher.eventpair.destroy");

zx_status_t EventPairDispatcher::Create(fbl::RefPtr<Dispatcher>* dispatcher0,
                                        fbl::RefPtr<Dispatcher>* dispatcher1,
                                        zx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto holder0 = fbl::AdoptRef(new (&ac) PeerHolder<EventPairDispatcher>());
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;
    auto holder1 = holder0;

    auto disp0 = fbl::AdoptRef(new (&ac) EventPairDispatcher(ktl::move(holder0)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    auto disp1 = fbl::AdoptRef(new (&ac) EventPairDispatcher(ktl::move(holder1)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    disp0->Init(disp1);
    disp1->Init(disp0);

    *rights = default_rights();
    *dispatcher0 = ktl::move(disp0);
    *dispatcher1 = ktl::move(disp1);

    return ZX_OK;
}

EventPairDispatcher::~EventPairDispatcher() {
    kcounter_add(dispatcher_eventpair_destroy_count, 1);
}

void EventPairDispatcher::on_zero_handles_locked() {
    canary_.Assert();
}

void EventPairDispatcher::OnPeerZeroHandlesLocked() {
    InvalidateCookieLocked(get_cookie_jar());
    UpdateStateLocked(0u, ZX_EVENTPAIR_PEER_CLOSED);
}

EventPairDispatcher::EventPairDispatcher(fbl::RefPtr<PeerHolder<EventPairDispatcher>> holder)
    : PeeredDispatcher(ktl::move(holder)) {
    kcounter_add(dispatcher_eventpair_create_count, 1);
}

// This is called before either EventPairDispatcher is accessible from threads other than the one
// initializing the event pair, so it does not need locking.
void EventPairDispatcher::Init(fbl::RefPtr<EventPairDispatcher> other) TA_NO_THREAD_SAFETY_ANALYSIS {
    DEBUG_ASSERT(other);
    // No need to take |lock_| here.
    DEBUG_ASSERT(!peer_);
    peer_koid_ = other->get_koid();
    peer_ = ktl::move(other);
}
