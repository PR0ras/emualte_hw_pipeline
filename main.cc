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

  tutorial_multi_pipeline();

  StopTracing(std::move(tracing_session));
  return 0;
}
