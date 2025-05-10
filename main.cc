#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/pipeline.hpp>
#include "trace_categories.h"
#include <string>
#include <thread>
#include <unistd.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>
#include <atomic>
#include <memory>

#include "hw_pipeline.h"

int main() {
  auto tracing_session = StartTracing();

  // Give a custom name for the traced process.
  perfetto::ProcessTrack process_track = perfetto::ProcessTrack::Current();
  perfetto::protos::gen::TrackDescriptor desc = process_track.Serialize();
  desc.mutable_process()->set_process_name("Example");
  perfetto::TrackEvent::SetTrackDescriptor(process_track, desc);

  init_hardware_pool(); // 初始化硬件池

  tutorial_multi_pipeline();

  StopTracing(std::move(tracing_session));
  return 0;
}
