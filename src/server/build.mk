SERVER_SRCS := $(shell find src/server -name '*.cpp')
SERVER_OUT := bin/server

# -I src/server lets sources include "ws/foo.h" (and just "foo.h" for top-level).
SERVER_CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -I src/server

# Static-link libstdc++/libgcc when building on Linux (i.e. the Docker build),
# so the resulting binary doesn't depend on the build host's libstdc++ version.
# Lets us compile with trixie's GCC 14 (needed for <format>) and still run on
# older runtimes. macOS's Apple clang uses libc++ and doesn't accept these
# flags, so they're guarded out for local builds.
ifeq ($(shell uname -s),Linux)
SERVER_LDFLAGS := -static-libstdc++ -static-libgcc
endif

server: $(SERVER_OUT)

$(SERVER_OUT): $(SERVER_SRCS)
	@mkdir -p bin
	clang++ $(SERVER_CXXFLAGS) -o $@ $(SERVER_SRCS) $(SERVER_LDFLAGS)
