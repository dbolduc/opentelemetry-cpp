// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "opentelemetry/detectors/gcp/resource_detector_options.h"
#include "opentelemetry/ext/http/client/http_client_factory.h"
#include "opentelemetry/sdk/resource/resource_detector.h"

#include <deque>
#include <nlohmann/json.hpp>
#include <thread>

OPENTELEMETRY_BEGIN_NAMESPACE
namespace detector
{
namespace gcp
{
// The internal namespace is for implementation details only. The symbols within are not part of the
// public API. They are subject to change, including deletion, without notice.
namespace internal {

// Interface to simplify testing. The default will sleep.
class Retry
{
public:
  // Returns `true` if we should keep retrying, `false` if we should stop retrying.
  virtual bool OnRetry() = 0;
};

/**
 * Creates a default retry policy
 *
 * The policy is to sleep for 1s, then 2s, then 4s, then give up.
 */
std::shared_ptr<Retry> MakeDefaultRetry();

// In tests, we mock the client and the retry policy.
std::unique_ptr<sdk::resource::ResourceDetector> MakeGcpDetector(
    std::shared_ptr<ext::http::client::HttpClientSync> client,
    std::shared_ptr<Retry> retry,
    GcpDetectorOptions options = {});

}  // namespace internal
}  // namespace gcp
}  // namespace detector
OPENTELEMETRY_END_NAMESPACE
