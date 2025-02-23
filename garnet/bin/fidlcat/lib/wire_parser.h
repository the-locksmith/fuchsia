// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_FIDLCAT_LIB_WIRE_PARSER_H_
#define GARNET_BIN_FIDLCAT_LIB_WIRE_PARSER_H_

#include "library_loader.h"

namespace fidlcat {

// Given a wire-formatted |message| and a schema for that message represented by
// |method|, populates |request| with JSON representing that request.
//
// Returns false if it cannot decode the message using the metadata associated
// with the method.
bool RequestToJSON(const InterfaceMethod* method, const fidl::Message& message,
                   rapidjson::Document& request);

}  // namespace fidlcat

#endif  // GARNET_BIN_FIDLCAT_LIB_WIRE_PARSER_H_
