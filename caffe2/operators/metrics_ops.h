#pragma once

#include "caffe2/core/context.h"
#include "caffe2/core/operator.h"
#include "caffe2/core/timer.h"

#include <mutex>

namespace caffe2 {
namespace {
struct QPSMetricState {
  Timer lifetimeTimer;
  Timer windowTimer;
  int64_t windowExamples{0};
  int64_t lifetimeExamples{0};

  std::mutex mutex;
};
}

CAFFE_KNOWN_TYPE(std::unique_ptr<QPSMetricState>);

// TODO(amalevich): Consider making all the code below templated, so it'll be
// easier to share it across different metrics.
class CreateQPSMetricOp final : public Operator<CPUContext> {
 public:
  using Operator<CPUContext>::Operator;

  bool RunOnDevice() override {
    *OperatorBase::Output<std::unique_ptr<QPSMetricState>>(0) =
        caffe2::make_unique<QPSMetricState>();
    return true;
  }
};

class QPSMetricOp final : public Operator<CPUContext> {
 public:
  using Operator<CPUContext>::Operator;

  bool RunOnDevice() override {
    auto& metricsBlob =
        *OperatorBase::Input<std::unique_ptr<QPSMetricState>>(0);
    auto examples = Input(1).dim(0);
    // All changes to metrics should happen under critical section.
    {
      std::lock_guard<std::mutex> guard(metricsBlob.mutex);
      metricsBlob.windowExamples += examples;
      metricsBlob.lifetimeExamples += examples;
    }
    return true;
  }
};

class QPSMetricReportOp final : public Operator<CPUContext> {
 public:
  using Operator<CPUContext>::Operator;

  bool RunOnDevice() override {
    auto& metricsBlob =
        *OperatorBase::Input<std::unique_ptr<QPSMetricState>>(0);
    // All changes to metrics should happen under critical section.
    float windowSeconds = -1;
    int64_t windowExamples = 0;
    float lifetimeSeconds = -1;
    int64_t lifetimeExamples = 0;
    {
      std::lock_guard<std::mutex> guard(metricsBlob.mutex);
      windowSeconds = metricsBlob.windowTimer.Seconds();
      lifetimeSeconds = metricsBlob.lifetimeTimer.Seconds();
      windowExamples = metricsBlob.windowExamples;
      lifetimeExamples = metricsBlob.lifetimeExamples;

      metricsBlob.windowTimer.Start();
      metricsBlob.windowExamples = 0;
    }
    // TODO(amalevich): Add output blobs, so it would be relatively easy to
    // access this metrics from the outside
    LOG(INFO) << "Overal QPS = "
              << (static_cast<double>(lifetimeExamples) / lifetimeSeconds)
              << ", Window QPS = "
              << (static_cast<double>(windowExamples) / windowSeconds);
    return true;
  }
};
}
