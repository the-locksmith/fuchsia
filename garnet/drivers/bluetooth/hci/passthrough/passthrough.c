// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/bt/hci.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/bt-hci.h>
#include <zircon/status.h>

typedef struct {
  zx_device_t* dev;
  zx_device_t* transport_dev;

  bt_hci_protocol_t hci;
} passthrough_t;

static zx_status_t bt_hci_passthrough_get_protocol(void* ctx, uint32_t proto_id,
                                                   void* out_proto) {
  if (proto_id != ZX_PROTOCOL_BT_HCI) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  passthrough_t* passthrough = ctx;
  bt_hci_protocol_t* hci_proto = out_proto;

  // Forward the underlying bt-transport ops.
  hci_proto->ops = passthrough->hci.ops;
  hci_proto->ctx = passthrough->hci.ctx;

  return ZX_OK;
}

static zx_status_t bt_hci_passthrough_ioctl(void* ctx, uint32_t op,
                                            const void* in_buf, size_t in_len,
                                            void* out_buf, size_t out_len,
                                            size_t* out_actual) {
  passthrough_t* passthrough = ctx;
  if (out_len < sizeof(zx_handle_t)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  zx_handle_t* reply = out_buf;

  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  if (op == IOCTL_BT_HCI_GET_COMMAND_CHANNEL) {
    status = bt_hci_open_command_channel(&passthrough->hci, reply);
  } else if (op == IOCTL_BT_HCI_GET_ACL_DATA_CHANNEL) {
    status = bt_hci_open_acl_data_channel(&passthrough->hci, reply);
  } else if (op == IOCTL_BT_HCI_GET_SNOOP_CHANNEL) {
    status = bt_hci_open_snoop_channel(&passthrough->hci, reply);
  }

  if (status != ZX_OK) {
    return status;
  }

  *out_actual = sizeof(*reply);
  return ZX_OK;
}

static void bt_hci_passthrough_unbind(void* ctx) {
  passthrough_t* passthrough = ctx;

  device_remove(passthrough->dev);
}

static void bt_hci_passthrough_release(void* ctx) { free(ctx); }

static zx_protocol_device_t passthrough_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = bt_hci_passthrough_get_protocol,
    .ioctl = bt_hci_passthrough_ioctl,
    .unbind = bt_hci_passthrough_unbind,
    .release = bt_hci_passthrough_release,
};

static zx_status_t bt_hci_passthrough_bind(void* ctx, zx_device_t* device) {
  printf("bt_hci_passthrough_bind: starting\n");
  passthrough_t* passthrough = calloc(1, sizeof(passthrough_t));
  if (!passthrough) {
    printf("bt_hci_passthrough_bind: not enough memory\n");
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status =
      device_get_protocol(device, ZX_PROTOCOL_BT_HCI, &passthrough->hci);
  if (status != ZX_OK) {
    printf("bt_hci_passthrough_bind: failed protocol: %s\n",
           zx_status_get_string(status));
    return status;
  }

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "bt_hci_passthrough",
      .ctx = passthrough,
      .ops = &passthrough_device_proto,
      .proto_id = ZX_PROTOCOL_BT_HCI,
  };

  passthrough->transport_dev = device;

  status = device_add(device, &args, &passthrough->dev);
  if (status == ZX_OK) {
    return status;
  }

  printf("bt_hci_passthrough_bind failed: %s\n", zx_status_get_string(status));
  bt_hci_passthrough_release(passthrough);
  return status;
}

static zx_driver_ops_t bt_hci_passthrough_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = bt_hci_passthrough_bind,
};

// This should be the last driver queried, so we match any transport.
// clang-format off
ZIRCON_DRIVER_BEGIN(bt_hci_passthrough, bt_hci_passthrough_driver_ops, "fuchsia", "*0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BT_TRANSPORT),
ZIRCON_DRIVER_END(bt_hci_passthrough)
