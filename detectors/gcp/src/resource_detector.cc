// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/detectors/gcp/resource_detector.h"
#include "opentelemetry/detectors/gcp/internal/resource_detector_impl.h"
#include "opentelemetry/ext/http/client/http_client_factory.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace detector
{
namespace gcp
{

std::unique_ptr<sdk::resource::ResourceDetector> MakeGcpDetector(GcpDetectorOptions options)
{
  return internal::MakeGcpDetector(ext::http::client::HttpClientFactory::CreateSync(),
                                   internal::MakeDefaultRetry(), std::move(options));
}

}  // namespace gcp
}  // namespace detector
OPENTELEMETRY_END_NAMESPACE
