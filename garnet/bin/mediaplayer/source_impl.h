// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_SOURCE_IMPL_H_
#define GARNET_BIN_MEDIAPLAYER_SOURCE_IMPL_H_

#include <fuchsia/mediaplayer/cpp/fidl.h>
#include <vector>
#include "garnet/bin/mediaplayer/core/demux_source_segment.h"
#include "garnet/bin/mediaplayer/core/stream_source_segment.h"
#include "garnet/bin/mediaplayer/demux/demux.h"
#include "garnet/bin/mediaplayer/graph/graph.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"

namespace media_player {

// Base class for fidl agents that represent a source of content that may be
// played.
class SourceImpl {
 public:
  SourceImpl(Graph* graph, fit::closure connection_failure_callback);

  virtual ~SourceImpl();

  // Removes and returns the |SourceSegment| hosted by this |SourceImpl|.
  virtual std::unique_ptr<SourceSegment> TakeSourceSegment() = 0;

 protected:
  // Completes construction.
  void CompleteConstruction(SourceSegment* source_segment);

  // Sends status updates to clients.
  virtual void SendStatusUpdates();

  // Returns the current status of the source.
  fuchsia::mediaplayer::SourceStatus& status() { return status_; }

  // Clears |source_segment_|, |streams_| and |status_|.
  void Clear();

  // Calls the remove callback, if there is one.
  void Remove();

 private:
  struct Stream {
    std::unique_ptr<StreamType> stream_type_;
    OutputRef output_;
  };

  // Handles a stream update from the source segment.
  void OnStreamUpdated(size_t index, const SourceSegment::Stream& stream);

  // Handles a stream removal from the source segment.
  void OnStreamRemoved(size_t index);

  // Updates |status_|.
  void UpdateStatus();

  SourceSegment* source_segment_;
  Graph* graph_;
  fit::closure connection_failure_callback_;
  async_dispatcher_t* dispatcher_;

  // TODO(dalesat): Do we really need to maintain this or can we just have an
  // abstract GetStreams()?
  std::vector<Stream> streams_;

  fuchsia::mediaplayer::SourceStatus status_;
};

////////////////////////////////////////////////////////////////////////////////
// DemuxSourceImpl declaration.

// |SourceImpl| that hosts a |DemuxSourceSegment|.
class DemuxSourceImpl : public SourceImpl, public fuchsia::mediaplayer::Source {
 public:
  // Creates a |DemuxSourceImpl|. |request| is optional.
  // |connection_failure_callback|, which is also optional, allows the source
  // to signal that its connection has failed.
  static std::unique_ptr<DemuxSourceImpl> Create(
      std::shared_ptr<Demux> demux, Graph* graph,
      fidl::InterfaceRequest<fuchsia::mediaplayer::Source> request,
      fit::closure connection_failure_callback);

  DemuxSourceImpl(std::shared_ptr<Demux> demux, Graph* graph,
                  fidl::InterfaceRequest<fuchsia::mediaplayer::Source> request,
                  fit::closure connection_failure_callback);

  ~DemuxSourceImpl() override;

  // SourceImpl overrides.
  std::unique_ptr<SourceSegment> TakeSourceSegment() override;

  void SendStatusUpdates() override;

  // Source implementation (Source has no methods).

 private:
  std::shared_ptr<Demux> demux_;
  fidl::Binding<fuchsia::mediaplayer::Source> binding_;

  std::unique_ptr<DemuxSourceSegment> demux_source_segment_;
};

////////////////////////////////////////////////////////////////////////////////
// StreamSourceImpl declaration.

// |SourceImpl| that hosts a |StreamSourceSegment|.
class StreamSourceImpl : public SourceImpl,
                         public fuchsia::mediaplayer::StreamSource {
 public:
  // Creates a |StreamSourceImpl|. |request| is required.
  // |connection_failure_callback|, which is also optional, allows the source
  // to signal that its connection has failed.
  static std::unique_ptr<StreamSourceImpl> Create(
      int64_t duration_ns, bool can_pause, bool can_seek,
      std::unique_ptr<fuchsia::mediaplayer::Metadata> metadata, Graph* graph,
      fidl::InterfaceRequest<fuchsia::mediaplayer::StreamSource> request,
      fit::closure connection_failure_callback);

  StreamSourceImpl(
      int64_t duration_ns, bool can_pause, bool can_seek,
      std::unique_ptr<fuchsia::mediaplayer::Metadata> metadata, Graph* graph,
      fidl::InterfaceRequest<fuchsia::mediaplayer::StreamSource> request,
      fit::closure connection_failure_callback);

  ~StreamSourceImpl() override;

  // SourceImpl overrides.
  std::unique_ptr<SourceSegment> TakeSourceSegment() override;

  void SendStatusUpdates() override;

  // StreamSource implementation.
  void AddStream(fuchsia::media::StreamType type,
                 uint32_t tick_per_second_numerator,
                 uint32_t tick_per_second_denominator,
                 ::fidl::InterfaceRequest<fuchsia::media::SimpleStreamSink>
                     simple_stream_sink_request) override;

  void AddBinding(fidl::InterfaceRequest<fuchsia::mediaplayer::StreamSource>
                      stream_source_request) override;

 private:
  void AddBindingInternal(
      fidl::InterfaceRequest<fuchsia::mediaplayer::StreamSource>
          stream_source_request);

  fidl::BindingSet<fuchsia::mediaplayer::StreamSource> bindings_;

  std::unique_ptr<StreamSourceSegment> stream_source_segment_;
  StreamSourceSegment* stream_source_segment_raw_ptr_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_SOURCE_IMPL_H_
