// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "job_scheduler.h"

#include "magma_util/dlog.h"
#include "msd_arm_connection.h"
#include "msd_defs.h"
#include "platform_trace.h"

JobScheduler::JobScheduler(Owner* owner, uint32_t job_slots)
    : owner_(owner), job_slots_(job_slots), executing_atoms_(job_slots), runnable_atoms_(job_slots)
{
}

void JobScheduler::EnqueueAtom(std::shared_ptr<MsdArmAtom> atom)
{
    atoms_.push_back(std::move(atom));
}

// Use different names for different slots so they'll line up cleanly in the
// trace viewer.
[[maybe_unused]] static const char* AtomRunningString(uint32_t slot)
{
    switch (slot) {
        case 0:
            return "Atom running slot 0";
        case 1:
            return "Atom running slot 1";
        case 2:
            return "Atom running slot 2";
        default:
            DASSERT(false);
            return "Atom running unknown slot";
    }
}

void JobScheduler::MoveAtomsToRunnable()
{
    // Movement to next iterator happens inside loop.
    // Atoms can't depend on those after them, so one pass through the loop
    // should be enough.
    for (auto it = atoms_.begin(); it != atoms_.end();) {
        std::shared_ptr<MsdArmAtom> atom = *it;
        bool dependencies_finished;
        atom->UpdateDependencies(&dependencies_finished);
        if (dependencies_finished) {
            it = atoms_.erase(it);
            auto soft_atom = MsdArmSoftAtom::cast(atom);
            ArmMaliResultCode dep_status = atom->GetFinalDependencyResult();
            if (dep_status != kArmMaliResultSuccess) {
                owner_->AtomCompleted(atom.get(), dep_status);
            } else if (soft_atom) {
                soft_atom->SetExecutionStarted();
                ProcessSoftAtom(soft_atom);
            } else if (atom->IsDependencyOnly()) {
                owner_->AtomCompleted(atom.get(), kArmMaliResultSuccess);
            } else {
                DASSERT(atom->slot() < runnable_atoms_.size());
                runnable_atoms_[atom->slot()].push_back(atom);
            }
        } else {
            DLOG("Skipping atom %lx due to dependency", atom->gpu_address());
            ++it;
        }
    }
}

void JobScheduler::ValidateCanSwitchProtected()
{
    bool have_protected = false;
    bool have_nonprotected = false;
    for (uint32_t slot = 0; slot < runnable_atoms_.size(); slot++) {
        if (runnable_atoms_[slot].empty())
            continue;
        if (runnable_atoms_[slot].front()->is_protected()) {
            have_protected = true;
        } else {
            have_nonprotected = true;
        }
    }
    // If a switch was wanted but there's no actual atom of that type to run, then that could hang
    // execution of all other atoms.
    if (!have_protected)
        want_to_switch_to_protected_ = false;
    if (!have_nonprotected)
        want_to_switch_to_nonprotected_ = false;
}

static bool HigherPriorityThan(const MsdArmAtom* a, const MsdArmAtom* b)
{
    return a->connection().lock() == b->connection().lock() && a->priority() > b->priority();
}

void JobScheduler::ScheduleRunnableAtoms()
{
    // First try to preempt running atoms if necessary.
    for (uint32_t slot = 0; slot < runnable_atoms_.size(); slot++) {
        if (!executing_atoms_[slot]) {
            continue;
        }
        std::shared_ptr<MsdArmAtom> atom = executing_atoms_[slot];
        if (atom->is_protected()) {
            // We can't soft-stop protected-mode atoms because they can't
            // write out their progress to memory to be restarted.
            continue;
        }
        if (atom->soft_stopped()) {
            // No point trying to soft-stop an atom that's already stopping.
            continue;
        }
        auto& runnable = runnable_atoms_[slot];
        bool found_preempter = false;
        for (auto preempting = runnable.begin(); preempting != runnable.end(); ++preempting) {
            std::shared_ptr<MsdArmAtom> preempting_atom = *preempting;
            if (HigherPriorityThan(preempting_atom.get(), atom.get())) {
                found_preempter = true;
                break;
            }
        }
        if (found_preempter) {
            atom->set_soft_stopped(true);
            // If the atom's soft-stopped its current state will be saved in the job chain so it
            // will restart at the place it left off. When JobCompleted is received it will be
            // requeued so it can run again, priority permitting.
            owner_->SoftStopAtom(atom.get());
        }
    }
    // Start executing on empty slots.
    for (uint32_t slot = 0; slot < runnable_atoms_.size(); slot++) {
        if (executing_atoms_[slot]) {
            continue;
        }
        auto& runnable = runnable_atoms_[slot];
        if (runnable.empty())
            continue;
        std::shared_ptr<MsdArmAtom> atom = runnable.front();
        DASSERT(!MsdArmSoftAtom::cast(atom));
        DASSERT(atom->GetFinalDependencyResult() == kArmMaliResultSuccess);
        DASSERT(!atom->IsDependencyOnly());
        DASSERT(atom->slot() == slot);

        for (auto preempting = std::next(runnable.begin()); preempting != runnable.end();
             ++preempting) {
            std::shared_ptr<MsdArmAtom> preempting_atom = *preempting;
            if (HigherPriorityThan(preempting_atom.get(), atom.get())) {
                // Swap the lower priority atom to the current location so we
                // don't change the ratio of atoms executed between connections.
                std::swap(atom, *preempting);
                // It's possible a protected atom was preempted for a nonprotected atom, or vice
                // versa.
                ValidateCanSwitchProtected();
                // Keep looping, as there may be an even higher priority atom.
            }
        }
        DASSERT(atom->slot() == slot);
        bool new_atom_protected = atom->is_protected();
        bool currently_protected = owner_->IsInProtectedMode();
        bool want_switch = false;

        if (new_atom_protected != currently_protected) {
            want_switch = true;
            if (new_atom_protected) {
                DASSERT(!want_to_switch_to_nonprotected_);
                want_to_switch_to_protected_ = true;
            } else {
                DASSERT(!want_to_switch_to_protected_);
                want_to_switch_to_nonprotected_ = true;
            }
        }

        // Don't execute more atoms in the wrong mode if a switch is pending,
        // because executing more atoms will delay the time until we can switch.
        // TODO(MA-524): Allow longer between context switches. Ensure fairness between
        // slots.
        if ((want_to_switch_to_protected_ && !new_atom_protected) ||
            (want_to_switch_to_nonprotected_ && new_atom_protected)) {
            continue;
        }

        if (want_switch) {
            if (num_executing_atoms() > 0) {
                // Wait for switch until there are no executing atoms.
                continue;
            }
            if (new_atom_protected) {
                owner_->EnterProtectedMode();
                want_to_switch_to_protected_ = false;
            } else {
                if (!owner_->ExitProtectedMode())
                    return;
                want_to_switch_to_nonprotected_ = false;
            }
        }

        atom->SetExecutionStarted();
        atom->SetTickStarted();
        DASSERT(!atom->preempted());
        DASSERT(!atom->soft_stopped());
        executing_atoms_[slot] = atom;
        runnable.erase(runnable.begin());
        std::shared_ptr<MsdArmConnection> connection = atom->connection().lock();
        [[maybe_unused]] msd_client_id_t id =
            connection ? connection->client_id() : 0;
        TRACE_ASYNC_BEGIN("magma", AtomRunningString(slot), executing_atoms_[slot]->trace_nonce(),
                          "id", id);
        owner_->RunAtom(executing_atoms_[slot].get());
    }
}

void JobScheduler::TryToSchedule()
{
    MoveAtomsToRunnable();
    ScheduleRunnableAtoms();

    UpdatePowerManager();
}

void JobScheduler::CancelAtomsForConnection(std::shared_ptr<MsdArmConnection> connection)
{
    auto removal_function = [connection](auto it) {
        auto locked = it->connection().lock();
        return !locked || locked == connection;
    };
    waiting_atoms_.erase(
        std::remove_if(waiting_atoms_.begin(), waiting_atoms_.end(), removal_function),
        waiting_atoms_.end());

    atoms_.remove_if(removal_function);
    for (auto& runnable_list : runnable_atoms_)
        runnable_list.remove_if(removal_function);

    ValidateCanSwitchProtected();
}

void JobScheduler::JobCompleted(uint64_t slot, ArmMaliResultCode result_code, uint64_t tail)
{
    std::shared_ptr<MsdArmAtom>& atom = executing_atoms_[slot];
    DASSERT(atom);
    TRACE_ASYNC_END("magma", AtomRunningString(slot), atom->trace_nonce());
    if (result_code == kArmMaliResultSoftStopped) {
        atom->set_soft_stopped(false);
        // The tail is the first job executed that didn't complete. When continuing execution, skip
        // jobs before that in the job chain, or else kArmMaliResultDataInvalidFault is generated.
        atom->set_gpu_address(tail);
        if (atom->preempted()) {
            atom->set_preempted(false);
            runnable_atoms_[slot].push_back(atom);
        } else {
            runnable_atoms_[slot].push_front(atom);
        }
    }
    owner_->AtomCompleted(atom.get(), result_code);
    atom.reset();
    TryToSchedule();
}

void JobScheduler::SoftJobCompleted(std::shared_ptr<MsdArmSoftAtom> atom)
{
    owner_->AtomCompleted(atom.get(), kArmMaliResultSuccess);
    // The loop in TryToSchedule should cause any atoms that just had their
    // dependencies satisfied to run.
}

void JobScheduler::PlatformPortSignaled(uint64_t key)
{
    std::vector<std::shared_ptr<MsdArmSoftAtom>> unfinished_atoms;
    bool completed_atom = false;
    for (auto& atom : waiting_atoms_) {
        bool wait_succeeded;
        if (atom->soft_flags() == kAtomFlagSemaphoreWait) {
            wait_succeeded = atom->platform_semaphore()->WaitNoReset(0).ok();
        } else {
            DASSERT(atom->soft_flags() == kAtomFlagSemaphoreWaitAndReset);
            wait_succeeded = atom->platform_semaphore()->Wait(0).ok();
        }

        if (wait_succeeded) {
            completed_atom = true;
            owner_->AtomCompleted(atom.get(), kArmMaliResultSuccess);
        } else {
            if (atom->platform_semaphore()->id() == key)
                atom->platform_semaphore()->WaitAsync(owner_->GetPlatformPort());
            unfinished_atoms.push_back(atom);
        }
    }
    if (completed_atom) {
        waiting_atoms_ = unfinished_atoms;
        TryToSchedule();
    }
}

size_t JobScheduler::GetAtomListSize() { return atoms_.size(); }

JobScheduler::Clock::duration JobScheduler::GetCurrentTimeoutDuration()
{
    auto timeout_time = Clock::time_point::max();
    for (auto& atom : executing_atoms_) {
        if (!atom || atom->hard_stopped())
            continue;

        auto atom_timeout_time =
            atom->execution_start_time() + std::chrono::milliseconds(timeout_duration_ms_);

        if (atom_timeout_time < timeout_time)
            timeout_time = atom_timeout_time;

        bool may_want_to_preempt = !atom->is_protected() && !atom->soft_stopped() &&
                                   !runnable_atoms_[atom->slot()].empty();

        if (may_want_to_preempt) {
            auto tick_timeout =
                atom->tick_start_time() + std::chrono::milliseconds(job_tick_duration_ms_);
            if (tick_timeout < timeout_time) {
                timeout_time = tick_timeout;
            }
        }
    }

    for (auto& atom : waiting_atoms_) {
        auto atom_timeout_time = atom->execution_start_time() +
                                 std::chrono::milliseconds(semaphore_timeout_duration_ms_);
        if (atom_timeout_time < timeout_time)
            timeout_time = atom_timeout_time;
    }

    if (timeout_time == Clock::time_point::max())
        return Clock::duration::max();
    return timeout_time - Clock::now();
}

void JobScheduler::HandleTimedOutAtoms()
{
    bool have_output_hang_message = false;
    auto now = Clock::now();
    for (auto& atom : executing_atoms_) {
        if (!atom || atom->hard_stopped())
            continue;
        if (atom->execution_start_time() + std::chrono::milliseconds(timeout_duration_ms_) <= now) {
            if (!have_output_hang_message) {
                have_output_hang_message = true;
                owner_->OutputHangMessage();
            }

            atom->set_hard_stopped();
            owner_->HardStopAtom(atom.get());
        } else if (atom->tick_start_time() + std::chrono::milliseconds(job_tick_duration_ms_) <=
                   now) {
            // Reset tick time so we won't spin trying to stop this atom.
            atom->SetTickStarted();

            if (atom->soft_stopped() || atom->is_protected())
                continue;
            DASSERT(!atom->preempted());
            bool want_to_preempt = false;
            // Only preempt if there's another atom of equal or higher priority that could run.
            for (auto& waiting_atom : runnable_atoms_[atom->slot()]) {
                if (!HigherPriorityThan(atom.get(), waiting_atom.get())) {
                    want_to_preempt = true;
                    break;
                }
            }
            if (want_to_preempt) {
                DLOG("Preempting atom gpu addr: %lx\n", atom->gpu_address());
                atom->set_soft_stopped(true);
                atom->set_preempted(true);
                // If the atom's soft-stopped its current state will be saved in the job chain
                // so it will restart at the place it left off. When JobCompleted is received it
                // will be requeued so it can run again, priority permitting.
                owner_->SoftStopAtom(atom.get());
            }
        }
    }
    bool removed_waiting_atoms = false;
    for (auto it = waiting_atoms_.begin(); it != waiting_atoms_.end();) {
        std::shared_ptr<MsdArmAtom> atom = *it;
        auto atom_timeout_time = atom->execution_start_time() +
                                 std::chrono::milliseconds(semaphore_timeout_duration_ms_);
        if (atom_timeout_time <= now) {
            magma::log(magma::LOG_WARNING, "Timing out hung semaphore");
            removed_waiting_atoms = true;
            owner_->AtomCompleted(atom.get(), kArmMaliResultTimedOut);
            // The semaphore wait on the port will be canceled by the closing of the event handle.
            it = waiting_atoms_.erase(it);
        } else {
            ++it;
        }
    }
    if (removed_waiting_atoms)
        TryToSchedule();
}

void JobScheduler::ProcessSoftAtom(std::shared_ptr<MsdArmSoftAtom> atom)
{
    DASSERT(owner_->GetPlatformPort());
    if (atom->soft_flags() == kAtomFlagSemaphoreSet) {
        atom->platform_semaphore()->Signal();
        SoftJobCompleted(atom);
    } else if (atom->soft_flags() == kAtomFlagSemaphoreReset) {
        atom->platform_semaphore()->Reset();
        SoftJobCompleted(atom);
    } else if ((atom->soft_flags() == kAtomFlagSemaphoreWait) ||
               (atom->soft_flags() == kAtomFlagSemaphoreWaitAndReset)) {
        bool wait_succeeded;
        if (atom->soft_flags() == kAtomFlagSemaphoreWait) {
            wait_succeeded = atom->platform_semaphore()->WaitNoReset(0).ok();
        } else {
            wait_succeeded = atom->platform_semaphore()->Wait(0).ok();
        }

        if (wait_succeeded) {
            SoftJobCompleted(atom);
        } else {
            waiting_atoms_.push_back(atom);
            atom->platform_semaphore()->WaitAsync(owner_->GetPlatformPort());
        }
    } else {
        DASSERT(false);
    }
}

void JobScheduler::ReleaseMappingsForConnection(std::shared_ptr<MsdArmConnection> connection)
{

    for (auto& executing_atom : executing_atoms_) {
        if (executing_atom && executing_atom->connection().lock() == connection) {
            executing_atom->set_hard_stopped();
            owner_->ReleaseMappingsForAtom(executing_atom.get());
        }
    }
}

void JobScheduler::UpdatePowerManager()
{
    bool active = false;
    for (std::shared_ptr<MsdArmAtom>& slot : executing_atoms_) {
        if (slot)
            active = true;
    }
    owner_->UpdateGpuActive(active);
}
