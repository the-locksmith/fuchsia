// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <garnet/lib/inet/ip_address.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "src/lib/files/unique_fd.h"
#include <lib/fxl/strings/string_printf.h>
#include <sys/socket.h>
#include <future>

#include "enclosed_guest.h"
#include "guest_test.h"

using ::testing::Each;
using ::testing::HasSubstr;

static constexpr char kVirtioNetUtil[] = "virtio_net_test_util";
static constexpr size_t kMtu = 1500;
static constexpr size_t kTestPacketSize = 1000;

// Includes ethernet + IPv6 + UDP headers.
static constexpr size_t kHeadersSize = 62;

template <class T>
T* GuestTest<T>::enclosed_guest_ = nullptr;

class VirtioNetZirconGuest : public ZirconEnclosedGuest {
 public:
  zx_status_t LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) override {
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--virtio-net=true");
    launch_info->args.push_back("--cmdline-add=kernel.serial=none");
    // Disable netsvc to avoid spamming the net device with logs.
    launch_info->args.push_back("--cmdline-add=netsvc.disable=true");
    launch_info->args.push_back("--legacy-net=false");
    return ZX_OK;
  }
};

class VirtioNetLinuxGuest : public LinuxEnclosedGuest {
 public:
  zx_status_t LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) override {
    launch_info->url = kLinuxGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--virtio-net=true");
    launch_info->args.push_back("--cmdline=console=hvc0 root=/dev/vda rw");
    // launch_info->args.push_back(
    //    "--cmdline=loglevel=0 console=hvc0 root=/dev/vda rw");
    launch_info->args.push_back("--legacy-net=false");
    return ZX_OK;
  }
};

static void TestThread(const MockNetstack& netstack, uint8_t receive_byte,
                       uint8_t send_byte, bool use_raw_packets) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  zx_status_t status;
  if (!use_raw_packets) {
    status = netstack.SendAdvertisement();
    ASSERT_EQ(status, ZX_OK);
  }

  // This thread will loop indefinitely until it receives the correct packet.
  // The test will time out via RunUtil in the test fixture if we fail to
  // receive the correct packet.
  uint8_t pkt[kMtu];
  while (true) {
    size_t actual;
    status = netstack.ReceivePacket(static_cast<void*>(pkt), kMtu, &actual);
    ASSERT_EQ(status, ZX_OK);

    bool match_test_packet = false;
    size_t headers_size = use_raw_packets ? 0 : kHeadersSize;
    if (actual == headers_size + kTestPacketSize) {
      match_test_packet = true;
      for (size_t i = headers_size; i != actual; ++i) {
        if (pkt[i] != receive_byte) {
          match_test_packet = false;
          break;
        }
      }
    }
    if (match_test_packet) {
      break;
    }
  }

  memset(pkt, send_byte, kTestPacketSize);
  if (use_raw_packets) {
    status = netstack.SendPacket(static_cast<void*>(pkt), kTestPacketSize);
  } else {
    status = netstack.SendUdpPacket(static_cast<void*>(pkt), kTestPacketSize);
  }
  ASSERT_EQ(status, ZX_OK);
}

using VirtioNetZirconGuestTest = GuestTest<VirtioNetZirconGuest>;

TEST_F(VirtioNetZirconGuestTest, ReceiveAndSend) {
  auto handle = std::async(std::launch::async, [this] {
    MockNetstack* netstack = this->GetEnclosedGuest()->GetNetstack();
    TestThread(*netstack, 0xab, 0xba, true /* use_raw_packets */);
  });

  std::string args =
      fxl::StringPrintf("%u %u %zu", 0xab, 0xba, kTestPacketSize);
  std::string result;
  EXPECT_EQ(this->RunUtil(kVirtioNetUtil, args, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));

  handle.wait();
}

using VirtioNetLinuxGuestTest = GuestTest<VirtioNetLinuxGuest>;

TEST_F(VirtioNetLinuxGuestTest, ReceiveAndSend) {
  std::string result;
  EXPECT_EQ(this->RunUtil(kVirtioNetUtil, "up", &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));

  auto handle = std::async(std::launch::async, [this] {
    MockNetstack* netstack = this->GetEnclosedGuest()->GetNetstack();
    TestThread(*netstack, 0xab, 0xba, false /* use_raw_packets */);
  });

  std::string args =
      fxl::StringPrintf("xfer %u %u %zu", 0xab, 0xba, kTestPacketSize);
  EXPECT_EQ(this->RunUtil(kVirtioNetUtil, args, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));

  handle.wait();
}
