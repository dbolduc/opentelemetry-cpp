# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "gcp_detector",
    srcs = [
        "src/resource_detector.cc",
        "src/resource_detector_impl.cc",
    ],
    hdrs = [
        "include/opentelemetry/detectors/gcp/resource_detector.h",
        "include/opentelemetry/detectors/gcp/resource_detector_options.h",
        "include/opentelemetry/detectors/gcp/internal/resource_detector_impl.h",
    ],
    copts = [
        "-DCURL_STATICLIB",
    ],
    strip_include_prefix = "include",
    tags = ["gcp"],
    deps = [
        "//sdk/src/resource",
        "//ext/src/http/client/curl:http_client_curl",
        "@github_nlohmann_json//:json",
    ],
)

cc_test(
    name = "resource_detector_test",
    srcs = ["test/resource_detector_test.cc"],
    tags = [
        "test",
        "gcp",
    ],
    deps = [
        ":gcp_detector",
        "@com_google_googletest//:gtest_main",
    ],
)
