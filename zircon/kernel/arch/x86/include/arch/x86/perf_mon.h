// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <stdint.h>

#include <fbl/ref_ptr.h>
#include <vm/vm_object.h>

#include <arch/x86.h>

#include <lib/zircon-internal/device/cpu-trace/intel-pm.h>

// TODO(dje): Transition helpers.
using zx_x86_pmu_properties_t = perfmon::X86PmuProperties;
using zx_x86_pmu_config_t = perfmon::X86PmuConfig;

zx_status_t arch_perfmon_get_properties(zx_x86_pmu_properties_t* state);

zx_status_t arch_perfmon_init();

zx_status_t arch_perfmon_assign_buffer(uint32_t cpu, fbl::RefPtr<VmObject> vmo);

zx_status_t arch_perfmon_stage_config(zx_x86_pmu_config_t* config);

zx_status_t arch_perfmon_start();

zx_status_t arch_perfmon_stop();

zx_status_t arch_perfmon_fini();

void apic_pmi_interrupt_handler(x86_iframe_t *frame);
