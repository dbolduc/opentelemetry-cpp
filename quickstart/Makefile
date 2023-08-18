# The CXX, CXXFLAGS and CXXLD variables are hard-coded. These values work for
# our tests, but applications would typically make them configurable parameters.
CXX=g++
CXXFLAGS=
CXXLD=$(CXX)
BIN=.

all: $(BIN)/quickstart

# Configuration variables to compile and link against the OpenTelemetry SDK
# Resources library.
CLIENT_MODULE     := opentelemetry_resources
CLIENT_CXXFLAGS   := $(shell pkg-config $(CLIENT_MODULE) --cflags)
CLIENT_CXXLDFLAGS := $(shell pkg-config $(CLIENT_MODULE) --libs-only-L)
CLIENT_LIBS       := $(shell pkg-config $(CLIENT_MODULE) --libs-only-l)

$(BIN)/quickstart: quickstart.cc
	$(CXXLD) $(CXXFLAGS) $(CLIENT_CXXFLAGS) $(CLIENT_CXXLDFLAGS) -o $@ $^ $(CLIENT_LIBS)
