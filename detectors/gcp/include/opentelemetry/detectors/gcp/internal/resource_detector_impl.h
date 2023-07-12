// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "opentelemetry/ext/http/client/http_client_factory.h"
#include "opentelemetry/sdk/common/env_variables.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/resource/resource_detector.h"
#include "opentelemetry/sdk/resource/semantic_conventions.h"

#include <deque>
#include <nlohmann/json.hpp>
#include <thread>

OPENTELEMETRY_BEGIN_NAMESPACE
namespace detector
{
namespace gcp
{

namespace sc = opentelemetry::sdk::resource::SemanticConventions;
using ext::http::client::HttpClientFactory;
using ext::http::client::HttpClientSync;

// The metadata server returns fully qualified names. (e.g. a zone may be
// "projects/p/zones/us-central1-a"). Return the IDs only.
std::string Tail(std::string const &value)
{
  auto const pos = value.rfind('/');
  return value.substr(pos + 1);
}

// NOLINTNEXTLINE(misc-no-recursion);
std::string FindRecursive(nlohmann::json json, std::deque<std::string> keys)
{
  if (keys.empty())
  {
    if (json.is_string()) {
      return json.get<std::string>();
    }
    if (json.is_number()) {
      return std::to_string(json.get<std::int64_t>());
    }
    return {};
  }
  auto const l = json.find(keys.front());
  if (l == json.end()) {
    return {};
  }
  keys.pop_front();
  return FindRecursive(*l, std::move(keys));
}

std::string ToLower(nostd::string_view s)
{
  std::string ret;
  ret.reserve(s.size());
  for (auto c : s)
  {
    if (c != '\r')
    {
      ret.push_back(std::tolower(c));
    }
  }
  return ret;
}

bool ValidateHeaders(ext::http::client::Response &response)
{
  bool valid_content_type = false;
  bool valid_metadata_flavor = false;
  response.ForEachHeader([&](nostd::string_view k, nostd::string_view v) {
    auto ks = ToLower(k);
    if (ks == "content-type" && ToLower(v).find("application/json") == 0)
    {
      valid_content_type = true;
    }
    if (ks == "metadata-flavor" && ToLower(v) == "google")
    {
      valid_metadata_flavor = true;
    }
    return true;
  });
  return valid_content_type && valid_metadata_flavor;
}

bool ValidateJson(nlohmann::json const &json)
{
  return json.is_object() && json.find("project") != json.end();
}

// TODO : abstract into options? and probably factor out the param ?recursive=true
std::string Endpoint()
{
  return "http://metadata.google.internal/computeMetadata/v1/?recursive=true";
}

struct QmsResult
{
  nlohmann::json json;  // used if error.empty()
  std::string error = "Unknown error.";
  bool retry        = false;
};

QmsResult QmsMapStatus(ext::http::client::StatusCode c)
{
  QmsResult result;
  // TODO : set retries as well.
  if (c < 200 || c >= 300)
  {
    result.error = "HTTP code=" + std::to_string(c);
  }
  // TODO : error is defaulted to bad, so clear it. A clearer interface might be in order.
  result.error = "";
  return result;
}

QmsResult QmsOnce(std::shared_ptr<HttpClientSync> client)
{
  auto endpoint = Endpoint();
  auto result   = client->GetNoSsl(endpoint, {{"Metadata-Flavor", "Google"}});
  if (!result)
  {
    QmsResult r;
    r.error =
        "SessionState as int: " + std::to_string(static_cast<int>(result.GetSessionState()));
    r.retry = false;
    return r;
  }
  auto &response = result.GetResponse();
  auto status    = QmsMapStatus(response.GetStatusCode());
  if (!status.error.empty())
  {
    return status;
  }
  if (!ValidateHeaders(response))
  {
    QmsResult r;
    r.error = "response headers do not match expectations";
    r.retry   = true;
    return r;
  }
  auto json = nlohmann::json::parse(response.GetBody(), nullptr, false);
  if (!ValidateJson(json))
  {
    QmsResult r;
    r.error = "returned payload does not match expectation.";
    r.retry   = true;
    return r;
  }
  QmsResult r;
  r.json = std::move(json);
  r.error = "";
  return r;
}

class Retry
{
public:
  // Returns true if we should keep retrying, false if we should stop retrying.
  virtual bool OnRetry() = 0;
};

QmsResult RetryLoop(std::shared_ptr<HttpClientSync> client, std::shared_ptr<Retry> retry)
{
  QmsResult result;
  for (;;)
  {
    result = QmsOnce(client);
    if (!result.retry || !retry->OnRetry())
    {
      break;
    }
  }
  return result;
}

// This class is essentially a function that takes in metadata and returns
// resource attributes. We only use a class because it simplifies the code.
class Parser
{
public:
  explicit Parser(nlohmann::json metadata) : metadata_(std::move(metadata))
  {
    ProcessMetadataAndEnv();
  }

  opentelemetry::sdk::resource::ResourceAttributes Attributes() && { return attributes_; }

private:
  // Synthesize the metadata returned from the metadata server and certain
  // environment variables into resource attributes. This populates the
  // `attributes_` member.
  void ProcessMetadataAndEnv()
  {
    SetAttribute(sc::kCloudProvider, "gcp");
    SetAttribute(sc::kCloudAccountId, Metadata({"project", "projectId"}));

    std::string value;
    if (sdk::common::GetStringEnvironmentVariable("KUBERNETES_SERVICE_HOST", value))
    {
      return Gke();
    }
    if (sdk::common::GetStringEnvironmentVariable("FUNCTION_TARGET", value))
    {
      return CloudFunctions();
    }
    if (sdk::common::GetStringEnvironmentVariable("K_CONFIGURATION", value))
    {
      return CloudRun();
    }
    if (sdk::common::GetStringEnvironmentVariable("GAE_SERVICE", value))
    {
      return Gae();
    }
    if (!Metadata({"instance", "machineType"}).empty()) {
      return Gce();
    }
  }

  void Gke()
  {
    SetAttribute(sc::kCloudPlatform, "gcp_kubernetes_engine");
    SetAttribute(sc::kK8sClusterName, Metadata({"instance", "attributes", "cluster-name"}));
    SetAttribute(sc::kHostId, Metadata({"instance", "id"}));
    auto cluster_location = Tail(Metadata({"instance", "attributes", "cluster-location"}));

    // The cluster location is either a region (us-west1) or a zone (us-west1-a)
    auto hyphen_count = std::count(cluster_location.begin(), cluster_location.end(), '-');
    if (hyphen_count == 1)
    {
      SetAttribute(sc::kCloudRegion, cluster_location);
    }
    else if (hyphen_count == 2)
    {
      SetAttribute(sc::kCloudAvailabilityZone, cluster_location);
    }
  }

  void CloudFunctions()
  {
    SetAttribute(sc::kCloudPlatform, "gcp_cloud_functions");
    SetEnvAttribute(sc::kFaasName, "K_SERVICE");
    SetEnvAttribute(sc::kFaasVersion, "K_REVISION");
    SetAttribute(sc::kFaasInstance, Metadata({"instance", "id"}));
  }

  void CloudRun()
  {
    SetAttribute(sc::kCloudPlatform, "gcp_cloud_run");
    SetEnvAttribute(sc::kFaasName, "K_SERVICE");
    SetEnvAttribute(sc::kFaasVersion, "K_REVISION");
    SetAttribute(sc::kFaasInstance, Metadata({"instance", "id"}));
  }

  void Gae()
  {
    SetAttribute(sc::kCloudPlatform, "gcp_app_engine");
    SetEnvAttribute(sc::kFaasName, "GAE_SERVICE");
    SetEnvAttribute(sc::kFaasVersion, "GAE_VERSION");
    SetEnvAttribute(sc::kFaasInstance, "GAE_INSTANCE");

    auto zone = Tail(Metadata({"instance", "zone"}));
    SetAttribute(sc::kCloudAvailabilityZone, zone);
    auto const pos = zone.rfind('-');
    SetAttribute(sc::kCloudRegion, zone.substr(0, pos));
  }

  void Gce()
  {
    SetAttribute(sc::kCloudPlatform, "gcp_compute_engine");
    SetAttribute(sc::kHostType, Tail(Metadata({"instance", "machineType"})));
    SetAttribute(sc::kHostId, Metadata({"instance", "id"}));
    SetAttribute(sc::kHostName, Metadata({"instance", "name"}));

    auto zone = Tail(Metadata({"instance", "zone"}));
    SetAttribute(sc::kCloudAvailabilityZone, zone);
    auto const pos = zone.rfind('-');
    SetAttribute(sc::kCloudRegion, zone.substr(0, pos));
  }

  std::string Metadata(std::deque<std::string> keys)
  {
    return FindRecursive(metadata_, std::move(keys));
  }

  void SetAttribute(char const *key, std::string const &value)
  {
    if (value.empty())
      return;
    attributes_.SetAttribute(key, value);
  }

  void SetEnvAttribute(char const *key, char const *env)
  {
    std::string value;
    auto valid = sdk::common::GetStringEnvironmentVariable(env, value);
    if (valid) {
      SetAttribute(key, value);
    }
  }

  nlohmann::json metadata_;
  opentelemetry::sdk::resource::ResourceAttributes attributes_;
};

class DefaultRetry : public Retry
{
public:
  bool OnRetry() override
  {
    if (it_ == backoffs_.end())
    {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::seconds(*it_));
    ++it_;
    return true;
  }

private:
  std::vector<int> backoffs_     = {1, 2, 4, 8, 16};
  std::vector<int>::iterator it_ = backoffs_.begin();
};

}  // namespace gcp
}  // namespace detector
OPENTELEMETRY_END_NAMESPACE
