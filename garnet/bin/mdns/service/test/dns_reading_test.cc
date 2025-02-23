// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/dns_reading.h"
#include "garnet/bin/mdns/service/packet_reader.h"
#include "gtest/gtest.h"

namespace mdns {
namespace test {

// Note: Most of the test data in this file was captured from error messages
// produced by the mdns service when it failed to recognized messages. The
// parsed messages appearing in comments were generated using the ostream
// overloads in dns_formatting.h once the parser was fixed in each case.
// TODO(dalesat): Provide an easy way to generate new test cases.

// Tests parsing of a message that failed in early development.
TEST(DnsReadingTest, Regression0) {
  /*
  header:
      id: 0
      flags: 0x0000
  questions:
  [0]
      name:
  6.5.4.3.2.1.e.f.d.4.0.0.4.5.0.5.0.0.0.0.0.0.0.0.0.0.0.0.0.8.e.f.ip6.arpa.
      type: PTR
      class: IN
      unicast_response: 0
  additionals:
  [0]
      name:
      type: OPT
      class: CLASS 1440
      cache_flush: 0
      time_to_live: 4500
      options:
          ...
  */
  std::vector<uint8_t> buffer{
      0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
      0x01, 0x36, 0x01, 0x35, 0x01, 0x34, 0x01, 0x33, 0x01, 0x32, 0x01, 0x31,
      0x01, 0x65, 0x01, 0x66, 0x01, 0x64, 0x01, 0x34, 0x01, 0x30, 0x01, 0x30,
      0x01, 0x34, 0x01, 0x35, 0x01, 0x30, 0x01, 0x35, 0x01, 0x30, 0x01, 0x30,
      0x01, 0x30, 0x01, 0x30, 0x01, 0x30, 0x01, 0x30, 0x01, 0x30, 0x01, 0x30,
      0x01, 0x30, 0x01, 0x30, 0x01, 0x30, 0x01, 0x30, 0x01, 0x30, 0x01, 0x38,
      0x01, 0x65, 0x01, 0x66, 0x03, 0x69, 0x70, 0x36, 0x04, 0x61, 0x72, 0x70,
      0x61, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x29, 0x05, 0xa0, 0x00,
      0x00, 0x11, 0x94, 0x00, 0x12, 0x00, 0x04, 0x00, 0x0e, 0x00, 0x05, 0x80,
      0xe6, 0x50, 0x10, 0xf3, 0x12, 0xee, 0x62, 0x50, 0x1e, 0x88, 0x4c};

  PacketReader reader(buffer);
  DnsMessage message;
  reader >> message;
  EXPECT_TRUE(reader.healthy());
  EXPECT_TRUE(reader.complete());
}

// Tests parsing of a message that failed in early development.
TEST(DnsReadingTest, Regression1) {
  /*
    header:
        id: 0
        flags: 0x8400
    answers:
    [0]
        name: _services._dns-sd._udp.local.
        type: PTR
        class: IN
        cache_flush: 0
        time_to_live: 4500
        pointer_domain_name_: _mediaremotetv._tcp.local.
    [1]
        name: _mediaremotetv._tcp.local.
        type: PTR
        class: IN
        cache_flush: 0
        time_to_live: 4500
        pointer_domain_name_: Apple TV._mediaremotetv._tcp.local.
    [2]
        name: Apple TV._device-info._tcp.local.
        type: TXT
        class: IN
        cache_flush: 0
        time_to_live: 4500
        text:
            "model=J42dAP"
    [3]
        name: _services._dns-sd._udp.local.
        type: PTR
        class: IN
        cache_flush: 0
        time_to_live: 4500
        pointer_domain_name_: _airplay._tcp.local.
    [4]
        name: _airplay._tcp.local.
        type: PTR
        class: IN
        cache_flush: 0
        time_to_live: 4500
        pointer_domain_name_: Apple TV._airplay._tcp.local.
    [5]
        name: _services._dns-sd._udp.local.
        type: PTR
        class: IN
        cache_flush: 0
        time_to_live: 4500
        pointer_domain_name_: _raop._tcp.local.
    [6]
        name: _raop._tcp.local.
        type: PTR
        class: IN
        cache_flush: 0
        time_to_live: 4500
        pointer_domain_name_: C869CD3A48FF@Apple TV._raop._tcp.local.
    [7]
        name: Apple TV._mediaremotetv._tcp.local.
        type: TXT
        class: IN
        cache_flush: 1
        time_to_live: 4500
        text:
            "UniqueIdentifier=C30116AB-B0E9-4C5D-9186-E691C17A73C3"
            "Name=Apple TV"
            "SystemBuildVersion=14W265"
            "ModelName="
            "AllowPairing=YES"
    [8]
        name: Apple TV._mediaremotetv._tcp.local.
        type: SRV
        class: IN
        cache_flush: 1
        time_to_live: 120
        priority: 0
        weight: 0
        port: 58387
        target: Apple-TV-3.local.
    [9]
        name: Apple TV._airplay._tcp.local.
        type: TXT
        class: IN
        cache_flush: 1
        time_to_live: 4500
        text:
            "deviceid=C8:69:CD:3A:48:FF"
            "features=0x5A7FFFF7,0xCDE"
            "flags=0x244"
            "model=AppleTV5,3"
            "pi=cf7f16a6-bb37-4026-9785-652f44edc2b3"
            "pk=b90130a3383841ba9111a88630f08ff24aac2b02d4c73dc6313097100a3b84b4"
            "psi=C987E66D-726E-4655-9D66-C899A54242B0"
            "srcvers=320.20"
            "vv=2"
    [10]
        name: C869CD3A48FF@Apple TV._raop._tcp.local.
        type: TXT
        class: IN
        cache_flush: 1
        time_to_live: 4500
        text:
            "cn=0,1,2,3"
            "da=true"
            "et=0,3,5"
            "ft=0x5A7FFFF7,0xCDE"
            "sf=0x244"
            "md=0,1,2"
            "am=AppleTV5,3"
            "pk=b90130a3383841ba9111a88630f08ff24aac2b02d4c73dc6313097100a3b84b4"
            "tp=UDP"
            "vn=65537"
            "vs=320.20"
            "vv=2"
    [11]
        name: Apple TV._airplay._tcp.local.
        type: SRV
        class: IN
        cache_flush: 1
        time_to_live: 120
        priority: 0
        weight: 0
        port: 7000
        target: Apple-TV-3.local.
    [12]
        name: C869CD3A48FF@Apple TV._raop._tcp.local.
        type: SRV
        class: IN
        cache_flush: 1
        time_to_live: 120
        priority: 0
        weight: 0
        port: 7000
        target: Apple-TV-3.local.
    [13]
        name: Apple-TV-3.local.
        type: AAAA
        class: IN
        cache_flush: 1
        time_to_live: 120
        address: [80fe::6e04:7fdf:9096:7f33]
    [14]
        name: Apple-TV-3.local.
        type: A
        class: IN
        cache_flush: 1
        time_to_live: 120
        address: 10.0.0.108
    additionals:
    [0]
        name: Apple TV._mediaremotetv._tcp.local.
        type: NSEC
        class: IN
        cache_flush: 1
        time_to_live: 4500
        next_domain: Apple TV._mediaremotetv._tcp.local.
        bits:
        0000  00 05 00 00 80 00 40                              ......@
    [1]
        name: Apple TV._airplay._tcp.local.
        type: NSEC
        class: IN
        cache_flush: 1
        time_to_live: 4500
        next_domain: Apple TV._airplay._tcp.local.
        bits:
        0000  00 05 00 00 80 00 40                              ......@
    [2]
        name: C869CD3A48FF@Apple TV._raop._tcp.local.
        type: NSEC
        class: IN
        cache_flush: 1
        time_to_live: 4500
        next_domain: C869CD3A48FF@Apple TV._raop._tcp.local.
        bits:
        0000  00 05 00 00 80 00 40                              ......@
    [3]
        name: Apple-TV-3.local.
        type: NSEC
        class: IN
        cache_flush: 1
        time_to_live: 120
        next_domain: Apple-TV-3.local.
        bits:
        0000  00 04 40 00 00 08                                 ..@...
  */
  std::vector<uint8_t> buffer{
      0x00, 0x00, 0x84, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x04,
      0x09, 0x5f, 0x73, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x73, 0x07, 0x5f,
      0x64, 0x6e, 0x73, 0x2d, 0x73, 0x64, 0x04, 0x5f, 0x75, 0x64, 0x70, 0x05,
      0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00,
      0x11, 0x94, 0x00, 0x16, 0x0e, 0x5f, 0x6d, 0x65, 0x64, 0x69, 0x61, 0x72,
      0x65, 0x6d, 0x6f, 0x74, 0x65, 0x74, 0x76, 0x04, 0x5f, 0x74, 0x63, 0x70,
      0xc0, 0x23, 0xc0, 0x34, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x11, 0x94,
      0x00, 0x0b, 0x08, 0x41, 0x70, 0x70, 0x6c, 0x65, 0x20, 0x54, 0x56, 0xc0,
      0x34, 0x08, 0x41, 0x70, 0x70, 0x6c, 0x65, 0x20, 0x54, 0x56, 0x0c, 0x5f,
      0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x2d, 0x69, 0x6e, 0x66, 0x6f, 0xc0,
      0x43, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00, 0x11, 0x94, 0x00, 0x0d, 0x0c,
      0x6d, 0x6f, 0x64, 0x65, 0x6c, 0x3d, 0x4a, 0x34, 0x32, 0x64, 0x41, 0x50,
      0xc0, 0x0c, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x11, 0x94, 0x00, 0x0b,
      0x08, 0x5f, 0x61, 0x69, 0x72, 0x70, 0x6c, 0x61, 0x79, 0xc0, 0x43, 0xc0,
      0x9c, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x11, 0x94, 0x00, 0x0b, 0x08,
      0x41, 0x70, 0x70, 0x6c, 0x65, 0x20, 0x54, 0x56, 0xc0, 0x9c, 0xc0, 0x0c,
      0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x11, 0x94, 0x00, 0x08, 0x05, 0x5f,
      0x72, 0x61, 0x6f, 0x70, 0xc0, 0x43, 0xc0, 0xca, 0x00, 0x0c, 0x00, 0x01,
      0x00, 0x00, 0x11, 0x94, 0x00, 0x18, 0x15, 0x43, 0x38, 0x36, 0x39, 0x43,
      0x44, 0x33, 0x41, 0x34, 0x38, 0x46, 0x46, 0x40, 0x41, 0x70, 0x70, 0x6c,
      0x65, 0x20, 0x54, 0x56, 0xc0, 0xca, 0xc0, 0x56, 0x00, 0x10, 0x80, 0x01,
      0x00, 0x00, 0x11, 0x94, 0x00, 0x7a, 0x35, 0x55, 0x6e, 0x69, 0x71, 0x75,
      0x65, 0x49, 0x64, 0x65, 0x6e, 0x74, 0x69, 0x66, 0x69, 0x65, 0x72, 0x3d,
      0x43, 0x33, 0x30, 0x31, 0x31, 0x36, 0x41, 0x42, 0x2d, 0x42, 0x30, 0x45,
      0x39, 0x2d, 0x34, 0x43, 0x35, 0x44, 0x2d, 0x39, 0x31, 0x38, 0x36, 0x2d,
      0x45, 0x36, 0x39, 0x31, 0x43, 0x31, 0x37, 0x41, 0x37, 0x33, 0x43, 0x33,
      0x0d, 0x4e, 0x61, 0x6d, 0x65, 0x3d, 0x41, 0x70, 0x70, 0x6c, 0x65, 0x20,
      0x54, 0x56, 0x19, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x42, 0x75, 0x69,
      0x6c, 0x64, 0x56, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x3d, 0x31, 0x34,
      0x57, 0x32, 0x36, 0x35, 0x0a, 0x4d, 0x6f, 0x64, 0x65, 0x6c, 0x4e, 0x61,
      0x6d, 0x65, 0x3d, 0x10, 0x41, 0x6c, 0x6c, 0x6f, 0x77, 0x50, 0x61, 0x69,
      0x72, 0x69, 0x6e, 0x67, 0x3d, 0x59, 0x45, 0x53, 0xc0, 0x56, 0x00, 0x21,
      0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00,
      0xe4, 0x13, 0x0a, 0x41, 0x70, 0x70, 0x6c, 0x65, 0x2d, 0x54, 0x56, 0x2d,
      0x33, 0xc0, 0x23, 0xc0, 0xb3, 0x00, 0x10, 0x80, 0x01, 0x00, 0x00, 0x11,
      0x94, 0x00, 0xfb, 0x1a, 0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x69, 0x64,
      0x3d, 0x43, 0x38, 0x3a, 0x36, 0x39, 0x3a, 0x43, 0x44, 0x3a, 0x33, 0x41,
      0x3a, 0x34, 0x38, 0x3a, 0x46, 0x46, 0x19, 0x66, 0x65, 0x61, 0x74, 0x75,
      0x72, 0x65, 0x73, 0x3d, 0x30, 0x78, 0x35, 0x41, 0x37, 0x46, 0x46, 0x46,
      0x46, 0x37, 0x2c, 0x30, 0x78, 0x43, 0x44, 0x45, 0x0b, 0x66, 0x6c, 0x61,
      0x67, 0x73, 0x3d, 0x30, 0x78, 0x32, 0x34, 0x34, 0x10, 0x6d, 0x6f, 0x64,
      0x65, 0x6c, 0x3d, 0x41, 0x70, 0x70, 0x6c, 0x65, 0x54, 0x56, 0x35, 0x2c,
      0x33, 0x27, 0x70, 0x69, 0x3d, 0x63, 0x66, 0x37, 0x66, 0x31, 0x36, 0x61,
      0x36, 0x2d, 0x62, 0x62, 0x33, 0x37, 0x2d, 0x34, 0x30, 0x32, 0x36, 0x2d,
      0x39, 0x37, 0x38, 0x35, 0x2d, 0x36, 0x35, 0x32, 0x66, 0x34, 0x34, 0x65,
      0x64, 0x63, 0x32, 0x62, 0x33, 0x43, 0x70, 0x6b, 0x3d, 0x62, 0x39, 0x30,
      0x31, 0x33, 0x30, 0x61, 0x33, 0x33, 0x38, 0x33, 0x38, 0x34, 0x31, 0x62,
      0x61, 0x39, 0x31, 0x31, 0x31, 0x61, 0x38, 0x38, 0x36, 0x33, 0x30, 0x66,
      0x30, 0x38, 0x66, 0x66, 0x32, 0x34, 0x61, 0x61, 0x63, 0x32, 0x62, 0x30,
      0x32, 0x64, 0x34, 0x63, 0x37, 0x33, 0x64, 0x63, 0x36, 0x33, 0x31, 0x33,
      0x30, 0x39, 0x37, 0x31, 0x30, 0x30, 0x61, 0x33, 0x62, 0x38, 0x34, 0x62,
      0x34, 0x28, 0x70, 0x73, 0x69, 0x3d, 0x43, 0x39, 0x38, 0x37, 0x45, 0x36,
      0x36, 0x44, 0x2d, 0x37, 0x32, 0x36, 0x45, 0x2d, 0x34, 0x36, 0x35, 0x35,
      0x2d, 0x39, 0x44, 0x36, 0x36, 0x2d, 0x43, 0x38, 0x39, 0x39, 0x41, 0x35,
      0x34, 0x32, 0x34, 0x32, 0x42, 0x30, 0x0e, 0x73, 0x72, 0x63, 0x76, 0x65,
      0x72, 0x73, 0x3d, 0x33, 0x32, 0x30, 0x2e, 0x32, 0x30, 0x04, 0x76, 0x76,
      0x3d, 0x32, 0xc0, 0xde, 0x00, 0x10, 0x80, 0x01, 0x00, 0x00, 0x11, 0x94,
      0x00, 0xb3, 0x0a, 0x63, 0x6e, 0x3d, 0x30, 0x2c, 0x31, 0x2c, 0x32, 0x2c,
      0x33, 0x07, 0x64, 0x61, 0x3d, 0x74, 0x72, 0x75, 0x65, 0x08, 0x65, 0x74,
      0x3d, 0x30, 0x2c, 0x33, 0x2c, 0x35, 0x13, 0x66, 0x74, 0x3d, 0x30, 0x78,
      0x35, 0x41, 0x37, 0x46, 0x46, 0x46, 0x46, 0x37, 0x2c, 0x30, 0x78, 0x43,
      0x44, 0x45, 0x08, 0x73, 0x66, 0x3d, 0x30, 0x78, 0x32, 0x34, 0x34, 0x08,
      0x6d, 0x64, 0x3d, 0x30, 0x2c, 0x31, 0x2c, 0x32, 0x0d, 0x61, 0x6d, 0x3d,
      0x41, 0x70, 0x70, 0x6c, 0x65, 0x54, 0x56, 0x35, 0x2c, 0x33, 0x43, 0x70,
      0x6b, 0x3d, 0x62, 0x39, 0x30, 0x31, 0x33, 0x30, 0x61, 0x33, 0x33, 0x38,
      0x33, 0x38, 0x34, 0x31, 0x62, 0x61, 0x39, 0x31, 0x31, 0x31, 0x61, 0x38,
      0x38, 0x36, 0x33, 0x30, 0x66, 0x30, 0x38, 0x66, 0x66, 0x32, 0x34, 0x61,
      0x61, 0x63, 0x32, 0x62, 0x30, 0x32, 0x64, 0x34, 0x63, 0x37, 0x33, 0x64,
      0x63, 0x36, 0x33, 0x31, 0x33, 0x30, 0x39, 0x37, 0x31, 0x30, 0x30, 0x61,
      0x33, 0x62, 0x38, 0x34, 0x62, 0x34, 0x06, 0x74, 0x70, 0x3d, 0x55, 0x44,
      0x50, 0x08, 0x76, 0x6e, 0x3d, 0x36, 0x35, 0x35, 0x33, 0x37, 0x09, 0x76,
      0x73, 0x3d, 0x33, 0x32, 0x30, 0x2e, 0x32, 0x30, 0x04, 0x76, 0x76, 0x3d,
      0x32, 0xc0, 0xb3, 0x00, 0x21, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00,
      0x08, 0x00, 0x00, 0x00, 0x00, 0x1b, 0x58, 0xc1, 0x8e, 0xc0, 0xde, 0x00,
      0x21, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x08, 0x00, 0x00, 0x00,
      0x00, 0x1b, 0x58, 0xc1, 0x8e, 0xc1, 0x8e, 0x00, 0x1c, 0x80, 0x01, 0x00,
      0x00, 0x00, 0x78, 0x00, 0x10, 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x04, 0x6e, 0xdf, 0x7f, 0x96, 0x90, 0x33, 0x7f, 0xc1, 0x8e, 0x00,
      0x01, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x04, 0x0a, 0x00, 0x00,
      0x6c, 0xc0, 0x56, 0x00, 0x2f, 0x80, 0x01, 0x00, 0x00, 0x11, 0x94, 0x00,
      0x09, 0xc0, 0x56, 0x00, 0x05, 0x00, 0x00, 0x80, 0x00, 0x40, 0xc0, 0xb3,
      0x00, 0x2f, 0x80, 0x01, 0x00, 0x00, 0x11, 0x94, 0x00, 0x09, 0xc0, 0xb3,
      0x00, 0x05, 0x00, 0x00, 0x80, 0x00, 0x40, 0xc0, 0xde, 0x00, 0x2f, 0x80,
      0x01, 0x00, 0x00, 0x11, 0x94, 0x00, 0x09, 0xc0, 0xde, 0x00, 0x05, 0x00,
      0x00, 0x80, 0x00, 0x40, 0xc1, 0x8e, 0x00, 0x2f, 0x80, 0x01, 0x00, 0x00,
      0x00, 0x78, 0x00, 0x08, 0xc1, 0x8e, 0x00, 0x04, 0x40, 0x00, 0x00, 0x08,
  };

  PacketReader reader(buffer);
  DnsMessage message;
  reader >> message;
  EXPECT_TRUE(reader.healthy());
  EXPECT_TRUE(reader.complete());
}

// Tests parsing of a message.
TEST(DnsReadingTest, SimpleQuestion) {
  /*
    header:
        id: 0
        flags: 0x0000
    questions:
    [0]
        name: _fuchsia._tcp.local.
        type: PTR
        class: IN
        unicast_response: 0
  */
  std::vector<uint8_t> buffer{
      0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x08, 0x5f, 0x66, 0x75, 0x63, 0x68, 0x73, 0x69,
      0x61, 0x04, 0x5f, 0x74, 0x63, 0x70, 0x05, 0x6c, 0x6f, 0x63,
      0x61, 0x6c, 0x00, 0x00, 0x0c, 0x00, 0x01,
  };

  PacketReader reader(buffer);
  DnsMessage message;
  reader >> message;
  EXPECT_TRUE(reader.healthy());
  EXPECT_TRUE(reader.complete());
}

// Tests parsing of an ill-formed message that prevously caused a crash.
// NET-1962 - issue discovered by fuzzer.
TEST(DnsReadingTest, RegressionNET1962) {
  std::vector<uint8_t> buffer{
      0x50, 0xf1, 0x09, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x10, 0x40, 0x02,
  };

  PacketReader reader(buffer);
  DnsMessage message;
  reader >> message;
  EXPECT_FALSE(reader.healthy());
  EXPECT_FALSE(reader.complete());
}

}  // test
}  // namespace mdns
