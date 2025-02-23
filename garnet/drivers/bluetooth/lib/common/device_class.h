// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_DEVICE_CLASS_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_DEVICE_CLASS_H_

#include <array>
#include <initializer_list>
#include <string>
#include <unordered_set>

namespace btlib {
namespace common {

// Represents a 24-bit "Class of Device/Service" field.
// This data structure can be directly serialized into HCI command payloads.
// See the Bluetooth SIG Assigned Numbers for the Baseband
// (https://www.bluetooth.com/specifications/assigned-numbers/baseband)
// for the format.
class DeviceClass {
 public:
  enum class MajorClass : uint8_t {
    kMiscellaneous = 0x00,
    kComputer = 0x01,
    kPhone = 0x02,
    kLAN = 0x03,
    kAudioVideo = 0x04,
    kPeripheral = 0x05,
    kImaging = 0x06,
    kWearable = 0x07,
    kToy = 0x08,
    kHealth = 0x09,
    kUnspecified = 0x1F,
  };

  enum class ServiceClass : uint8_t {
    kPositioning = 16,
    kNetworking = 17,
    kRendering = 18,
    kCapturing = 19,
    kObjectTransfer = 20,
    kAudio = 21,
    kTelephony = 22,
    kInformation = 23,
  };

  // Initializes the device to an uncategorized device with no services.
  DeviceClass();

  // Initializes the contents from |bytes|, as they are represented from the
  // controller (little-endian)
  explicit DeviceClass(std::initializer_list<uint8_t> bytes);

  // Initializes the contents using the given |major_class|.
  explicit DeviceClass(MajorClass major_class);

  MajorClass major_class() const { return MajorClass(bytes_[1] & 0x1F); }

  // Sets the major service classes of this.
  // Clears any service classes that are not set.
  void SetServiceClasses(const std::unordered_set<ServiceClass>& classes);

  // Gets a set representing the major service classes that are set.
  std::unordered_set<ServiceClass> GetServiceClasses() const;

  // Returns a string describing the device, like "Computer" or "Headphones"
  std::string ToString() const;

  // Equality operators
  bool operator==(const DeviceClass& rhs) const { return rhs.bytes_ == bytes_; }
  bool operator!=(const DeviceClass& rhs) const { return rhs.bytes_ != bytes_; }

  // TODO(jamuraa): add MinorClass
 private:
  std::array<uint8_t, 3> bytes_;
};

static_assert(sizeof(DeviceClass) == 3,
              "DeviceClass must take up exactly 3 bytes");

}  // namespace common
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_DEVICE_CLASS_H_
