// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

// When communicating with an IHDA driver using zx_channel_call, do not use the
// IHDA_INVALID_TRANSACTION_ID as your message's transaction ID.
#define IHDA_INVALID_TRANSACTION_ID ((zx_txid_t)0)

// Invalid Stream ID and Stream TAG.  These values will never be returned as
// part of a successful REQUEST_STREAM response.
#define IHDA_INVALID_STREAM_ID  ((uint16_t)0)
#define IHDA_INVALID_STREAM_TAG ((uint8_t)0)

// Size of a register snapshot
#define IHDA_REGISTER_SNAPSHOT_SIZE (8u << 10)

__BEGIN_CDECLS

typedef uint32_t ihda_cmd_t;
#define IHDA_CMD_GET_IDS                  ((ihda_cmd_t)0x1000)
#define IHDA_CONTROLLER_CMD_SNAPSHOT_REGS ((ihda_cmd_t)0x2000)
#define IHDA_CODEC_SEND_CORB_CMD          ((ihda_cmd_t)0x3000)
#define IHDA_CODEC_REQUEST_STREAM         ((ihda_cmd_t)0x3001)
#define IHDA_CODEC_RELEASE_STREAM         ((ihda_cmd_t)0x3002)
#define IHDA_CODEC_SET_STREAM_FORMAT      ((ihda_cmd_t)0x3003)
#define IHDA_NOACK_FLAG                   ((ihda_cmd_t)0x80000000)
#define IHDA_CODEC_SEND_CORB_CMD_NOACK    ((ihda_cmd_t)(IHDA_NOACK_FLAG | IHDA_CODEC_SEND_CORB_CMD))
#define IHDA_CODEC_RELEASE_STREAM_NOACK   ((ihda_cmd_t)(IHDA_NOACK_FLAG | IHDA_CODEC_RELEASE_STREAM))

typedef struct ihda_cmd_hdr {
    zx_txid_t  transaction_id;
    ihda_cmd_t cmd;
} ihda_cmd_hdr_t;

// IHDA_CONTROLLER_CMD_GET_IDS
typedef struct ihda_get_ids_req {
    ihda_cmd_hdr_t hdr;
} ihda_get_ids_req_t;

typedef struct ihda_get_ids_resp {
    ihda_cmd_hdr_t  hdr;
    uint16_t        vid;
    uint16_t        did;
    uint8_t         ihda_vmaj;
    uint8_t         ihda_vmin;
    uint8_t         rev_id;
    uint8_t         step_id;
} ihda_get_ids_resp_t;

// IHDA_CONTROLLER_CMD_SNAPSHOT_REGS,
//
// Note: returns a snapshot of just the primary register file.  Alias registers
// are not included.
//
// TODO(johngro): When we have a way to return a handle to a read-only VMO which
// provides access to the actual PCI registers, do that instead of snapshotting
// and marshaling the result back.
typedef struct ihda_controller_snapshot_regs_req {
    ihda_cmd_hdr_t hdr;
} ihda_controller_snapshot_regs_req_t;

typedef struct ihda_controller_snapshot_regs_resp {
    ihda_cmd_hdr_t  hdr;
    uint8_t         snapshot[IHDA_REGISTER_SNAPSHOT_SIZE];
} ihda_controller_snapshot_regs_resp_t;

// IHDA_CODEC_SEND_CORB_CMD
//
// TODO(johngro) : minimize thread context switching by allowing more than one
// command to be queued per job.
//
typedef struct ihda_codec_send_corb_cmd_req {
    ihda_cmd_hdr_t hdr;
    uint32_t       verb;
    uint16_t       nid;
} ihda_codec_send_corb_cmd_req_t;

typedef struct ihda_codec_send_corb_cmd_resp {
    ihda_cmd_hdr_t  hdr;
    uint32_t        data;
    uint32_t        data_ex;
} ihda_codec_send_corb_cmd_resp_t;

// IHDA_CODEC_REQUEST_STREAM
//
typedef struct ihda_codec_request_stream_req {
    ihda_cmd_hdr_t  hdr;
    bool            input;          // true => input, false => output
} ihda_codec_request_stream_req_t;

typedef struct ihda_codec_request_stream_resp {
    ihda_cmd_hdr_t  hdr;
    zx_status_t     result;
    uint16_t        stream_id;
    uint8_t         stream_tag;
} ihda_codec_request_stream_resp_t;

// IHDA_CODEC_RELEASE_STREAM
//
typedef struct ihda_codec_release_stream_req {
    ihda_cmd_hdr_t  hdr;
    uint16_t        stream_id;
} ihda_codec_release_stream_req_t;

typedef struct ihda_codec_release_stream_resp {
    ihda_cmd_hdr_t  hdr;
} ihda_codec_release_stream_resp_t;

// IHDA_CODEC_SET_STREAM_FORMAT
//
typedef struct ihda_codec_set_stream_format_req {
    ihda_cmd_hdr_t  hdr;
    uint16_t        stream_id;
    uint16_t        format;         // see section 3.7.1
} ihda_codec_set_stream_format_req_t;

typedef struct ihda_codec_set_stream_format_resp {
    ihda_cmd_hdr_t  hdr;
} ihda_codec_set_stream_format_resp_t;

__END_CDECLS
