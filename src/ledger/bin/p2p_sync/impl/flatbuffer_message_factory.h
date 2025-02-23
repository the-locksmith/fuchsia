// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_SYNC_IMPL_FLATBUFFER_MESSAGE_FACTORY_H_
#define SRC_LEDGER_BIN_P2P_SYNC_IMPL_FLATBUFFER_MESSAGE_FACTORY_H_

#include <flatbuffers/flatbuffers.h>
#include <lib/fxl/strings/string_view.h>

#include "src/ledger/bin/p2p_sync/impl/message_generated.h"

namespace p2p_sync {

void CreateUnknownResponseMessage(flatbuffers::FlatBufferBuilder* buffer,
                                  fxl::StringView namespace_id,
                                  fxl::StringView page_id,
                                  ResponseStatus status);
}  // namespace p2p_sync

#endif  // SRC_LEDGER_BIN_P2P_SYNC_IMPL_FLATBUFFER_MESSAGE_FACTORY_H_
