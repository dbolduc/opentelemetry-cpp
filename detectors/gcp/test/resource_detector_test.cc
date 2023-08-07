// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#ifndef HAVE_CPP_STDLIB
#  ifndef ENABLE_HTTP_SSL_PREVIEW

#    include <chrono>
#    include <thread>

#    include "opentelemetry/detectors/gcp/resource_detector.h"
#    include "opentelemetry/ext/http/client/http_client_factory.h"
#    include "opentelemetry/sdk/common/env_variables.h"
#    include "opentelemetry/sdk/common/global_log_handler.h"
#    include "opentelemetry/sdk/resource/semantic_conventions.h"

#    include <gtest/gtest.h>
#    include "gmock/gmock.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace detector
{
namespace gcp
{
namespace
{

namespace sc = opentelemetry::sdk::resource::SemanticConventions;
using ::testing::_;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsSupersetOf;
using ::testing::Matcher;
using ::testing::Pair;
using ::testing::StrCaseEq;
using ::testing::StrEq;
using ::testing::VariantWith;

#if defined(_MSC_VER)
using opentelemetry::sdk::common::setenv;
using opentelemetry::sdk::common::unsetenv;
#endif

template <typename T>
Matcher<std::pair<std::string, opentelemetry::sdk::common::OwnedAttributeValue>>
Attribute(std::string const &key, Matcher<T const &> matcher)
{
  return Pair(key, VariantWith<T>(matcher));
}

// A mock log handler to check whether log messages with a specific level were emitted.
struct MockLogHandler : public sdk::common::internal_log::LogHandler
{
  using Message = std::pair<sdk::common::internal_log::LogLevel, std::string>;

  void Handle(sdk::common::internal_log::LogLevel level,
              const char * /*file*/,
              int /*line*/,
              const char *msg,
              const sdk::common::AttributeMap & /*attributes*/) noexcept override
  {
    messages.emplace_back(level, msg);
  }

  std::vector<Message> messages;
};

class ScopedLog {
  public:
    ScopedLog()
        : handler_(new MockLogHandler()),
          previous_handler_(sdk::common::internal_log::GlobalLogHandler::GetLogHandler()),
          previous_level_(sdk::common::internal_log::GlobalLogHandler::GetLogLevel())
    {
        sdk::common::internal_log::GlobalLogHandler::SetLogHandler(handler_);
        sdk::common::internal_log::GlobalLogHandler::SetLogLevel(
            sdk::common::internal_log::LogLevel::Debug);
    }

    ~ScopedLog()
    {
      sdk::common::internal_log::GlobalLogHandler::SetLogHandler(
          nostd::shared_ptr<sdk::common::internal_log::LogHandler>(previous_handler_));
      sdk::common::internal_log::GlobalLogHandler::SetLogLevel(previous_level_);
    }

    std::vector<MockLogHandler::Message> Messages()
    {
    auto mock      = static_cast<MockLogHandler *>(handler_.get());
    auto messages  = std::move(mock->messages);
    mock->messages = {};
    return messages;
    }

  private:
    nostd::shared_ptr<sdk::common::internal_log::LogHandler> handler_;
    nostd::shared_ptr<sdk::common::internal_log::LogHandler> previous_handler_;
    sdk::common::internal_log::LogLevel previous_level_;
};

class MockHttpClient : public opentelemetry::ext::http::client::HttpClientSync
{
public:
  MOCK_METHOD(ext::http::client::Result,
              Post,
              (const nostd::string_view &,
               const ext::http::client::Body &,
               const ext::http::client::Headers &),
              (noexcept, override));
  MOCK_METHOD(ext::http::client::Result,
              Get,
              (const nostd::string_view &, const ext::http::client::Headers &),
              (noexcept, override));
};

class FakeResponse : public opentelemetry::ext::http::client::Response
{
public:
  FakeResponse(opentelemetry::ext::http::client::Headers headers,
               opentelemetry::ext::http::client::Body body,
               opentelemetry::ext::http::client::StatusCode status_code)
      : headers_(std::move(headers)), body_(std::move(body)), status_code_(status_code)
  {}

  const opentelemetry::ext::http::client::Body &GetBody() const noexcept override { return body_; }

  bool ForEachHeader(nostd::function_ref<bool(nostd::string_view name, nostd::string_view value)>
                         callable) const noexcept override
  {
    for (const auto &header : headers_)
    {
      if (!callable(header.first, header.second))
      {
        return false;
      }
    }
    return true;
  }

  bool ForEachHeader(const nostd::string_view &name,
                     nostd::function_ref<bool(nostd::string_view name, nostd::string_view value)>
                         callable) const noexcept override
  {
    auto range = headers_.equal_range(static_cast<std::string>(name));
    for (auto it = range.first; it != range.second; ++it)
    {
      if (!callable(it->first, it->second))
      {
        return false;
      }
    }
    return true;
  }

  opentelemetry::ext::http::client::StatusCode GetStatusCode() const noexcept override
  {
    return status_code_;
  }

private:
  opentelemetry::ext::http::client::Headers headers_;
  opentelemetry::ext::http::client::Body body_;
  opentelemetry::ext::http::client::StatusCode status_code_;
};

std::shared_ptr<internal::Retry> LimitedErrorCountRetry(int retries)
{
  class LimitedErrorCountRetry : public internal::Retry {
    public:
      LimitedErrorCountRetry(int retries) : retries_(retries) {}

      bool OnRetry() override { return current_++ < retries_; }

    private:
      int current_ = 0;
      int retries_;
  };

  return std::make_shared<LimitedErrorCountRetry>(retries);
}

Matcher<nostd::string_view const&> expected_path() {
  return StrEq("http://metadata.google.internal/computeMetadata/v1/?recursive=true");
};

Matcher<ext::http::client::Headers const&> expected_headers() {
  return Contains(Pair(StrCaseEq("Metadata-Flavor"), StrCaseEq("Google")));
};

opentelemetry::ext::http::client::Body ToVector(nostd::string_view s) {
  return {s.begin(), s.end()};
}

std::unique_ptr<opentelemetry::sdk::resource::ResourceDetector> MakeTestDetector(
    std::shared_ptr<ext::http::client::HttpClientSync> client)
{
  return MakeGcpDetector(std::move(client), LimitedErrorCountRetry(0), GcpDetectorOptions{});
}

std::unique_ptr<opentelemetry::sdk::resource::ResourceDetector> MakeTestDetector(
    char const *payload)
{
  auto mock = std::make_shared<MockHttpClient>();
  EXPECT_CALL(*mock, Get(expected_path(), expected_headers()))
      .WillRepeatedly([payload](nostd::string_view const &, ext::http::client::Headers const &) {
        auto headers = ext::http::client::Headers{
            {"Metadata-Flavor", "Google"}, {"content-type", "application/json; charset=utf-8"}};
        auto response = std::unique_ptr<FakeResponse>(
            new FakeResponse(std::move(headers), ToVector(payload), 200));
        return ext::http::client::Result(std::move(response),
                                         ext::http::client::SessionState::Response);
      });

  return MakeTestDetector(std::move(mock));
}

TEST(GcpResourceDetector, RespectsEndpoint) {
  auto mock = std::make_shared<MockHttpClient>();
  EXPECT_CALL(*mock, Get(StrEq("http://custom.endpoint/computeMetadata/v1/?recursive=true"),
                         expected_headers()))
      .WillOnce([](nostd::string_view const &, ext::http::client::Headers const &) {
        return ext::http::client::Result(nullptr, ext::http::client::SessionState::ConnectFailed);
      });

  GcpDetectorOptions options;
  options.endpoint       = "http://custom.endpoint";
  auto detector          = internal::MakeGcpDetector(mock, LimitedErrorCountRetry(0), options);
  (void)detector->Detect();
}

TEST(GcpResourceDetector, ConnectionErrors) {
  ScopedLog log;
  auto constexpr kNumRetries = 3;

  auto mock = std::make_shared<MockHttpClient>();
  EXPECT_CALL(*mock, Get(expected_path(), expected_headers()))
      .WillOnce([](nostd::string_view const &, ext::http::client::Headers const &) {
        return ext::http::client::Result(nullptr, ext::http::client::SessionState::ConnectFailed);
      });

  auto detector          = internal::MakeGcpDetector(mock, LimitedErrorCountRetry(kNumRetries));
  auto resource          = detector->Detect();
  auto const &attributes = resource.GetAttributes();

  EXPECT_THAT(attributes, Not(Contains(Attribute<std::string>(sc::kCloudProvider, "gcp"))));

  EXPECT_THAT(log.Messages(),
              Contains(Pair(sdk::common::internal_log::LogLevel::Info,
                            AllOf(HasSubstr("Could not query the metadata server"),
                                  HasSubstr("SessionState"), HasSubstr("ConnectFailed")))));
}

TEST(GcpResourceDetector, RetriesTransientHttpErrors) {
  ScopedLog log;
  auto constexpr kNumRetries = 3;

  auto mock = std::make_shared<MockHttpClient>();
  EXPECT_CALL(*mock, Get(expected_path(), expected_headers()))
      .Times(kNumRetries + 1)
      .WillRepeatedly([](nostd::string_view const &, ext::http::client::Headers const &) {
        auto headers  = ext::http::client::Headers{};
        auto response = std::unique_ptr<FakeResponse>(
            new FakeResponse(std::move(headers), {}, 503));
        return ext::http::client::Result(std::move(response),
                                         ext::http::client::SessionState::Response);
      });

  auto detector          = internal::MakeGcpDetector(mock, LimitedErrorCountRetry(kNumRetries));
  auto resource          = detector->Detect();
  auto const &attributes = resource.GetAttributes();

  EXPECT_THAT(attributes, Not(Contains(Attribute<std::string>(sc::kCloudProvider, "gcp"))));

  EXPECT_THAT(
      log.Messages(),
      Contains(Pair(sdk::common::internal_log::LogLevel::Info,
                    AllOf(HasSubstr("Could not query the metadata server"), HasSubstr("503")))));
}

TEST(GcpResourceDetector, DoesNotRetryPermanentHttpErrors) {
  ScopedLog log;
  auto constexpr kNumRetries = 3;

  auto mock = std::make_shared<MockHttpClient>();
  EXPECT_CALL(*mock, Get(expected_path(), expected_headers()))
      .WillOnce([](nostd::string_view const &, ext::http::client::Headers const &) {
        auto headers  = ext::http::client::Headers{};
        auto response = std::unique_ptr<FakeResponse>(
            new FakeResponse(std::move(headers), {}, 404));
        return ext::http::client::Result(std::move(response),
                                         ext::http::client::SessionState::Response);
      });

  auto detector          = internal::MakeGcpDetector(mock, LimitedErrorCountRetry(kNumRetries));
  auto resource          = detector->Detect();
  auto const &attributes = resource.GetAttributes();

  EXPECT_THAT(attributes, Not(Contains(Attribute<std::string>(sc::kCloudProvider, "gcp"))));

  EXPECT_THAT(
      log.Messages(),
      Contains(Pair(sdk::common::internal_log::LogLevel::Info,
                    AllOf(HasSubstr("Could not query the metadata server"), HasSubstr("404")))));
}

TEST(GcpResourceDetector, ValidatesHeaders) {
  for (auto const &bad_headers : std::vector<ext::http::client::Headers>{
           {},
           {{"content-type", "application/json"}},
           {{"metadata-flavor", "google"}},
           {{"content-type", "wrong"}, {"metadata-flavor", "google"}},
           {{"content-type", "application/json"}, {"metadata-flavor", "wrong"}},
       })
  {
      ScopedLog log;

      auto mock = std::make_shared<MockHttpClient>();
      EXPECT_CALL(*mock, Get(expected_path(), expected_headers()))
          .WillOnce([&bad_headers](nostd::string_view const &, ext::http::client::Headers const &) {
            auto response = std::unique_ptr<FakeResponse>(new FakeResponse(bad_headers, {}, 200));
            return ext::http::client::Result(std::move(response),
                                             ext::http::client::SessionState::Response);
          });

      auto detector          = MakeTestDetector(mock);
      auto resource          = detector->Detect();
      auto const &attributes = resource.GetAttributes();

      EXPECT_THAT(attributes, Not(Contains(Attribute<std::string>(sc::kCloudProvider, "gcp"))));

      EXPECT_THAT(log.Messages(),
                  Contains(Pair(sdk::common::internal_log::LogLevel::Info,
                                AllOf(HasSubstr("Could not query the metadata server"),
                                      HasSubstr("response headers")))));
  }
}

TEST(GcpResourceDetector, HandlesBadJson) {
  auto constexpr kMissingKeysJson = R"json({})json";
  auto constexpr kMalformedJson = R"json({{})json";
  auto constexpr kWrongTypeJson = R"json({
  "instance": [],
  "project": {
    "projectId": "test-project"
  }
})json";
  auto constexpr kWrongStructureJson = R"json({
  "instance": {
    "machineType": {
      "unexpected": 5
    }
  },
  "project": {
    "projectId": "test-project"
  }
})json";

  for (auto const* payload : {kMissingKeysJson, kMalformedJson, kWrongTypeJson,
                              kWrongStructureJson}) {
    auto detector = MakeTestDetector(payload);
    (void)detector->Detect();
  }
}

#ifndef NO_GETENV
void SetEnv(char const *variable, char const *value) {
    if (value == nullptr)
    {
      unsetenv(variable);
    }
    else
    {
      setenv(variable, value, 1);
    }
}

class ScopedEnvironment
{
public:
  ScopedEnvironment(char const *variable, char const *value)
      : variable_(variable), previous_(getenv(variable_))
  {
      SetEnv(variable_, value);
  }

  ~ScopedEnvironment() { SetEnv(variable_, previous_); }

private:
  char const *variable_;
  char const *previous_;
};

TEST(GcpResourceDetector, GkeRegion) {
  ScopedEnvironment env("KUBERNETES_SERVICE_HOST", "0.0.0.0");
  auto constexpr kPayload = R"json({
  "instance": {
    "attributes": {
      "cluster-name": "test-cluster",
      "cluster-location": "projects/1234567890/regions/us-central1"
    },
    "id": 1020304050607080900
  },
  "project": {
    "projectId": "test-project"
  }
})json";

  auto detector = MakeTestDetector(kPayload);
  auto resource = detector->Detect();
  auto const& attributes = resource.GetAttributes();

  EXPECT_THAT(
      attributes,
      IsSupersetOf({
          Attribute<std::string>(sc::kCloudProvider, "gcp"),
          Attribute<std::string>(sc::kCloudAccountId, "test-project"),
          Attribute<std::string>(sc::kCloudPlatform,
                                     "gcp_kubernetes_engine"),
          Attribute<std::string>(sc::kK8sClusterName, "test-cluster"),
          Attribute<std::string>(sc::kHostId, "1020304050607080900"),
          Attribute<std::string>(sc::kCloudRegion, "us-central1"),
      }));
}

TEST(GcpResourceDetector, GkeZone) {
  ScopedEnvironment env("KUBERNETES_SERVICE_HOST", "0.0.0.0");
  auto constexpr kPayload = R"json({
  "instance": {
    "attributes": {
      "cluster-name": "test-cluster",
      "cluster-location": "projects/1234567890/zones/us-central1-a"
    },
    "id": 1020304050607080900
  },
  "project": {
    "projectId": "test-project"
  }
})json";

  auto detector = MakeTestDetector(kPayload);
  auto resource = detector->Detect();
  auto const& attributes = resource.GetAttributes();

  EXPECT_THAT(
      attributes,
      IsSupersetOf({
          Attribute<std::string>(sc::kCloudProvider, "gcp"),
          Attribute<std::string>(sc::kCloudAccountId, "test-project"),
          Attribute<std::string>(sc::kCloudPlatform,
                                     "gcp_kubernetes_engine"),
          Attribute<std::string>(sc::kK8sClusterName, "test-cluster"),
          Attribute<std::string>(sc::kHostId, "1020304050607080900"),
          Attribute<std::string>(sc::kCloudAvailabilityZone,
                                     "us-central1-a"),
      }));
}

TEST(GcpResourceDetector, CloudFunctions) {
  ScopedEnvironment e1("KUBERNETES_SERVICE_HOST", nullptr);
  ScopedEnvironment e2("FUNCTION_TARGET", "set");
  ScopedEnvironment e3("K_SERVICE", "test-service");
  ScopedEnvironment e4("K_REVISION", "test-version");
  auto constexpr kPayload = R"json({
  "instance": {
    "id": 1020304050607080900
  },
  "project": {
    "projectId": "test-project"
  }
})json";

  auto detector = MakeTestDetector(kPayload);
  auto resource = detector->Detect();
  auto const& attributes = resource.GetAttributes();

  EXPECT_THAT(
      attributes,
      IsSupersetOf({
          Attribute<std::string>(sc::kCloudProvider, "gcp"),
          Attribute<std::string>(sc::kCloudAccountId, "test-project"),
          Attribute<std::string>(sc::kCloudPlatform, "gcp_cloud_functions"),
          Attribute<std::string>(sc::kFaasName, "test-service"),
          Attribute<std::string>(sc::kFaasVersion, "test-version"),
          Attribute<std::string>(sc::kFaasInstance, "1020304050607080900"),
      }));
}

TEST(GcpResourceDetector, CloudRun) {
  ScopedEnvironment e1("KUBERNETES_SERVICE_HOST", nullptr);
  ScopedEnvironment e2("FUNCTION_TARGET", nullptr);
  ScopedEnvironment e3("K_CONFIGURATION", "set");
  ScopedEnvironment e4("K_SERVICE", "test-service");
  ScopedEnvironment e5("K_REVISION", "test-version");
  auto constexpr kPayload = R"json({
  "instance": {
    "id": 1020304050607080900
  },
  "project": {
    "projectId": "test-project"
  }
})json";

  auto detector = MakeTestDetector(kPayload);
  auto resource = detector->Detect();
  auto const& attributes = resource.GetAttributes();

  EXPECT_THAT(
      attributes,
      IsSupersetOf({
          Attribute<std::string>(sc::kCloudProvider, "gcp"),
          Attribute<std::string>(sc::kCloudAccountId, "test-project"),
          Attribute<std::string>(sc::kCloudPlatform, "gcp_cloud_run"),
          Attribute<std::string>(sc::kFaasName, "test-service"),
          Attribute<std::string>(sc::kFaasVersion, "test-version"),
          Attribute<std::string>(sc::kFaasInstance, "1020304050607080900"),
      }));
}

TEST(GcpResourceDetector, Gae) {
  ScopedEnvironment e1("KUBERNETES_SERVICE_HOST", nullptr);
  ScopedEnvironment e2("FUNCTION_TARGET", nullptr);
  ScopedEnvironment e3("K_CONFIGURATION", nullptr);
  ScopedEnvironment e4("GAE_SERVICE", "test-service");
  ScopedEnvironment e5("GAE_VERSION", "test-version");
  ScopedEnvironment e6("GAE_INSTANCE", "test-instance");
  auto constexpr kPayload = R"json({
  "instance": {
    "zone": "projects/1234567890/zones/us-central1-a"
  },
  "project": {
    "projectId": "test-project"
  }
})json";

  auto detector = MakeTestDetector(kPayload);
  auto resource = detector->Detect();
  auto const& attributes = resource.GetAttributes();

  EXPECT_THAT(
      attributes,
      IsSupersetOf({
          Attribute<std::string>(sc::kCloudProvider, "gcp"),
          Attribute<std::string>(sc::kCloudAccountId, "test-project"),
          Attribute<std::string>(sc::kCloudPlatform, "gcp_app_engine"),
          Attribute<std::string>(sc::kFaasName, "test-service"),
          Attribute<std::string>(sc::kFaasVersion, "test-version"),
          Attribute<std::string>(sc::kFaasInstance, "test-instance"),
          Attribute<std::string>(sc::kCloudAvailabilityZone,
                                     "us-central1-a"),
          Attribute<std::string>(sc::kCloudRegion, "us-central1"),
      }));
}

TEST(GcpResourceDetector, Gce) {
  ScopedEnvironment e1("KUBERNETES_SERVICE_HOST", nullptr);
  ScopedEnvironment e2("FUNCTION_TARGET", nullptr);
  ScopedEnvironment e3("K_CONFIGURATION", nullptr);
  ScopedEnvironment e4("GAE_SERVICE", nullptr);
  auto constexpr kPayload = R"json({
  "instance": {
    "id": 1020304050607080900,
    "machineType": "projects/1234567890/machineTypes/c2d-standard-16",
    "name": "test-instance",
    "zone": "projects/1234567890/zones/us-central1-a"
  },
  "project": {
    "projectId": "test-project"
  }
})json";

  auto detector = MakeTestDetector(kPayload);
  auto resource = detector->Detect();
  auto const& attributes = resource.GetAttributes();

  EXPECT_THAT(
      attributes,
      IsSupersetOf({
          Attribute<std::string>(sc::kCloudProvider, "gcp"),
          Attribute<std::string>(sc::kCloudAccountId, "test-project"),
          Attribute<std::string>(sc::kCloudPlatform, "gcp_compute_engine"),
          Attribute<std::string>(sc::kHostType, "c2d-standard-16"),
          Attribute<std::string>(sc::kHostId, "1020304050607080900"),
          Attribute<std::string>(sc::kHostName, "test-instance"),
          Attribute<std::string>(sc::kCloudAvailabilityZone,
                                     "us-central1-a"),
          Attribute<std::string>(sc::kCloudRegion, "us-central1"),
      }));
}

TEST(GcpResourceDetector, CachesAttributes) {
  ScopedEnvironment e1("KUBERNETES_SERVICE_HOST", nullptr);
  ScopedEnvironment e2("FUNCTION_TARGET", nullptr);
  ScopedEnvironment e3("K_CONFIGURATION", nullptr);
  ScopedEnvironment e4("GAE_SERVICE", nullptr);
  auto constexpr kPayload = R"json({
  "instance": {
    "id": 1020304050607080900,
    "machineType": "projects/1234567890/machineTypes/c2d-standard-16",
    "name": "test-instance",
    "zone": "projects/1234567890/zones/us-central1-a"
  },
  "project": {
    "projectId": "test-project"
  }
})json";

  // Note that this detector expects exactly one HttpClientSync::Get() call.
  auto detector = MakeTestDetector(kPayload);
  (void)detector->Detect();
  (void)detector->Detect();
}
#  endif  // NO_GETENV

}  // namespace
}  // namespace gcp
}  // namespace detector
OPENTELEMETRY_END_NAMESPACE

#  endif
#endif
