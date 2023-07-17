// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "opentelemetry/detectors/gcp/internal/resource_detector_impl.h"
#include "opentelemetry/detectors/gcp/resource_detector_options.h"
#include "opentelemetry/sdk/resource/resource_detector.h"

#include <nlohmann/json.hpp>

OPENTELEMETRY_BEGIN_NAMESPACE
namespace detector
{
namespace gcp
{

std::unique_ptr<sdk::resource::ResourceDetector> MakeGcpDetector(GcpDetectorOptions options = {});

}  // namespace gcp
}  // namespace detector
OPENTELEMETRY_END_NAMESPACE
