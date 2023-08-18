# To be run from within the docker image.
#
# Build that docker image with:
# ```sh
# docker build -t darren-otel .
# ```
#
# Then run it with:
# ```sh
# docker run --rm -it --entrypoint bash darren-otel
# ```
#

# Install opentelemetry-cpp
mkdir -p build && pushd build >/dev/null \
  && cmake \
      -DCMAKE_CXX_STANDARD=14 \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE  \
      -DCMAKE_INSTALL_PREFIX=/opt/opentelemetry-cpp-install \
      -DBUILD_TESTING=OFF \
      -DBUILD_SHARED_LIBS=ON \
      -DWITH_ABSEIL=ON \
      .. \
  && cmake --build . -j $(nproc) --target install
popd >/dev/null

# Build quickstart with make
mkdir -p "quickstart/make-bin"
PKG_CONFIG_PATH="/opt/opentelemetry-cpp-install/lib64/pkgconfig:${PKG_CONFIG_PATH:-}" \
  make -C "quickstart/" BIN="make-bin"

# Run quickstart
LD_LIBRARY_PATH="/usr/local/lib64:/opt/opentelemetry-cpp-install/lib64:${LD_LIBRARY_PATH:-}" \
  quickstart/make-bin/quickstart
