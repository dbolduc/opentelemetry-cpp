// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace detector
{
namespace gcp
{

/**
 * Configuration options for the GCP resource detector.
 */
struct GcpDetectorOptions
{
  /**
   * The endpoint of the Google Compute Engine (GCE) [Metadata Server]
   *
   * [metadata server]: https://cloud.google.com/compute/docs/metadata/overview
   */
  std::string endpoint = "http://metadata.google.internal";
};

}  // namespace gcp
}  // namespace detector
OPENTELEMETRY_END_NAMESPACE
