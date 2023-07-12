// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "opentelemetry/detectors/gcp/internal/resource_detector_impl.h"
#include "opentelemetry/ext/http/client/http_client_factory.h"
#include "opentelemetry/sdk/resource/resource_detector.h"

#include <nlohmann/json.hpp>

OPENTELEMETRY_BEGIN_NAMESPACE
namespace detector
{
namespace gcp
{

class GcpResourceDetector final : public opentelemetry::sdk::resource::ResourceDetector
{
public:
  opentelemetry::sdk::resource::Resource Detect() override
  {
    if (attributes_.empty())
    {
      auto result = RetryLoop(client_, std::make_shared<DefaultRetry>());
      if (!result.error.empty())
      {
        OTEL_INTERNAL_LOG_INFO("Could not query the metadata server. status=" + result.error +
                               "\n");
        return opentelemetry::sdk::resource::Resource::GetEmpty();
      }
      Parser parser(std::move(result.json));
      attributes_ = std::move(parser).Attributes();
    }
    return opentelemetry::sdk::resource::Resource::Create(attributes_);
  }

private:
  std::shared_ptr<ext::http::client::HttpClientSync> client_ =
      ext::http::client::HttpClientFactory::CreateSync();
  nlohmann::json json_;
  opentelemetry::sdk::resource::ResourceAttributes attributes_;
};

}  // namespace gcp
}  // namespace detector
OPENTELEMETRY_END_NAMESPACE
