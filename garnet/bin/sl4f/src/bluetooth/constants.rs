// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Default advertising interval (ms) is 1000ms
pub const DEFAULT_BLE_ADV_INTERVAL_MS: u32 = 1000;

/// GATT Attribute Permission Values
pub const PERMISSION_READ_ENCRYPTED: u32 = 0x02;
pub const PERMISSION_READ_ENCRYPTED_MITM: u32 = 0x04;
pub const PERMISSION_WRITE_ENCRYPTED: u32 = 0x20;
pub const PERMISSION_WRITE_ENCRYPTED_MITM: u32 = 0x40;
pub const PERMISSION_WRITE_SIGNED: u32 = 0x80;
pub const PERMISSION_WRITE_SIGNED_MITM: u32 = 0x100;

/// GATT Attribute Property Values
pub const PROPERTY_NOTIFY: u32 = 0x10;
pub const PROPERTY_INDICATE: u32 = 0x20;
