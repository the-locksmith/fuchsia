// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thread.h"

#include <cinttypes>
#include <string>

#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/lib/debugger_utils/util.h"

#include "arch.h"
#include "process.h"
#include "server.h"

namespace inferior_control {

// static
const char* Thread::StateName(Thread::State state) {
#define CASE_TO_STR(x)   \
  case Thread::State::x: \
    return #x
  switch (state) {
    CASE_TO_STR(kNew);
    CASE_TO_STR(kInException);
    CASE_TO_STR(kRunning);
    CASE_TO_STR(kStepping);
    CASE_TO_STR(kExiting);
    CASE_TO_STR(kGone);
    default:
      break;
  }
#undef CASE_TO_STR
  return "(unknown)";
}

Thread::Thread(Process* process, zx_handle_t handle, zx_koid_t id)
    : process_(process),
      handle_(handle),
      id_(id),
      state_(State::kNew),
      breakpoints_(this),
      weak_ptr_factory_(this) {
  FXL_DCHECK(process_);
  FXL_DCHECK(handle_ != ZX_HANDLE_INVALID);
  FXL_DCHECK(id_ != ZX_KOID_INVALID);

  name_ = debugger_utils::GetObjectName(handle_);

  registers_ = Registers::Create(this);
  FXL_DCHECK(registers_.get());
}

Thread::~Thread() {
  FXL_VLOG(2) << "Destructing thread " << GetDebugName();

  Clear();
}

std::string Thread::GetName() const {
  if (name_.empty()) {
    return fxl::StringPrintf("%lu.%lu", process_->id(), id_);
  }
  return fxl::StringPrintf("%lu.%lu(%s)",
                           process_->id(), id_, name_.c_str());
}

std::string Thread::GetDebugName() const {
  if (name_.empty()) {
    return fxl::StringPrintf("%lu.%lu(%lx.%lx)",
                             process_->id(), id_, process_->id(), id_);
  }
  return fxl::StringPrintf("%lu.%lu(%lx.%lx)(%s)",
                           process_->id(), id_, process_->id(), id_,
                           name_.c_str());
}

void Thread::set_state(State state) {
  FXL_DCHECK(state != State::kNew);
  state_ = state;
}

bool Thread::IsLive() const {
  switch (state_) {
    case State::kNew:
    case State::kInException:
    case State::kRunning:
    case State::kStepping:
      return true;
    default:
      return false;
  }
}

void Thread::Clear() {
  // We close the handle here so the o/s will release the thread.
  if (handle_ != ZX_HANDLE_INVALID)
    zx_handle_close(handle_);
  handle_ = ZX_HANDLE_INVALID;
}

zx_handle_t Thread::GetExceptionPortHandle() {
  return process_->server()->exception_port().handle();
}

fxl::WeakPtr<Thread> Thread::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

GdbSignal Thread::GetGdbSignal() const {
  if (!exception_context_) {
    // TODO(dje): kNone may be a better value to return here.
    return GdbSignal::kUnsupported;
  }

  return ComputeGdbSignal(*exception_context_);
}

void Thread::OnException(const zx_excp_type_t type,
                         const zx_exception_context_t& context) {
  // TODO(dje): While having a pointer allows for a simple "do we have a
  // context" check, it might be simpler to just store the struct in the class.
  exception_context_.reset(new zx_exception_context_t);
  *exception_context_ = context;

  State prev_state = state_;
  set_state(State::kInException);

  // If we were singlestepping turn it off.
  // If the user wants to try the singlestep again it must be re-requested.
  // If the thread has exited we may not be able to, and there's no point
  // anyway.
  if (prev_state == State::kStepping && type != ZX_EXCP_THREAD_EXITING) {
    FXL_DCHECK(breakpoints_.SingleStepBreakpointInserted());
    if (!breakpoints_.RemoveSingleStepBreakpoint()) {
      FXL_LOG(ERROR) << "Unable to clear single-step bkpt for thread "
                     << GetName();
    } else {
      FXL_VLOG(2) << "Single-step bkpt cleared for thread "
                  << GetDebugName();
    }
  }

  FXL_VLOG(1) << ExceptionToString(type, context);
}

bool Thread::TryNext() {
  if (state() != State::kInException && state() != State::kNew) {
    FXL_LOG(ERROR) << "Cannot try-next thread " << GetName()
                   << " while in state " << StateName(state());
    return false;
  }

  FXL_VLOG(2) << "Thread " << GetDebugName()
              << ": trying next exception handler";

  zx_status_t status =
      zx_task_resume_from_exception(handle_, GetExceptionPortHandle(),
                                    ZX_RESUME_TRY_NEXT);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to try-next thread "
                   << GetName() << ": "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  return true;
}

bool Thread::ResumeFromException() {
  if (state() != State::kInException && state() != State::kNew) {
    FXL_LOG(ERROR) << "Cannot resume thread " << GetName()
                   << " while in state: " << StateName(state());
    return false;
  }

  // This is printed here before resuming the task so that this is always
  // printed before any subsequent exception report (which is read by another
  // thread).
  FXL_VLOG(2) << "Resuming thread " << GetDebugName() << " after an exception";

  zx_status_t status =
      zx_task_resume_from_exception(handle_, GetExceptionPortHandle(), 0);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to resume thread " << GetName() << ": "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  state_ = State::kRunning;
  return true;
}

void Thread::ResumeForExit() {
  switch (state()) {
    case State::kNew:
    case State::kInException:
    case State::kExiting:
      break;
    default:
      FXL_DCHECK(false) << "unexpected state " << StateName(state());
      break;
  }

  FXL_VLOG(2) << "Thread " << GetDebugName() << " is exiting";

  auto status =
      zx_task_resume_from_exception(handle_, GetExceptionPortHandle(), 0);
  if (status < 0) {
    // This might fail if the process has been killed in the interim.
    // It shouldn't otherwise fail. Just log the failure, nothing else
    // we can do.
    zx_info_process_t info;
    auto info_status =
        zx_object_get_info(process()->handle(), ZX_INFO_PROCESS, &info,
                           sizeof(info), nullptr, nullptr);
    if (info_status != ZX_OK) {
      FXL_LOG(ERROR) << "Error getting process info for thread "
                     << GetName() << ": "
                     << debugger_utils::ZxErrorString(info_status);
    }
    if (info_status == ZX_OK && info.exited) {
      FXL_VLOG(2) << "Process " << process()->GetName() << " exited too";
    } else {
      FXL_LOG(ERROR) << "Failed to resume thread " << GetName()
                     << " for exit: "
                     << debugger_utils::ZxErrorString(status);
    }
  }

  set_state(State::kGone);
  Clear();
}

bool Thread::Step() {
  if (state() != State::kInException) {
    FXL_LOG(ERROR) << "Cannot step thread " << GetName() << " while in state: "
                   << StateName(state());
    return false;
  }

  if (!registers_->RefreshGeneralRegisters()) {
    FXL_LOG(ERROR) << "Failed refreshing gregs for thread " << GetName();
    return false;
  }
  zx_vaddr_t pc = registers_->GetPC();

  if (!breakpoints_.InsertSingleStepBreakpoint(pc))
    return false;

  // This is printed here before resuming the task so that this is always
  // printed before any subsequent exception report (which is read by another
  // thread).
  FXL_LOG(INFO) << "Thread " << GetName() << " is now stepping";

  zx_status_t status =
      zx_task_resume_from_exception(handle_, GetExceptionPortHandle(), 0);
  if (status < 0) {
    breakpoints_.RemoveSingleStepBreakpoint();
    FXL_LOG(ERROR) << "Failed to resume thread " << GetName() << " for step: "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  state_ = State::kStepping;
  return true;
}

zx_status_t Thread::GetExceptionReport(zx_exception_report_t* report) const {
  zx_status_t status =
    zx_object_get_info(handle_, ZX_INFO_THREAD_EXCEPTION_REPORT,
                       report, sizeof(*report), nullptr, nullptr);
  // This could fail if the process terminates before we get a chance to
  // look at it.
  if (status == ZX_ERR_BAD_STATE) {
    FXL_LOG(WARNING) << "No exception report for thread " << id_;
  }

  return status;
}

std::string Thread::ExceptionToString(
    zx_excp_type_t type, const zx_exception_context_t& context) const {
  std::string description = fxl::StringPrintf(
    "Thread %s: received exception %s",
    GetDebugName().c_str(),
    debugger_utils::ExceptionNameAsString(type).c_str());

  if (ZX_EXCP_IS_ARCH(type)) {
    Registers* regs = registers();
    if (regs->RefreshGeneralRegisters()) {
      zx_vaddr_t pc = regs->GetPC();
      description += fxl::StringPrintf(", @PC 0x%lx", pc);
    }
  }

  return description;
}

std::string Thread::SignalsToString(zx_signals_t signals) const {
  std::string description;
  if (signals & ZX_THREAD_RUNNING)
    description += ", running";
  if (signals & ZX_THREAD_SUSPENDED)
    description += ", suspended";
  if (signals & ZX_THREAD_TERMINATED)
    description += ", terminated";
  zx_signals_t mask =
      (ZX_THREAD_RUNNING | ZX_THREAD_SUSPENDED | ZX_THREAD_TERMINATED);
  if (signals & ~mask)
    description += fxl::StringPrintf(", unknown (0x%x)", signals & ~mask);
  if (description.length() == 0)
    description = ", none";
  return fxl::StringPrintf("Thread %s got signals: %s",
                           GetDebugName().c_str(), description.c_str() + 2);
}

}  // namespace inferior_control
