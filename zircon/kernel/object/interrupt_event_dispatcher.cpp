// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/interrupt_event_dispatcher.h>

#include <dev/interrupt.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <kernel/auto_lock.h>
#include <lib/counters.h>
#include <platform.h>
#include <zircon/rights.h>

KCOUNTER(dispatcher_interrupt_event_create_count, "dispatcher.interrupt_event.create");
KCOUNTER(dispatcher_interrupt_event_destroy_count, "dispatcher.interrupt_event.destroy");

zx_status_t InterruptEventDispatcher::Create(fbl::RefPtr<Dispatcher>* dispatcher,
                                             zx_rights_t* rights,
                                             uint32_t vector,
                                             uint32_t options) {

    if (options & ZX_INTERRUPT_VIRTUAL)
        return ZX_ERR_INVALID_ARGS;

    // Attempt to construct the dispatcher.
    fbl::AllocChecker ac;
    InterruptEventDispatcher* disp = new (&ac) InterruptEventDispatcher(vector);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    // Hold a ref while we check to see if someone else owns this vector or not.
    // If things go wrong, this ref will be released and the IED will get
    // cleaned up automatically.
    auto disp_ref = fbl::AdoptRef<Dispatcher>(disp);

    Guard<fbl::Mutex> guard{disp->get_lock()};

    uint32_t interrupt_flags = 0;

    if (options & ~(ZX_INTERRUPT_REMAP_IRQ | ZX_INTERRUPT_MODE_MASK))
        return ZX_ERR_INVALID_ARGS;

    // Remap the vector if we have been asked to do so.
    if (options & ZX_INTERRUPT_REMAP_IRQ)
        vector = remap_interrupt(vector);

    if (!is_valid_interrupt(vector, 0))
        return ZX_ERR_INVALID_ARGS;

    bool default_mode = false;
    enum interrupt_trigger_mode tm = IRQ_TRIGGER_MODE_EDGE;
    enum interrupt_polarity pol = IRQ_POLARITY_ACTIVE_LOW;
    switch (options & ZX_INTERRUPT_MODE_MASK) {
    case ZX_INTERRUPT_MODE_DEFAULT:
        default_mode = true;
        break;
    case ZX_INTERRUPT_MODE_EDGE_LOW:
        tm = IRQ_TRIGGER_MODE_EDGE;
        pol = IRQ_POLARITY_ACTIVE_LOW;
        break;
    case ZX_INTERRUPT_MODE_EDGE_HIGH:
        tm = IRQ_TRIGGER_MODE_EDGE;
        pol = IRQ_POLARITY_ACTIVE_HIGH;
        break;
    case ZX_INTERRUPT_MODE_LEVEL_LOW:
        tm = IRQ_TRIGGER_MODE_LEVEL;
        pol = IRQ_POLARITY_ACTIVE_LOW;
        interrupt_flags = INTERRUPT_UNMASK_PREWAIT | INTERRUPT_MASK_POSTWAIT;
        break;
    case ZX_INTERRUPT_MODE_LEVEL_HIGH:
        tm = IRQ_TRIGGER_MODE_LEVEL;
        pol = IRQ_POLARITY_ACTIVE_HIGH;
        interrupt_flags = INTERRUPT_UNMASK_PREWAIT | INTERRUPT_MASK_POSTWAIT;
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    if (!default_mode) {
        zx_status_t status = configure_interrupt(vector, tm, pol);
        if (status != ZX_OK)
            return status;
    }

    disp->set_flags(interrupt_flags);

    // Register the interrupt
    zx_status_t status = disp->RegisterInterruptHandler();
    if (status != ZX_OK)
        return status;

    unmask_interrupt(vector);

    // Transfer control of the new dispatcher to the creator and we are done.
    *rights = default_rights();
    *dispatcher = ktl::move(disp_ref);

    return ZX_OK;
}

zx_status_t InterruptEventDispatcher::BindVcpu(fbl::RefPtr<VcpuDispatcher> vcpu_dispatcher) {
    Guard<SpinLock, IrqSave> guard{&spinlock_};
    if (state() == InterruptState::DESTROYED) {
        return ZX_ERR_CANCELED;
    } else if (state() == InterruptState::WAITING) {
        return ZX_ERR_BAD_STATE;
    } else if (HasPort()) {
        return ZX_ERR_ALREADY_BOUND;
    }

    for (const auto& vcpu : vcpus_) {
        if (vcpu == vcpu_dispatcher) {
            return ZX_OK;
        } else if (vcpu->guest() != vcpu_dispatcher->guest()) {
            return ZX_ERR_INVALID_ARGS;
        }
    }

    fbl::AllocChecker ac;
    vcpus_.push_back(ktl::move(vcpu_dispatcher), &ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    if (vcpus_.size() == 1) {
        MaskInterrupt();
        UnregisterInterruptHandler();
        zx_status_t status = register_int_handler(vector_, VcpuIrqHandler, this);
        if (status != ZX_OK) {
            return status;
        }
        UnmaskInterrupt();
    }
    return ZX_OK;
}

interrupt_eoi InterruptEventDispatcher::IrqHandler(void* ctx) {
    InterruptEventDispatcher* self = reinterpret_cast<InterruptEventDispatcher*>(ctx);

    if (self->get_flags() & INTERRUPT_MASK_POSTWAIT)
        mask_interrupt(self->vector_);

    self->InterruptHandler();
    return IRQ_EOI_DEACTIVATE;
}

interrupt_eoi InterruptEventDispatcher::VcpuIrqHandler(void* ctx) {
    InterruptEventDispatcher* self = reinterpret_cast<InterruptEventDispatcher*>(ctx);
    self->VcpuInterruptHandler();
    // Skip the EOI to allow the guest to deactivate the interrupt.
    return IRQ_EOI_PRIORITY_DROP;
}

void InterruptEventDispatcher::VcpuInterruptHandler() {
    Guard<SpinLock, IrqSave> guard{&spinlock_};
    cpu_mask_t mask = 0;
    for (const auto& vcpu : vcpus_) {
        mask |= vcpu->PhysicalInterrupt(vector_);
    }
    if (mask != 0) {
        mp_interrupt(MP_IPI_TARGET_MASK, mask);
    }
}

InterruptEventDispatcher::InterruptEventDispatcher(uint32_t vector)
    : vector_(vector) {
    kcounter_add(dispatcher_interrupt_event_create_count, 1);
}

InterruptEventDispatcher::~InterruptEventDispatcher() {
    kcounter_add(dispatcher_interrupt_event_destroy_count, 1);
}

void InterruptEventDispatcher::MaskInterrupt() {
    mask_interrupt(vector_);
}

void InterruptEventDispatcher::UnmaskInterrupt() {
    unmask_interrupt(vector_);
}

zx_status_t InterruptEventDispatcher::RegisterInterruptHandler() {
    return register_int_handler(vector_, IrqHandler, this);
}

void InterruptEventDispatcher::UnregisterInterruptHandler() {
    register_int_handler(vector_, nullptr, nullptr);
}

bool InterruptEventDispatcher::HasVcpu() const {
    return !vcpus_.is_empty();
}
