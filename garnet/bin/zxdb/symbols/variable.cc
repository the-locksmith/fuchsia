// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/variable.h"

namespace zxdb {

Variable::Variable(int tag) : Value(tag) {}
Variable::Variable(int tag, const std::string& assigned_name, LazySymbol type,
                   VariableLocation location)
    : Value(tag, assigned_name, std::move(type)),
      location_(std::move(location)) {}
Variable::~Variable() = default;

const Variable* Variable::AsVariable() const { return this; }

}  // namespace zxdb
