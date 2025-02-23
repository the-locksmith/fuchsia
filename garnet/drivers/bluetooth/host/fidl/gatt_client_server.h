// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_HOST_FIDL_GATT_CLIENT_SERVER_H_
#define GARNET_DRIVERS_BLUETOOTH_HOST_FIDL_GATT_CLIENT_SERVER_H_

#include <fuchsia/bluetooth/gatt/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

#include "garnet/drivers/bluetooth/host/fidl/gatt_remote_service_server.h"
#include "garnet/drivers/bluetooth/host/fidl/server_base.h"

namespace bthost {

// Implements the gatt::Client FIDL interface.
class GattClientServer
    : public GattServerBase<fuchsia::bluetooth::gatt::Client> {
 public:
  GattClientServer(
      std::string peer_id, fbl::RefPtr<btlib::gatt::GATT> gatt,
      fidl::InterfaceRequest<fuchsia::bluetooth::gatt::Client> request);
  ~GattClientServer() override = default;

 private:
  // bluetooth::gatt::Client overrides:
  void ListServices(::fidl::VectorPtr<::std::string> uuids,
                    ListServicesCallback callback) override;
  void ConnectToService(
      uint64_t id,
      ::fidl::InterfaceRequest<fuchsia::bluetooth::gatt::RemoteService> service)
      override;

  // The ID of the peer that this client is attached to.
  std::string peer_id_;

  // Remote GATT services that were connected through this client. The value can
  // be null while a ConnectToService request is in progress.
  std::unordered_map<uint64_t, std::unique_ptr<GattRemoteServiceServer>>
      connected_services_;

  fxl::WeakPtrFactory<GattClientServer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GattClientServer);
};

}  // namespace bthost

#endif  // GARNET_DRIVERS_BLUETOOTH_HOST_FIDL_GATT_CLIENT_SERVER_H_
