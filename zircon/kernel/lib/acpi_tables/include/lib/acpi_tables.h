// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef KERNEL_LIB_APIC_TABLES_H
#define KERNEL_LIB_APIC_TABLES_H

#include <acpica/acpi.h>
#include <arch/x86/apic.h>
#include <zircon/types.h>

// TODO(edcoyne): rename this to C++ naming.
struct acpi_hpet_descriptor {
    uint64_t address;
    bool port_io;

    uint16_t minimum_tick;
    uint8_t sequence;
};

struct AcpiNumaRegion {
    bool initialized = false;
    uint32_t domain;
    uint64_t base_address;
    uint64_t length;
};

// Wraps ACPICA functions (except init) to allow testing.
class AcpiTableProvider {
public:
    virtual ~AcpiTableProvider() {}

    // Looks up table, on success sets header to point to table. Maintains
    // ownership of the table's memory.
    virtual ACPI_STATUS GetTable(char* signature, uint32_t instance,
                                 ACPI_TABLE_HEADER** header) const {
        return AcpiGetTable(signature, instance, header);
    }
};

// Designed to read and parse APIC tables, other functions of the APIC
// subsystem are out of scope of this class. This class can work before dynamic memory
// allocation is available.
class AcpiTables {
public:
    AcpiTables(const AcpiTableProvider* tables)
        : tables_(tables) {}

    // Initialize the APIC Tables subsystem, this is separate from initializing
    // the whole APIC subsystem and generally happens much earlier. Argument is
    // ignored.
    static void Initialize(uint32_t);

    // Sets count equal to the number of cpus in the system.
    zx_status_t cpu_count(uint32_t* count) const;

    // Populates the apic_ids array with the apic ids of all cpus in the system.
    // Sets apic_id_count equal to the number of ids written to the array and is
    // bounded by array_size.
    zx_status_t cpu_apic_ids(uint32_t* apic_ids, uint32_t array_size, uint32_t* apic_id_count) const;

    // Sets count equal to the number of IO APICs in the system.
    zx_status_t io_apic_count(uint32_t* count) const;

    // Populates the io_apics array with data about the IO APICS in the system,
    // bounded by array_size. io_apics_count will contain how many io_apics were
    // populated in the array.
    zx_status_t io_apics(io_apic_descriptor* io_apics, uint32_t array_size,
                         uint32_t* io_apics_count) const;

    // Populates overrides with data on all overrides, bounded by array_size.
    // override_count will contain the number of overrides populated in the
    // array.
    zx_status_t interrupt_source_overrides(io_apic_isa_override* overrides, uint32_t array_size,
                                           uint32_t* override_count) const;

    // Sets count equal to the number of overrides registered in the system.
    zx_status_t interrupt_source_overrides_count(uint32_t* count) const;

    // Lookup high precision event timer information. Returns ZX_OK and
    // populates hpet if successful, otherwise returns error.
    zx_status_t hpet(acpi_hpet_descriptor* hpet) const;

    // Vists all pairs of cpu apic id and NumaRegion.
    // Visitor is expected to have the signature:
    // void(const AcpiNumaRegion&, uint32_t)
    template <typename V>
    zx_status_t VisitCpuNumaPairs(V visitor) const;

private:
    zx_status_t NumInMadt(uint8_t type, uint32_t* count) const;

    // For each subtable of type run visitor.
    // We can't take a std::function for the visitor because that can use dynamic
    // memory.
    template <typename V>
    zx_status_t ForEachInMadt(uint8_t type, V visitor) const;

    // Set start and end to the first and last (respectively) records in the
    // MADT table.
    zx_status_t GetMadtRecordLimits(uintptr_t* start, uintptr_t* end) const;

    const AcpiTableProvider* const tables_;

    // Whether APIC tables have ever been initialized.
    static bool initialized_;
};

template <typename V>
zx_status_t AcpiTables::VisitCpuNumaPairs(V visitor) const {
    ACPI_TABLE_HEADER* table = NULL;
    ACPI_STATUS status = tables_->GetTable((char*)ACPI_SIG_SRAT, 1, &table);
    if (status != AE_OK) {
        printf("Could not find SRAT table. ACPICA returned: %u\n", status);
        return ZX_ERR_NOT_FOUND;
    }

    ACPI_TABLE_SRAT* srat = (ACPI_TABLE_SRAT*)table;

    static constexpr size_t kSratHeaderSize = 48;
    static constexpr size_t kMaxNumaRegions = 10;
    AcpiNumaRegion regions[kMaxNumaRegions];

    // First find all numa regions.
    size_t offset = kSratHeaderSize;
    while (offset < srat->Header.Length) {
        ACPI_SUBTABLE_HEADER* sub_header = (ACPI_SUBTABLE_HEADER*)((uint64_t)table + offset);
        DEBUG_ASSERT(sub_header->Length > 0);
        offset += sub_header->Length;
        if (sub_header->Type == ACPI_SRAT_TYPE_MEMORY_AFFINITY) {
            const acpi_srat_mem_affinity* mem = (acpi_srat_mem_affinity*)sub_header;
            if (!(mem->Flags & ACPI_SRAT_MEM_ENABLED)) {
                // Ignore disabled entries.
                continue;
            }

            DEBUG_ASSERT(mem->ProximityDomain < kMaxNumaRegions);

            auto& region = regions[mem->ProximityDomain];
            region.base_address = mem->BaseAddress;
            region.length = mem->Length;
            region.domain = mem->ProximityDomain;
            region.initialized = true;

            printf("Numa Region:{ domain: %u base: %#lx length: %lx }\n",
                   mem->ProximityDomain, region.base_address, region.length);
        }
    }

    // Then visit all cpu apic ids and provide the accompanying numa region.
    offset = kSratHeaderSize;
    while (offset < srat->Header.Length) {
        ACPI_SUBTABLE_HEADER* sub_header = (ACPI_SUBTABLE_HEADER*)((uint64_t)table + offset);
        offset += sub_header->Length;
        const auto type = sub_header->Type;
        if (type == ACPI_SRAT_TYPE_CPU_AFFINITY) {
            const auto* cpu = (acpi_srat_cpu_affinity*)sub_header;
            if (!(cpu->Flags & ACPI_SRAT_CPU_ENABLED)) {
                // Ignore disabled entries.
                continue;
            }
            const auto domain = cpu->ProximityDomainLo |
                                (*((uint32_t*)cpu->ProximityDomainHi) & 0xFFFFFF) << 8;
            DEBUG_ASSERT(domain < kMaxNumaRegions);
            DEBUG_ASSERT(regions[domain].initialized);
            visitor(regions[domain], cpu->ApicId);

        } else if (type == ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY) {
            const auto* cpu = (acpi_srat_x2apic_cpu_affinity*)sub_header;
            if (!(cpu->Flags & ACPI_SRAT_CPU_ENABLED)) {
                // Ignore disabled entries.
                continue;
            }

            DEBUG_ASSERT(cpu->ProximityDomain < kMaxNumaRegions);
            DEBUG_ASSERT(regions[cpu->ProximityDomain].initialized);
            visitor(regions[cpu->ProximityDomain], cpu->ApicId);
        }
    }

    return ZX_OK;
}

#endif // KERNEL_LIB_APIC_TABLES_H
