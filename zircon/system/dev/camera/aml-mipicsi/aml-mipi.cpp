// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-mipi.h"
#include "aml-mipi-regs.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <memory>
#include <stdint.h>
#include <threads.h>
#include <zircon/types.h>

// NOTE: A lot of magic numbers, they come from vendor
//       source code.

namespace camera {

namespace {

// MMIO Indexes.
constexpr uint32_t kCsiPhy0 = 0;
constexpr uint32_t kAphy0 = 1;
constexpr uint32_t kCsiHost0 = 2;
constexpr uint32_t kMipiAdap = 3;
constexpr uint32_t kHiu = 4;

// CLK Shifts & Masks
constexpr uint32_t kClkMuxMask = 0xfff;
constexpr uint32_t kClockEnableShift = 8;

} // namespace

void AmlMipiDevice::InitMipiClock() {
    // clear existing values
    hiu_mmio_->ClearBits32(kClkMuxMask, HHI_MIPI_CSI_PHY_CLK_CNTL);
    // set the divisor = 2 (writing (2-1) to div field)
    // source for the unused mux = S905D2_FCLK_DIV5   = 6 // 400 MHz
    hiu_mmio_->SetBits32(((1 << kClockEnableShift) | 6 << 9 | 1),
                         HHI_MIPI_CSI_PHY_CLK_CNTL);
    // TODO(braval@) Double check to look into if
    // this sleep is really necessary.
    zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
}

zx_status_t AmlMipiDevice::InitPdev(zx_device_t* parent) {
    if (!pdev_.is_valid()) {
        return ZX_ERR_NO_RESOURCES;
    }

    zx_status_t status = pdev_.MapMmio(kCsiPhy0, &csi_phy0_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
        return status;
    }

    status = pdev_.MapMmio(kAphy0, &aphy0_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
        return status;
    }

    status = pdev_.MapMmio(kCsiHost0, &csi_host0_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
        return status;
    }

    status = pdev_.MapMmio(kMipiAdap, &mipi_adap_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
        return status;
    }

    status = pdev_.MapMmio(kHiu, &hiu_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
        return status;
    }

    // Get our bti.
    status = pdev_.GetBti(0, &bti_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not obtain bti: %d\n", __func__, status);
        return status;
    }

    // Get adapter interrupt.
    status = pdev_.GetInterrupt(0, &adap_irq_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not obtain adapter interrupt %d\n", __func__, status);
        return status;
    }

    return status;
}

void AmlMipiDevice::MipiPhyReset() {
    uint32_t data32 = 0x1f; //disable lanes digital clock
    data32 |= 0x1 << 31;    //soft reset bit
    csi_phy0_mmio_->Write32(data32, MIPI_PHY_CTRL);
}

void AmlMipiDevice::MipiCsi2Reset() {
    csi_host0_mmio_->Write32(0, MIPI_CSI_PHY_SHUTDOWNZ); // enable power
    csi_host0_mmio_->Write32(0, MIPI_CSI_DPHY_RSTZ);     // release DPHY reset
    csi_host0_mmio_->Write32(0, MIPI_CSI_CSI2_RESETN);   // csi2 reset
}

void AmlMipiDevice::MipiPhyInit(const mipi_info_t* info) {
    if (info->ui_value <= 1) {
        aphy0_mmio_->Write32(0x0b440585, HI_CSI_PHY_CNTL0);
    } else {
        aphy0_mmio_->Write32(0x0b440581, HI_CSI_PHY_CNTL0);
    }

    aphy0_mmio_->Write32(0x803f0000, HI_CSI_PHY_CNTL1);
    aphy0_mmio_->Write32(0x02, HI_CSI_PHY_CNTL3);

    // 3d8 :continue mode
    csi_phy0_mmio_->Write32(0x3d8, MIPI_PHY_CLK_LANE_CTRL);
    // clck miss = 50 ns --(x< 60 ns)
    csi_phy0_mmio_->Write32(0x9, MIPI_PHY_TCLK_MISS);
    // clck settle = 160 ns --(95ns< x < 300 ns)
    csi_phy0_mmio_->Write32(0x1f, MIPI_PHY_TCLK_SETTLE);
    // hs exit = 160 ns --(x>100ns)
    csi_phy0_mmio_->Write32(0x1f, MIPI_PHY_THS_EXIT);
    // hs skip = 55 ns --(40ns<x<55ns+4*UI)
    csi_phy0_mmio_->Write32(0xa, MIPI_PHY_THS_SKIP);

    // No documentation for this regisgter.
    // hs settle = 160 ns --(85 ns + 6*UI<x<145 ns + 10*UI)
    uint32_t settle = ((85 + 145 + (16 * info->ui_value)) / 2) / 5;
    csi_phy0_mmio_->Write32(settle, MIPI_PHY_THS_SETTLE);

    csi_phy0_mmio_->Write32(0x4e20, MIPI_PHY_TINIT); // >100us
    csi_phy0_mmio_->Write32(0x100, MIPI_PHY_TMBIAS);
    csi_phy0_mmio_->Write32(0x1000, MIPI_PHY_TULPS_C);
    csi_phy0_mmio_->Write32(0x100, MIPI_PHY_TULPS_S);
    csi_phy0_mmio_->Write32(0x0c, MIPI_PHY_TLP_EN_W);
    csi_phy0_mmio_->Write32(0x100, MIPI_PHY_TLPOK);
    csi_phy0_mmio_->Write32(0x400000, MIPI_PHY_TWD_INIT);
    csi_phy0_mmio_->Write32(0x400000, MIPI_PHY_TWD_HS);
    csi_phy0_mmio_->Write32(0x0, MIPI_PHY_DATA_LANE_CTRL);
    // enable data lanes pipe line and hs sync bit err.
    csi_phy0_mmio_->Write32((0x3 | (0x1f << 2) | (0x3 << 7)), MIPI_PHY_DATA_LANE_CTRL1);
    csi_phy0_mmio_->Write32(0x00000123, MIPI_PHY_MUX_CTRL0);
    csi_phy0_mmio_->Write32(0x00000123, MIPI_PHY_MUX_CTRL1);

    // NOTE: Possible bug in reference code. Leaving it here for future reference.
    // uint32_t data32 = ((~(info->channel)) & 0xf) | (0 << 4); //enable lanes digital clock
    // data32 |= ((0x10 | info->channel) << 5);        //mipi_chpu  to analog
    csi_phy0_mmio_->Write32(0, MIPI_PHY_CTRL);
}

void AmlMipiDevice::MipiCsi2Init(const mipi_info_t* info) {
    // csi2 reset
    csi_host0_mmio_->Write32(0, MIPI_CSI_CSI2_RESETN);
    // release csi2 reset
    csi_host0_mmio_->Write32(0xffffffff, MIPI_CSI_CSI2_RESETN);
    // release DPHY reset
    csi_host0_mmio_->Write32(0xffffffff, MIPI_CSI_DPHY_RSTZ);
    //set lanes
    csi_host0_mmio_->Write32((info->lanes - 1) & 3, MIPI_CSI_N_LANES);
    // enable power
    csi_host0_mmio_->Write32(0xffffffff, MIPI_CSI_PHY_SHUTDOWNZ);
}

zx_status_t AmlMipiDevice::MipiCsiInit(const mipi_info_t* mipi_info,
                                       const mipi_adap_info_t* adap_info) {

    // Setup MIPI CSI PHY CLK to 200MHz.
    // Setup MIPI ISP CLK to 667MHz.
    InitMipiClock();

    // Initialize the PHY.
    MipiPhyInit(mipi_info);
    // Initialize the CSI Host.
    MipiCsi2Init(mipi_info);

    // Initialize the MIPI Adapter.
    zx_status_t status = MipiAdapInit(adap_info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: MipiAdapInit failed %d\n", __func__, status);
        return status;
    }

    // Start the MIPI Adapter.
    MipiAdapStart(adap_info);
    return status;
}

zx_status_t AmlMipiDevice::MipiCsiDeInit() {
    MipiPhyReset();
    MipiCsi2Reset();
    MipiAdapReset();
    return ZX_OK;
}

void AmlMipiDevice::DdkUnbind() {
    DdkRemove();
}

void AmlMipiDevice::DdkRelease() {
    delete this;
}

zx_status_t AmlMipiDevice::DdkGetProtocol(uint32_t proto_id, void* out_protocol) {
    switch (proto_id) {
    case ZX_PROTOCOL_ISP_IMPL: {
        if (!parent_protocol_.is_valid()) {
            return ZX_ERR_NOT_SUPPORTED;
        }
        parent_protocol_.GetProto(static_cast<isp_impl_protocol_t*>(out_protocol));
        return ZX_OK;
    }
    case ZX_PROTOCOL_MIPI_CSI: {
        self_protocol_.GetProto(static_cast<mipi_csi_protocol_t*>(out_protocol));
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t AmlMipiDevice::Bind(camera_sensor_t* sensor_info) {

    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, sensor_info->vid},
        {BIND_PLATFORM_DEV_PID, 0, sensor_info->pid},
        {BIND_PLATFORM_DEV_DID, 0, sensor_info->did},
    };

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "aml-mipi";
    args.ctx = this;
    args.ops = &ddk_device_proto_;
    args.proto_id = ddk_proto_id_;
    args.proto_ops = ddk_proto_ops_;
    args.props = props;
    args.prop_count = countof(props);

    return pdev_.DeviceAdd(0, &args, &zxdev_);
}

// static
zx_status_t AmlMipiDevice::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto mipi_device = std::unique_ptr<AmlMipiDevice>(new (&ac) AmlMipiDevice(parent));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = mipi_device->InitPdev(parent);
    if (status != ZX_OK) {
        return status;
    }

    // Populate board specific information
    camera_sensor_t sensor_info;
    size_t actual;
    status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &sensor_info,
                                 sizeof(camera_sensor_t), &actual);
    if (status != ZX_OK || actual != sizeof(camera_sensor_t)) {
        zxlogf(ERROR, "aml-mipi: Could not get Sensor Info metadata %d\n", status);
        return status;
    }

    status = mipi_device->Bind(&sensor_info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-mipi driver failed to get added\n");
        return status;
    } else {
        zxlogf(INFO, "aml-mipi driver added\n");
    }

    // mipi_device intentionally leaked as it is now held by DevMgr.
    __UNUSED auto ptr = mipi_device.release();

    return status;
}

AmlMipiDevice::~AmlMipiDevice() {
    adap_irq_.destroy();
    running_.store(false);
    thrd_join(irq_thread_, NULL);
}

zx_status_t aml_mipi_bind(void* ctx, zx_device_t* device) {
    return camera::AmlMipiDevice::Create(device);
}

static zx_driver_ops_t driver_ops = []() {
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = aml_mipi_bind;
    return ops;
}();

} // namespace camera

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_mipi, camera::driver_ops, "aml-mipi-csi2", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_MIPI_CSI),
ZIRCON_DRIVER_END(aml_mipi)
