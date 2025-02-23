// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_TESTS_MOCK_HID_DECODER_H_
#define GARNET_BIN_UI_INPUT_READER_TESTS_MOCK_HID_DECODER_H_

#include <lib/fit/function.h>
#include <zx/event.h>

#include "garnet/bin/ui/input_reader/hid_decoder.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace mozart {

// Mocks HidDecoder and allows sending arbitrary ReportDescriptors and Reports
// through |SendReportDescriptor| and |Send|.
class MockHidDecoder : public HidDecoder {
 public:
  MockHidDecoder() : weak_ptr_factory_(this) {}
  MockHidDecoder(std::vector<uint8_t> report_descriptor)
      : weak_ptr_factory_(this) {
    report_descriptor_.data = report_descriptor;
    report_descriptor_.length = report_descriptor.size();
  }
  MockHidDecoder(BootMode boot_mode) : weak_ptr_factory_(this) {
    boot_mode_ = boot_mode;
  }
  ~MockHidDecoder() override;

  fxl::WeakPtr<MockHidDecoder> GetWeakPtr();

  // |HidDecoder|
  const std::string& name() const override;
  // |HidDecoder|
  bool Init() override;
  // |HidDecoder|
  zx::event GetEvent() override;
  // |HidDecoder|
  BootMode ReadBootMode() const override;
  // |HidDecoder|
  void SetupDevice(Device device) override;
  // |HidDecoder|
  const std::vector<uint8_t>& ReadReportDescriptor(int* bytes_read) override;
  // |HidDecoder|
  const std::vector<uint8_t>& Read(int* bytes_read) override;

  // Emulates sending a report, which will be read by |Read|.
  // There must not be a pending report that has not been |Read|.
  void Send(std::vector<uint8_t> bytes, int length);
  // Sets the report descripter, which will be read by
  // |ReadReportDescriptor|. This should only be called once at the beginning
  // of setting up |MockHidDecoder|.
  void SetReportDescriptor(std::vector<uint8_t> bytes, int length);
  // Sets the Boot Mode, which is read by |ReadBootMode|.
  void SetBootMode(HidDecoder::BootMode boot_mode);
  // Emulates removing the device. There must not be a pending report that has
  // not been |Read|.
  void Close();

 private:
  struct Report {
    std::vector<uint8_t> data;
    // This can be shorter than the length of the |data| vector.
    int length;
  };

  void Signal();
  void ClearReport();

  zx::event event_;
  Report report_ = {};
  Report report_descriptor_ = {};
  HidDecoder::BootMode boot_mode_ = HidDecoder::BootMode::NONE;

  fxl::WeakPtrFactory<MockHidDecoder> weak_ptr_factory_;
};

}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_TESTS_MOCK_HID_DECODER_H_
