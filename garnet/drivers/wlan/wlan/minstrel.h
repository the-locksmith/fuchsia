// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_WLAN_WLAN_MINSTREL_H_
#define GARNET_DRIVERS_WLAN_WLAN_MINSTREL_H_

#include "probe_sequence.h"

#include <wlan/common/macaddr.h>
#include <wlan/common/tx_vector.h>
#include <wlan/mlme/client/station.h>
#include <wlan/mlme/timer_manager.h>
#include <wlan/protocol/info.h>
#include <wlan/protocol/mac.h>

#include <fbl/unique_ptr.h>
#include <zx/time.h>

#include <fuchsia/wlan/minstrel/cpp/fidl.h>

#include <array>
#include <unordered_map>
#include <unordered_set>

namespace wlan {

static constexpr uint16_t kMinstrelFrameLength = 1400;  // bytes
static constexpr float kMinstrelExpWeight = 0.75;  // Used to calculate moving average throughput
static constexpr float kMinstrelProbabilityThreshold =
    0.9;  // If probability is past this level, only consider throughput
static constexpr uint8_t kProbeInterval = 16;  // normal packets to sent between two probe packets

// LINT.IfChange
struct TxStats {
    tx_vec_idx_t tx_vector_idx;
    zx::duration perfect_tx_time;
    size_t success_cur = 0;   // successful transmissions since last update.
    size_t attempts_cur = 0;  // transmission attempts since last update.
    float probability =
        1.0f - kMinstrelProbabilityThreshold;  // Moving Average Probability of success.
    float cur_tp = 0.0;                        // Expected average throughput.
    size_t success_total = 0;                  // cumulative success counts.
    size_t attempts_total = 0;                 // cumulative attempts.
    size_t probes_total = 0;
    // Delay probe if probability < (1 - kMinstrelProbabilityThreshold).
    uint8_t probe_cycles_skipped = 0;

    constexpr bool PhyPreferredOver(const TxStats& other) const;
    constexpr bool ThroughputHigherThan(const TxStats& other) const;
    constexpr bool ProbabilityHigherThan(const TxStats& other) const;
    ::fuchsia::wlan::minstrel::StatsEntry ToFidl() const;
};

struct Peer {
    common::MacAddr addr;
    bool is_ht = false;
    bool is_vht = false;

    std::unique_ptr<std::mutex> update_lock = std::make_unique<std::mutex>();
    std::unordered_map<tx_vec_idx_t, TxStats> tx_stats_map __TA_GUARDED(*update_lock);
    std::unordered_set<tx_vec_idx_t> basic_rates;

    tx_vec_idx_t basic_highest = kErpStartIdx;  // will be replaced when assoc_ctx is parsed.
    tx_vec_idx_t basic_max_probability = kErpStartIdx;  // optimality based on success probability
    // Index of the optimal tx vector
    tx_vec_idx_t max_tp = kInvalidTxVectorIdx;           // optimality based on expected throughput.
    tx_vec_idx_t max_probability = kInvalidTxVectorIdx;  // optimality based on success probability.

    ProbeSequence::Entry probe_entry;
    uint8_t num_probe_cycles_done = 0;  // +1 when all tx_vectors are probed at least once.
    uint8_t num_pkt_until_next_probe = kProbeInterval - 1;
    size_t probes;
};
// LINT.ThenChange(//sdk/fidl/fuchsia.wlan.minstrel/wlan_minstrel.fidl)

class MinstrelRateSelector {
   public:
    MinstrelRateSelector(fbl::unique_ptr<Timer>&& timer, ProbeSequence&& probe_sequence,
                         zx::duration update_interval);
    void AddPeer(const wlan_assoc_ctx_t& assoc_ctx);
    void RemovePeer(const common::MacAddr& addr);
    // Called after every tx packet.
    void HandleTxStatusReport(const wlan_tx_status_t& tx_status);
    bool HandleTimeout();

    tx_vec_idx_t GetTxVectorIdx(const FrameControl& fc, const common::MacAddr& peer_addr,
                                uint32_t flags);
    zx_status_t GetListToFidl(::fuchsia::wlan::minstrel::Peers* peers_fidl) const;
    zx_status_t GetStatsToFidl(const common::MacAddr& peer_addr,
                               ::fuchsia::wlan::minstrel::Peer* peer_fidl) const;
    bool IsActive() const;

   private:
    void UpdateStats();
    Peer* GetPeer(const common::MacAddr& addr);
    const Peer* GetPeer(const common::MacAddr& addr) const;

    // Holds MAC addresses of peers with at least one status report but has not been processed.
    std::unordered_set<common::MacAddr, common::MacAddrHasher> outdated_peers_;
    std::unordered_map<common::MacAddr, Peer, common::MacAddrHasher> peer_map_;
    TimerManager<> timer_mgr_;
    TimeoutId next_update_;

    const ProbeSequence probe_sequence_;
    const zx::duration update_interval_;
};

ProbeSequence RandomProbeSequence();

namespace debug {
std::string Describe(const TxStats& tx_stats);
}  // namespace debug

}  // namespace wlan

#endif  // GARNET_DRIVERS_WLAN_WLAN_MINSTREL_H_
