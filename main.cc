#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/pipeline.hpp>
#include "trace_categories.h"
#include <string>
#include <unistd.h>
#include <utility>
#include <memory>

#include "hw_pipeline.h"

int main() {
  auto tracing_session = StartTracing();

  // Give a custom name for the traced process.
  perfetto::ProcessTrack process_track = perfetto::ProcessTrack::Current();
  perfetto::protos::gen::TrackDescriptor desc = process_track.Serialize();
  desc.mutable_process()->set_process_name("Example");
  perfetto::TrackEvent::SetTrackDescriptor(process_track, desc);

  tutorial_multi_pipeline();

  StopTracing(std::move(tracing_session));
  return 0;
}
