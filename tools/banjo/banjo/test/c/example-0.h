// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.example0 banjo file

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct bar bar_t;
typedef struct foo foo_t;

// Declarations

struct foo {
    bar_t b;
};

struct bar {
    foo_t* f;
};

__END_CDECLS;
