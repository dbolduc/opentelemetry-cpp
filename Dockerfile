# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

FROM fedora:37

RUN dnf install -y cmake curl g++ git make ninja-build tar tree

WORKDIR /var/tmp/build
RUN curl -fsSL https://github.com/abseil/abseil-cpp/archive/20230802.0.tar.gz | \
    tar -xzf - --strip-components=1 && \
    cmake \
      -DCMAKE_CXX_STANDARD=14 \
      -DCMAKE_BUILD_TYPE="Release" \
      -DABSL_BUILD_TESTING=OFF \
      -DBUILD_SHARED_LIBS=yes \
      -GNinja -S . -B cmake-out && \
    cmake --build cmake-out --target install && \
    ldconfig && cd /var/tmp && rm -fr build

ADD . /opentelemetry-cpp
WORKDIR /opentelemetry-cpp
