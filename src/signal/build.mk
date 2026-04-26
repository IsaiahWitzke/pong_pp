SIGNAL_SRCS := $(wildcard src/signal/*.cpp)
SIGNAL_OUT := bin/signal

SIGNAL_CXXFLAGS := -std=c++20 -O2 -Wall -Wextra

signal: $(SIGNAL_OUT)

$(SIGNAL_OUT): $(SIGNAL_SRCS)
	@mkdir -p bin
	clang++ $(SIGNAL_CXXFLAGS) -o $@ $(SIGNAL_SRCS)
