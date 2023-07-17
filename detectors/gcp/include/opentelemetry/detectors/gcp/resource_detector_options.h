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

struct GcpDetectorOptions
{
  std::string endpoint = "http://metadata.google.internal";
};

}  // namespace gcp
}  // namespace detector
OPENTELEMETRY_END_NAMESPACE
