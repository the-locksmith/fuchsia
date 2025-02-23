// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FIDL_BUFFER_SET_H_
#define GARNET_BIN_MEDIAPLAYER_FIDL_BUFFER_SET_H_

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fuchsia/mediacodec/cpp/fidl.h>
#include "garnet/bin/mediaplayer/graph/payloads/payload_allocator.h"
#include "garnet/bin/mediaplayer/graph/payloads/payload_buffer.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/fxl/synchronization/thread_checker.h"

namespace media_player {

// A set of buffers associated with a specific StreamBufferSettings and
// buffer lifetime ordinal.
//
// This class is thread-safe.
class BufferSet : public fbl::RefCounted<BufferSet> {
 public:
  // Creates a buffer set with the specified settings and lifetime ordinal.
  // |single_vmo| indicates whether the buffers should be allocated from a
  // single VMO (true) or a VMO per buffer.
  static fbl::RefPtr<BufferSet> Create(
      const fuchsia::media::StreamBufferSettings& settings,
      uint64_t lifetime_ordinal, bool single_vmo);

  BufferSet(const fuchsia::media::StreamBufferSettings& settings,
            uint64_t lifetime_ordinal, bool single_vmo);

  ~BufferSet();

  // Gets the settings for this buffer set. The |buffer_lifetime_ordinal| of
  // settings is set to the |lifetime_ordinal| value passed into the
  // constructor.
  const fuchsia::media::StreamBufferSettings& settings() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return settings_;
  }

  // Sets the value passed into the constructor as |single_vmo|.
  bool single_vmo() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return single_vmo_;
  }

  // Returns the buffer lifetime ordinal passed to the constructor.
  uint64_t lifetime_ordinal() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return settings_.buffer_lifetime_ordinal;
  }

  // Returns the size in bytes of the buffers in this set.
  uint32_t buffer_size() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return settings_.per_packet_buffer_bytes;
  }

  // Returns the number of buffers in the set.
  uint32_t buffer_count() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return buffers_.size();
  }

  // Returns the number of free buffers.
  uint32_t free_buffer_count() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return free_buffer_count_;
  }

  // Returns a |StreamBuffer| struct for the specified buffer. |writeable|
  // determines whether the vmo handle in the descriptor should have write
  // permission.
  fuchsia::media::StreamBuffer GetBufferDescriptor(
      uint32_t buffer_index, bool writeable,
      const PayloadVmos& payload_vmos) const;

  // Allocates a buffer.
  fbl::RefPtr<PayloadBuffer> AllocateBuffer(uint64_t size,
                                            const PayloadVmos& payload_vmos);

  // Creates a payload buffer on behalf of the outboard decoder and stores
  // a reference to it. The reference may be released with
  // |ReleaseBufferForDecoder| or |ReleaseAllDecoderOwnedBuffers|.
  void CreateBufferForDecoder(uint32_t buffer_index,
                              const PayloadVmos& payload_vmos);

  // Adds a reference to the payload buffer on behalf of the outboard decoder.
  // |payload_buffer| cannot be null. This version is used when the client
  // has a reference to the |PayloadBuffer| already.
  void AddRefBufferForDecoder(uint32_t buffer_index,
                              fbl::RefPtr<PayloadBuffer> payload_buffer);

  // Takes a reference to the payload buffer previously added using
  // |AddRefBufferForDecoder| or |AllocateAllBuffersForDecoder| and returns a
  // reference to the |PayloadBuffer|.
  fbl::RefPtr<PayloadBuffer> TakeBufferFromDecoder(uint32_t buffer_index);

  // Gets a new reference to a buffer already owned by the outboard decoder.
  fbl::RefPtr<PayloadBuffer> GetDecoderOwnedBuffer(uint32_t buffer_index);

  // Allocates all buffers for the outboard decoder. All buffers must be free
  // when this method is called.
  void AllocateAllBuffersForDecoder(const PayloadVmos& payload_vmos);

  // Releases all buffers currently owned by the output decoder.
  void ReleaseAllDecoderOwnedBuffers();

  // Returns true if there's a free buffer, otherwise calls |callback| on an
  // arbirary thread when one becomes free. The pending action can be cancelled
  // by calling |CancelPendingFreeBufferCallback|.
  bool HasFreeBuffer(fit::closure callback);

  // Indicates that this |BufferSet| has been parked in favor of a new one.
  // After decommissioning and When all its buffers have been recycled, the
  // buffer set will be deleted.
  void Decommission();

 private:
  // The current state of a buffer in the set.
  struct BufferInfo {
    // Indicates whether the buffer is free. |decoder_ref_| must be false if
    // this field is true.
    bool free_ = true;

    // This field is non-null for buffers that are currently owned by the
    // outboard decoder.
    fbl::RefPtr<PayloadBuffer> decoder_ref_;
  };

  // Gets the |PayloadVmo| for the specified index.
  fbl::RefPtr<PayloadVmo> BufferVmo(size_t buffer_index,
                                    const PayloadVmos& payload_vmos) const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Creates a |PayloadBuffer| for the indicated |buffer_index|.
  fbl::RefPtr<PayloadBuffer> CreateBuffer(
      uint32_t buffer_index,
      const std::vector<fbl::RefPtr<PayloadVmo>>& payload_vmos)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  mutable std::mutex mutex_;

  fuchsia::media::StreamBufferSettings settings_ FXL_GUARDED_BY(mutex_);
  bool single_vmo_ FXL_GUARDED_BY(mutex_);
  std::vector<BufferInfo> buffers_ FXL_GUARDED_BY(mutex_);

  // |suggest_next_to_allocate_| suggests the next buffer to allocate. When
  // allocating a buffer, a sequential search for a free buffer starts at this
  // index, and this index is left referring to the buffer after the allocated
  // buffer (with wraparound). Given the normally FIFO behavior of the caller,
  // only one increment is typically required per allocation. This approach
  // tends to allocate the buffers in a round-robin fashion.
  uint32_t suggest_next_to_allocate_ FXL_GUARDED_BY(mutex_) = 0;
  uint32_t free_buffer_count_ FXL_GUARDED_BY(mutex_) = 0;

  fit::closure free_buffer_callback_ FXL_GUARDED_BY(mutex_);

  // Disallow copy and assign.
  BufferSet(const BufferSet&) = delete;
  BufferSet& operator=(const BufferSet&) = delete;
};

// Manages a sequence of buffer sets.
//
// This class is not thread-safe. The constructor, desctructor and all methods
// must be called on the same thread.
class BufferSetManager {
 public:
  BufferSetManager() = default;

  ~BufferSetManager() {
    FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  };

  // Determines whether this has a current buffer set.
  bool has_current_set() const {
    FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
    return !!current_set_;
  }

  // The current buffer set. Do not call this method when |has_current| returns
  // false.
  BufferSet& current_set() {
    FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
    FXL_DCHECK(current_set_);
    return *current_set_;
  }

  // Applies the specified constraints, creating a new buffer set. If
  // |single_vmo_preferred| and |single_buffer_mode_allowed| are true, one vmo
  // will be used for all the new buffers. Otherwise, each new buffer will have
  // its own vmo. The resulting set's |single_vmo| method with return true in
  // former case, false in the latter.
  void ApplyConstraints(
      const fuchsia::media::StreamBufferConstraints& constraints,
      bool single_vmo_preferred);

  // Releases a reference to the payload buffer previously added using
  // |BufferSet::AddRefBufferForDecoder| or
  // |BufferSet::AllocateAllBuffersForDecoder|.
  void ReleaseBufferForDecoder(uint64_t lifetime_ordinal,
                               uint32_t buffer_index);

 private:
  FXL_DECLARE_THREAD_CHECKER(thread_checker_);

  // The current |BufferSet| this is only null when |ApplyConstraints| has
  // never been called. It's important not to clear this arbitrarily, because
  // that would prevent the buffer lifetime ordinals from progressing correctly.
  fbl::RefPtr<BufferSet> current_set_;

  // Disallow copy and assign.
  BufferSetManager(const BufferSetManager&) = delete;
  BufferSetManager& operator=(const BufferSetManager&) = delete;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FIDL_BUFFER_SET_H_
