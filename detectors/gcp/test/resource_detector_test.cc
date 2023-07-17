// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#ifndef HAVE_CPP_STDLIB

#  include <chrono>
#  include <thread>

#  include "opentelemetry/detectors/gcp/resource_detector.h"
#  include "opentelemetry/ext/http/client/http_client_factory.h"
#  include "opentelemetry/sdk/resource/semantic_conventions.h"

#  include <gtest/gtest.h>
#  include "gmock/gmock.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace detector
{
namespace gcp
{
namespace
{

namespace sc = opentelemetry::sdk::resource::SemanticConventions;

template <typename T>
::testing::Matcher<std::pair<std::string, opentelemetry::sdk::common::OwnedAttributeValue>>
Attribute(std::string const &key, ::testing::Matcher<T const &> matcher)
{
  return ::testing::Pair(key, ::testing::VariantWith<T>(matcher));
}

TEST(GcpResourceDetector, IntegrationTest)
{
  using ::testing::AllOf;
  using ::testing::Contains;
  auto client = ext::http::client::HttpClientFactory::CreateSync();
  auto retry = internal::MakeDefaultRetry();

  auto detector  = internal::MakeGcpDetector(client, retry);
  auto resource = detector->Detect();
  auto attributes = resource.GetAttributes();
  EXPECT_THAT(attributes,
              AllOf(Contains(Attribute<std::string>(sc::kCloudProvider, "gcp")),
                    Contains(Attribute<std::string>(sc::kCloudPlatform, "gcp_compute_engine"))));
}

}  // namespace
}  // namespace gcp
}  // namespace detector
OPENTELEMETRY_END_NAMESPACE

#endif
