SERVER_SRCS := $(shell find src/server -name '*.cpp')
SERVER_OUT := bin/server

# -I src/server lets sources include "ws/foo.h" (and just "foo.h" for top-level).
SERVER_CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -I src/server

server: $(SERVER_OUT)

$(SERVER_OUT): $(SERVER_SRCS)
	@mkdir -p bin
	clang++ $(SERVER_CXXFLAGS) -o $@ $(SERVER_SRCS)
