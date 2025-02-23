// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_DEVICE_INTERFACE_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_DEVICE_INTERFACE_H_

#include <wlan/mlme/mac_frame.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <wlan/common/macaddr.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

#include <fuchsia/wlan/minstrel/cpp/fidl.h>

#include <cstdint>
#include <cstring>

namespace wlan {

class Packet;
class Timer;

// DeviceState represents the common runtime state of a device needed for interacting with external
// systems.
class DeviceState : public fbl::RefCounted<DeviceState> {
   public:
    const common::MacAddr& address() const { return addr_; }
    void set_address(const common::MacAddr& addr) { addr_ = addr; }

    wlan_channel_t channel() const { return chan_; }
    void set_channel(const wlan_channel_t& chan) { chan_ = chan; }

    bool online() { return online_; }
    void set_online(bool online) { online_ = online; }

   private:
    common::MacAddr addr_;
    wlan_channel_t chan_ = {};
    bool online_ = false;
};

// DeviceInterface represents the actions that may interact with external systems.
class DeviceInterface {
   public:
    virtual ~DeviceInterface() {}

    virtual zx_status_t GetTimer(uint64_t id, fbl::unique_ptr<Timer>* timer) = 0;

    virtual zx_status_t DeliverEthernet(Span<const uint8_t> eth_frame) = 0;
    virtual zx_status_t SendWlan(fbl::unique_ptr<Packet> packet, uint32_t flags = 0) = 0;
    virtual zx_status_t SendService(Span<const uint8_t> span) = 0;

    virtual zx_status_t SetChannel(wlan_channel_t chan) = 0;
    virtual zx_status_t SetStatus(uint32_t status) = 0;
    virtual zx_status_t ConfigureBss(wlan_bss_config_t* cfg) = 0;
    virtual zx_status_t EnableBeaconing(wlan_bcn_config_t* bcn_cfg) = 0;
    virtual zx_status_t ConfigureBeacon(fbl::unique_ptr<Packet> packet) = 0;
    virtual zx_status_t SetKey(wlan_key_config_t* key_config) = 0;
    virtual zx_status_t StartHwScan(const wlan_hw_scan_config_t* scan_config) = 0;
    virtual zx_status_t ConfigureAssoc(wlan_assoc_ctx_t* assoc_ctx) = 0;
    virtual zx_status_t ClearAssoc(const common::MacAddr& peer_addr) = 0;
    virtual zx_status_t GetMinstrelPeers(::fuchsia::wlan::minstrel::Peers* peers_fidl) = 0;
    virtual zx_status_t GetMinstrelStats(const common::MacAddr& addr,
                                         ::fuchsia::wlan::minstrel::Peer* peer_fidl) = 0;

    virtual fbl::RefPtr<DeviceState> GetState() = 0;
    virtual const wlanmac_info_t& GetWlanInfo() const = 0;
};

}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_DEVICE_INTERFACE_H_
