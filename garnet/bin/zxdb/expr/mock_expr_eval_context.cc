// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/mock_expr_eval_context.h"

#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/expr/identifier.h"

namespace zxdb {

MockExprEvalContext::MockExprEvalContext()
    : data_provider_(fxl::MakeRefCounted<MockSymbolDataProvider>()),
      resolver_(data_provider_) {}

MockExprEvalContext::~MockExprEvalContext() = default;

void MockExprEvalContext::AddVariable(const std::string& name, ExprValue v) {
  values_[name] = v;
}

// ExprEvalContext implementation.
void MockExprEvalContext::GetNamedValue(
    const Identifier& ident,
    std::function<void(const Err&, fxl::RefPtr<Symbol>, ExprValue)> cb) {
  // Can ignore the symbol output for this test, it's not needed by the
  // expression evaluation system.
  auto found = values_.find(ident.GetFullName());
  if (found == values_.end())
    cb(Err("Not found"), nullptr, ExprValue());
  else
    cb(Err(), nullptr, found->second);
}

SymbolVariableResolver& MockExprEvalContext::GetVariableResolver() {
  return resolver_;
}

fxl::RefPtr<SymbolDataProvider> MockExprEvalContext::GetDataProvider() {
  return data_provider_;
}

}  // namespace zxdv
