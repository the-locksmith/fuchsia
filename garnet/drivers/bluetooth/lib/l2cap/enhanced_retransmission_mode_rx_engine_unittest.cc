// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "enhanced_retransmission_mode_rx_engine.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fragmenter.h"
#include "gtest/gtest.h"

namespace btlib {
namespace l2cap {
namespace internal {
namespace {

constexpr hci::ConnectionHandle kTestHandle = 0x0001;
constexpr ChannelId kTestChannelId = 0x0001;

using Engine = EnhancedRetransmissionModeRxEngine;

void NopTxCallback(common::ByteBufferPtr) {}

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduImmediatelyReturnsDataForUnsegmentedSdu) {
  // See Core Spec, v5, Vol 3, Part A, Table 3.2 for the first two bytes.
  const auto payload =
      common::CreateStaticByteBuffer(0, 0, 'h', 'e', 'l', 'l', 'o');
  const common::ByteBufferPtr sdu =
      Engine(NopTxCallback)
          .ProcessPdu(
              Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload));
  ASSERT_TRUE(sdu);
  EXPECT_TRUE(common::ContainersEqual(
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o'), *sdu));
}

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduCanHandleZeroBytePayload) {
  // See Core Spec, v5, Vol 3, Part A, Table 3.2 for the first two bytes.
  const auto payload = common::CreateStaticByteBuffer(0, 0);
  const common::ByteBufferPtr sdu =
      Engine(NopTxCallback)
          .ProcessPdu(
              Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload));
  ASSERT_TRUE(sdu);
  EXPECT_EQ(0u, sdu->size());
}

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduDoesNotGenerateSduForOutOfSequencePdu) {
  // See Core Spec, v5, Vol 3, Part A, Table 3.2 for the first two bytes.
  const auto payload = common::CreateStaticByteBuffer(  //
      1 << 1,                                           // TxSeq = 1, R=0
      0,                                                // SAR and ReqSeq
      'h', 'e', 'l', 'l', 'o');
  EXPECT_FALSE(Engine(NopTxCallback)
                   .ProcessPdu(Fragmenter(kTestHandle)
                                   .BuildBasicFrame(kTestChannelId, payload)));
}

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduAdvancesSequenceNumberOnInSequenceFrame) {
  Engine rx_engine(NopTxCallback);

  // Send with sequence 0.
  {
    const auto payload = common::CreateStaticByteBuffer(  //
        0 << 1,                                           // TxSeq=0, R=0
        0,                                                // SAR and ReqSeq
        'h', 'e', 'l', 'l', 'o');
    ASSERT_TRUE(rx_engine.ProcessPdu(
        Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload)));
  }

  // Send with sequence 1.
  {
    const auto payload = common::CreateStaticByteBuffer(  //
        1 << 1,                                           // TxSeq=1, R=0
        0,                                                // SAR and ReqSeq
        'h', 'e', 'l', 'l', 'o');
    ASSERT_TRUE(rx_engine.ProcessPdu(
        Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload)));
  }

  // Send with sequence 2.
  {
    const auto payload = common::CreateStaticByteBuffer(  //
        2 << 1,                                           // TxSeq=2, R=0
        0,                                                // SAR and ReqSeq
        'h', 'e', 'l', 'l', 'o');
    EXPECT_TRUE(rx_engine.ProcessPdu(
        Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload)));
  }
}

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduRollsOverSequenceNumber) {
  Engine rx_engine(NopTxCallback);
  auto payload = common::CreateStaticByteBuffer(  //
      0 << 1,                                     // TxSeq=0, R=0
      0,                                          // SAR and ReqSeq
      'h', 'e', 'l', 'l', 'o');
  for (size_t i = 0; i < 64; ++i) {
    payload[0] = i << 1;  // Set TxSeq
    ASSERT_TRUE(rx_engine.ProcessPdu(
        Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload)))
        << " (i=" << i << ")";
  }

  // Per Core Spec v5, Vol 3, Part A, Sec 8.3, the sequence number should now
  // roll over to 0.
  payload[0] = 0 << 1;
  EXPECT_TRUE(rx_engine.ProcessPdu(
      Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload)));
}

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduDoesNotAdvanceSequenceNumberForOutOfSequencePdu) {
  Engine rx_engine(NopTxCallback);
  const auto out_of_seq = common::CreateStaticByteBuffer(  //
      1 << 1,                                              // TxSeq=1, R=0
      0,                                                   // SAR and ReqSeq
      'h', 'e', 'l', 'l', 'o');
  ASSERT_FALSE(rx_engine.ProcessPdu(
      Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, out_of_seq)));

  const auto in_seq = common::CreateStaticByteBuffer(  //
      0 << 1,                                          // TxSeq=0, R=0
      0,                                               // SAR and ReqSeq
      'h', 'e', 'l', 'l', 'o');
  EXPECT_TRUE(rx_engine.ProcessPdu(
      Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, in_seq)));
}

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduImmediatelyAcksUnsegmentedSdu) {
  size_t n_acks = 0;
  common::ByteBufferPtr outbound_ack;
  auto tx_callback = [&](auto pdu) {
    outbound_ack = std::move(pdu);
    ++n_acks;
  };

  // See Core Spec, v5, Vol 3, Part A, Table 3.2 for the first two bytes.
  const auto payload =
      common::CreateStaticByteBuffer(0, 0, 'h', 'e', 'l', 'l', 'o');
  ASSERT_TRUE(Engine(tx_callback)
                  .ProcessPdu(Fragmenter(kTestHandle)
                                  .BuildBasicFrame(kTestChannelId, payload)));
  EXPECT_EQ(1u, n_acks);
  ASSERT_TRUE(outbound_ack);
  ASSERT_EQ(sizeof(SimpleReceiverReadyFrame), outbound_ack->size());

  auto ack_frame =
      *reinterpret_cast<const SimpleReceiverReadyFrame *>(outbound_ack->data());
  EXPECT_EQ(SupervisoryFunction::ReceiverReady, ack_frame.function());
  EXPECT_EQ(1u, ack_frame.request_seq_num());
}

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduSendsCorrectReqSeqOnRollover) {
  size_t n_acks = 0;
  common::ByteBufferPtr last_ack;
  auto tx_callback = [&](auto pdu) {
    last_ack = std::move(pdu);
    ++n_acks;
  };

  Engine rx_engine(tx_callback);
  // See Core Spec, v5, Vol 3, Part A, Table 3.2 for the first two bytes.
  for (size_t i = 0; i < 64; ++i) {
    const auto payload =
        common::CreateStaticByteBuffer(i << 1, 0, 'h', 'e', 'l', 'l', 'o');
    ASSERT_TRUE(rx_engine.ProcessPdu(
        Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload)))
        << " (i=" << i << ")";
  }
  EXPECT_EQ(64u, n_acks);
  ASSERT_TRUE(last_ack);
  ASSERT_EQ(sizeof(SimpleReceiverReadyFrame), last_ack->size());

  auto ack_frame =
      *reinterpret_cast<const SimpleReceiverReadyFrame *>(last_ack->data());
  EXPECT_EQ(SupervisoryFunction::ReceiverReady, ack_frame.function());
  EXPECT_EQ(0u, ack_frame.request_seq_num());
}

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduDoesNotAckOutOfSequenceFrame) {
  size_t n_acks = 0;
  common::ByteBufferPtr outbound_ack;
  auto tx_callback = [&](auto pdu) {
    outbound_ack = std::move(pdu);
    ++n_acks;
  };

  // See Core Spec, v5, Vol 3, Part A, Table 3.2 for the first two bytes.
  const auto payload =
      common::CreateStaticByteBuffer(1, 0, 'h', 'e', 'l', 'l', 'o');

  // Per Core Spec, v5, Vol 3, Part A, Sec 8.4.7.1, receipt of an
  // out-of-sequence frame should cause us to transmit a Reject frame. We assume
  // that we should _not_ also transmit a ReceiverReady frame.
  //
  // TODO(BT-448): Revise this test when we start sending Reject frames.
  ASSERT_FALSE(Engine(tx_callback)
                   .ProcessPdu(Fragmenter(kTestHandle)
                                   .BuildBasicFrame(kTestChannelId, payload)));
  EXPECT_EQ(0u, n_acks);
}

}  // namespace
}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
