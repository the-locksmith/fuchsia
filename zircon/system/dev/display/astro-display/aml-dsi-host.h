// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/dsiimpl.h>

#include <ddktl/protocol/dsiimpl.h>
#include <ddktl/device.h>

#include <fbl/unique_ptr.h>
#include <hwreg/mmio.h>
#include <optional>
#include <unistd.h>
#include <zircon/compiler.h>

#include "common.h"
#include "hhi-regs.h"
#include "vpu-regs.h"
#include "lcd.h"
#include "aml-mipi-phy.h"

namespace astro_display {

class AmlDsiHost {
public:
    AmlDsiHost(zx_device_t* parent, uint32_t bitrate, uint8_t panel_type)
        : parent_(parent), bitrate_(bitrate), panel_type_(panel_type) {}

    // This function sets up mipi dsi interface. It includes both DWC and AmLogic blocks
    // The DesignWare setup could technically be moved to the dw_mipi_dsi driver. However,
    // given the highly configurable nature of this block, we'd have to provide a lot of
    // information to the generic driver. Therefore, it's just simpler to configure it here
    zx_status_t Init();
    zx_status_t HostOn(const display_setting_t& disp_setting);
    // This function will turn off DSI Host. It is a "best-effort" function. We will attempt
    // to shutdown whatever we can. Error during shutdown path is ignored and function proceeds
    // with shutting down.
    void HostOff(const display_setting_t& disp_setting);
    void Dump();
private:
    void PhyEnable();
    void PhyDisable();
    zx_status_t HostModeInit(const display_setting_t& disp_setting);

    std::optional<ddk::MmioBuffer> mipi_dsi_mmio_;
    std::optional<ddk::MmioBuffer> hhi_mmio_;

    pdev_protocol_t pdev_ = {};

    ddk::DsiImplProtocolClient dsiimpl_;

    zx_device_t* parent_;

    uint32_t bitrate_;
    uint8_t panel_type_;

    bool initialized_ = false;
    bool host_on_ = false;

    fbl::unique_ptr<Lcd> lcd_;
    fbl::unique_ptr<AmlMipiPhy> phy_;

};

} // namespace astro_display
