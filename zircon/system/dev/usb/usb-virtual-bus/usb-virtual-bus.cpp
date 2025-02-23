// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-virtual-bus.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/usb/virtualbus/c/fidl.h>

namespace usb_virtual_bus {

// For mapping bEndpointAddress value to/from index in range 0 - 31.
// OUT endpoints are in range 1 - 15, IN endpoints are in range 17 - 31.
static inline uint8_t EpAddressToIndex(uint8_t addr) {
    return static_cast<uint8_t>(((addr) & 0xF) | (((addr) & 0x80) >> 3));
}
static constexpr uint8_t IN_EP_START = 17;

#define DEVICE_SLOT_ID  0
#define DEVICE_HUB_ID   0
#define DEVICE_SPEED    USB_SPEED_HIGH

zx_status_t UsbVirtualBus::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto bus = fbl::make_unique_checked<UsbVirtualBus>(&ac, parent);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto status = bus->Init();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = bus.release();
    return ZX_OK;
}

zx_status_t UsbVirtualBus::CreateDevice() {
    fbl::AllocChecker ac;
    device_ = fbl::make_unique_checked<UsbVirtualDevice>(&ac, zxdev(), this);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto status = device_->DdkAdd("usb-virtual-device");
    if (status != ZX_OK) {
        device_ = nullptr;
        return status;
    }

    return ZX_OK;
}

zx_status_t UsbVirtualBus::CreateHost() {
    fbl::AllocChecker ac;
    host_ = fbl::make_unique_checked<UsbVirtualHost>(&ac, zxdev(), this);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto status = host_->DdkAdd("usb-virtual-host");
    if (status != ZX_OK) {
        host_ = nullptr;
        return status;
    }

    return ZX_OK;
}

zx_status_t UsbVirtualBus::Init() {
    auto status = DdkAdd("usb-virtual-bus", DEVICE_ADD_NON_BINDABLE);
    if (status != ZX_OK) {
        return status;
    }

    int rc = thrd_create_with_name(&thread_,
                                   [](void* arg) -> int {
                                       return reinterpret_cast<UsbVirtualBus*>(arg)->Thread();
                                   },
                                   reinterpret_cast<void*>(this),
                                   "usb-virtual-bus-thread");
    if (rc != thrd_success) {
        DdkRemove();
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

int UsbVirtualBus::Thread() {
    while (1) {
        sync_completion_wait(&thread_signal_, ZX_TIME_INFINITE);
        sync_completion_reset(&thread_signal_);

        bool unbinding;
        {
            fbl::AutoLock al(&lock_);
            unbinding = unbinding_;
        }
        if (unbinding) {
            for (unsigned i = 0; i < USB_MAX_EPS; i++) {
                usb_virtual_ep_t* ep = &eps_[i];

                for (auto req = ep->host_reqs.pop(); req; req = ep->host_reqs.pop()) {
                    req->Complete(ZX_ERR_IO_NOT_PRESENT, 0);
                }
                for (auto req = ep->device_reqs.pop(); req; req = ep->device_reqs.pop()) {
                    req->Complete(ZX_ERR_IO_NOT_PRESENT, 0);
                }
            }

            return 0;
        }

        // special case endpoint zero
        for (auto request = eps_[0].host_reqs.pop(); request; request = eps_[0].host_reqs.pop()) {
            // Handle control requests outside of the lock to avoid deadlock.
            HandleControl(*std::move(request));
        }

        for (unsigned i = 1; i < USB_MAX_EPS; i++) {
            usb_virtual_ep_t* ep = &eps_[i];
            bool out = (i < IN_EP_START);

            while (!ep->host_reqs.is_empty() && !ep->device_reqs.is_empty()) {
                auto device_req = ep->device_reqs.pop();
                auto req = ep->host_reqs.pop();

                zx_off_t offset = ep->req_offset;
                size_t length = req->request()->header.length - offset;
                if (length > device_req->request()->header.length) {
                    length = device_req->request()->header.length;
                }

                void* device_buffer;
                auto status = device_req->Mmap(&device_buffer);
                if (status != ZX_OK) {
                    zxlogf(ERROR, "%s: usb_request_mmap failed: %d\n", __func__, status);
                    req->Complete(status, 0);
                    device_req->Complete(status, 0);
                    continue;
                }

                if (out) {
                    req->CopyFrom(device_buffer, length, offset);
                } else {
                    req->CopyTo(device_buffer, length, offset);
                }

                offset += length;
                if (offset == req->request()->header.length ||
                    // Short packet in the IN direction signals end of transfer.
                    (!out && device_req->request()->header.length < ep->max_packet_size)) {
                    req->Complete(ZX_OK, length);
                    ep->req_offset = 0;
                } else {
                    ep->req_offset = offset;
                    ep->host_reqs.push_next(*std::move(req));
                }

                device_req->Complete(ZX_OK, length);
            }
        }
    }
    return 0;
}

void UsbVirtualBus::HandleControl(Request request) {
    const usb_setup_t* setup = &request.request()->setup;
    zx_status_t status;
    size_t length = le16toh(setup->wLength);
    size_t actual = 0;

    zxlogf(TRACE, "%s type: 0x%02X req: %d value: %d index: %d length: %zu\n", __func__,
           setup->bmRequestType, setup->bRequest, le16toh(setup->wValue), le16toh(setup->wIndex),
           length);

    if (dci_intf_.is_valid()) {
        void* buffer = nullptr;

        if (length > 0) {
            auto status = request.Mmap(&buffer);
            if (status != ZX_OK) {
                zxlogf(ERROR, "%s: usb_request_mmap failed: %d\n", __func__, status);
                request.Complete(status, 0);
                return;
            }
        }

        if ((setup->bmRequestType & USB_ENDPOINT_DIR_MASK) == USB_ENDPOINT_IN) {
            status = dci_intf_.Control(setup, nullptr, 0, buffer, length, &actual);
        } else {
            status = dci_intf_.Control(setup, buffer, length, nullptr, 0, nullptr);
        }
    } else {
        status = ZX_ERR_UNAVAILABLE;
    }

    request.Complete(status, actual);
}

void UsbVirtualBus::SetConnected(bool connected) {
    bool was_connected = connected;
    {
      fbl::AutoLock lock(&lock_);
      std::swap(connected_, was_connected);
    }

    if (connected && !was_connected) {
        if (bus_intf_.is_valid()) {
            bus_intf_.AddDevice(DEVICE_SLOT_ID, DEVICE_HUB_ID, DEVICE_SPEED);
        }
        if (dci_intf_.is_valid()) {
            dci_intf_.SetConnected(true);
        }
    } else if (!connected && was_connected) {
        if (bus_intf_.is_valid()) {
            bus_intf_.RemoveDevice(DEVICE_SLOT_ID);
        }
        if (dci_intf_.is_valid()) {
            dci_intf_.SetConnected(false);
        }


        for (unsigned i = 0; i < USB_MAX_EPS; i++) {
            usb_virtual_ep_t* ep = &eps_[i];

            for (auto req = ep->host_reqs.pop(); req; req = ep->host_reqs.pop()) {
                req->Complete(ZX_ERR_IO_NOT_PRESENT, 0);
            }
            for (auto req = ep->device_reqs.pop(); req; req = ep->device_reqs.pop()) {
                req->Complete(ZX_ERR_IO_NOT_PRESENT, 0);
            }
        }
    }
}

zx_status_t UsbVirtualBus::SetStall(uint8_t ep_address, bool stall) {
    uint8_t index = EpAddressToIndex(ep_address);
    if (index >= USB_MAX_EPS) {
        return ZX_ERR_INVALID_ARGS;
    }

    std::optional<Request> req = std::nullopt;
    {
        fbl::AutoLock lock(&lock_);

        usb_virtual_ep_t* ep = &eps_[index];
        ep->stalled = stall;

        if (stall) {
            req = ep->host_reqs.pop();
        }
    }

    if (req) {
        req->Complete(ZX_ERR_IO_REFUSED, 0);
    }

    return ZX_OK;
}

static fuchsia_usb_virtualbus_Bus_ops_t fidl_ops = {
    .Enable = [](void* ctx, fidl_txn_t* txn) {
                            return reinterpret_cast<UsbVirtualBus*>(ctx)->MsgEnable(txn); },
    .Disable = [](void* ctx, fidl_txn_t* txn) {
                            return reinterpret_cast<UsbVirtualBus*>(ctx)->MsgDisable(txn); },
    .Connect = [](void* ctx, fidl_txn_t* txn) {
                            return reinterpret_cast<UsbVirtualBus*>(ctx)->MsgConnect(txn); },
    .Disconnect = [](void* ctx, fidl_txn_t* txn) {
                            return reinterpret_cast<UsbVirtualBus*>(ctx)->MsgDisconnect(txn); },
};

zx_status_t UsbVirtualBus::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_usb_virtualbus_Bus_dispatch(this, txn, msg, &fidl_ops);
}

void UsbVirtualBus::DdkUnbind() {
    {
        fbl::AutoLock lock(&lock_);
        unbinding_ = true;
    }
    sync_completion_signal(&thread_signal_);
    thrd_join(thread_, nullptr);

    auto* host = host_.release();
    if (host) {
        host->DdkRemove();
    }
    auto* device = device_.release();
    if (device) {
        device->DdkRemove();
    }
}

void UsbVirtualBus::DdkRelease() {
    delete this;
}

void UsbVirtualBus::UsbDciRequestQueue(usb_request_t* req,
                                       const usb_request_complete_t* complete_cb) {
    Request request(req, *complete_cb, sizeof(usb_request_t));

    uint8_t index = EpAddressToIndex(request.request()->header.ep_address);
    if (index == 0 || index >= USB_MAX_EPS) {
        printf("%s: bad endpoint %u\n", __func__, request.request()->header.ep_address);
        request.Complete(ZX_ERR_INVALID_ARGS, 0);
        return;
    }
    bool connected;
    {
        fbl::AutoLock al(&lock_);
        connected = connected_;
    }
    if (!connected) {
        request.Complete(ZX_ERR_IO_NOT_PRESENT, 0);
        return;
    }

    eps_[index].device_reqs.push(std::move(request));

    sync_completion_signal(&thread_signal_);
}

zx_status_t UsbVirtualBus::UsbDciSetInterface(const usb_dci_interface_t* dci_intf) {
    if (dci_intf) {
        dci_intf_ = ddk::UsbDciInterfaceClient(dci_intf);
    } else {
        dci_intf_.clear();
    }
    return ZX_OK;
}

zx_status_t UsbVirtualBus::UsbDciConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                                          const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    uint8_t index = EpAddressToIndex(ep_desc->bEndpointAddress);
    if (index >= USB_MAX_EPS) {
        return ZX_ERR_INVALID_ARGS;
    }

    usb_virtual_ep_t* ep = &eps_[index];
    ep->max_packet_size = usb_ep_max_packet(ep_desc);
    return ZX_OK;
}

zx_status_t UsbVirtualBus::UsbDciDisableEp(uint8_t ep_address) {
    return ZX_OK;
}

zx_status_t UsbVirtualBus::UsbDciEpSetStall(uint8_t ep_address) {
    return SetStall(ep_address, true);
}

zx_status_t UsbVirtualBus::UsbDciEpClearStall(uint8_t ep_address) {
    return SetStall(ep_address, false);
}

size_t UsbVirtualBus::UsbDciGetRequestSize() {
    return Request::RequestSize(sizeof(usb_request_t));
}

void UsbVirtualBus::UsbHciRequestQueue(usb_request_t* req,
                                       const usb_request_complete_t* complete_cb) {
    Request request(req, *complete_cb, sizeof(usb_request_t));

    uint8_t index = EpAddressToIndex(request.request()->header.ep_address);
    if (index >= USB_MAX_EPS) {
        printf("usb_virtual_bus_host_queue bad endpoint %u\n",
               request.request()->header.ep_address);
        request.Complete(ZX_ERR_INVALID_ARGS, 0);
        return;
    }

    bool connected;
    {
        fbl::AutoLock al(&lock_);
        connected = connected_;
    }
    if (!connected) {
        request.Complete(ZX_ERR_IO_NOT_PRESENT, 0);
        return;
    }

    usb_virtual_ep_t* ep = &eps_[index];

    if (ep->stalled) {
        request.Complete(ZX_ERR_IO_REFUSED, 0);
        return;
    }
    ep->host_reqs.push(std::move(request));

    sync_completion_signal(&thread_signal_);
}

void UsbVirtualBus::UsbHciSetBusInterface(const usb_bus_interface_t* bus_intf) {
    if (bus_intf) {
        bus_intf_ = ddk::UsbBusInterfaceClient(bus_intf);

        bool connected;
        {
            fbl::AutoLock al(&lock_);
            connected = connected_;
        }

        if (connected) {
            bus_intf_.AddDevice(DEVICE_SLOT_ID, DEVICE_HUB_ID, DEVICE_SPEED);
        }
    } else {
        bus_intf_.clear();
    }
}

size_t UsbVirtualBus::UsbHciGetMaxDeviceCount() {
    return 1;
}

zx_status_t UsbVirtualBus::UsbHciEnableEndpoint(uint32_t device_id,
                                                const usb_endpoint_descriptor_t* ep_desc,
                                                const usb_ss_ep_comp_descriptor_t* ss_com_desc,
                                                bool enable) {
    return ZX_OK;
}

uint64_t UsbVirtualBus::UsbHciGetCurrentFrame() {
    return 0;
}

zx_status_t UsbVirtualBus::UsbHciConfigureHub(uint32_t device_id, usb_speed_t speed,
                                              const usb_hub_descriptor_t* desc) {
    return ZX_OK;
}

zx_status_t UsbVirtualBus::UsbHciHubDeviceAdded(uint32_t device_id, uint32_t port,
                                                usb_speed_t speed) {
    return ZX_OK;
}

zx_status_t UsbVirtualBus::UsbHciHubDeviceRemoved(uint32_t device_id, uint32_t port) {
    return ZX_OK;
}

zx_status_t UsbVirtualBus::UsbHciHubDeviceReset(uint32_t device_id, uint32_t port) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbVirtualBus::UsbHciResetEndpoint(uint32_t device_id, uint8_t ep_address) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbVirtualBus::UsbHciResetDevice(uint32_t hub_address, uint32_t device_id) {
    return ZX_ERR_NOT_SUPPORTED;
}

size_t UsbVirtualBus::UsbHciGetMaxTransferSize(uint32_t device_id, uint8_t ep_address) {
    return 65536;
}

zx_status_t UsbVirtualBus::UsbHciCancelAll(uint32_t device_id, uint8_t ep_address) {
    return ZX_ERR_NOT_SUPPORTED;
}

size_t UsbVirtualBus::UsbHciGetRequestSize() {
    return Request::RequestSize(sizeof(usb_request_t));
}

zx_status_t UsbVirtualBus::MsgEnable(fidl_txn_t* txn) {
    fbl::AutoLock lock(&lock_);

    zx_status_t status = ZX_OK;

    if (host_ == nullptr) {
        status = CreateHost();
    }
    if (status == ZX_OK && device_ == nullptr) {
        status = CreateDevice();
    }

    return fuchsia_usb_virtualbus_BusEnable_reply(txn, status);
}

zx_status_t UsbVirtualBus::MsgDisable(fidl_txn_t* txn) {
    SetConnected(false);
    UsbVirtualHost* host;
    UsbVirtualDevice* device;
    {
        fbl::AutoLock lock(&lock_);
        // Use release() here to avoid double free of these objects.
        // devmgr will handle freeing them.
        host = host_.release();
        device = device_.release();
    }
    if (host) {
        host->DdkRemove();
    }
    if (device) {
        device->DdkRemove();
    }
    return fuchsia_usb_virtualbus_BusDisable_reply(txn, ZX_OK);
}

zx_status_t UsbVirtualBus::MsgConnect(fidl_txn_t* txn) {
    if (host_ == nullptr || device_ == nullptr) {
        return fuchsia_usb_virtualbus_BusConnect_reply(txn, ZX_ERR_BAD_STATE);
    }

    SetConnected(true);
    return fuchsia_usb_virtualbus_BusConnect_reply(txn, ZX_OK);
}

zx_status_t UsbVirtualBus::MsgDisconnect(fidl_txn_t* txn) {
    if (host_ == nullptr || device_ == nullptr) {
        return fuchsia_usb_virtualbus_BusDisconnect_reply(txn, ZX_ERR_BAD_STATE);
    }

    SetConnected(false);
    return fuchsia_usb_virtualbus_BusDisconnect_reply(txn, ZX_OK);
}

static zx_status_t usb_virtual_bus_bind(void* ctx, zx_device_t* parent) {
    return usb_virtual_bus::UsbVirtualBus::Create(parent);
}

static zx_driver_ops_t driver_ops = [](){
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = usb_virtual_bus_bind;
    return ops;
}();

} // namespace usb_virtual_bus

ZIRCON_DRIVER_BEGIN(usb_virtual_bus, usb_virtual_bus::driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT),
ZIRCON_DRIVER_END(usb_virtual_bus)
