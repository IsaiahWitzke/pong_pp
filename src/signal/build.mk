SIGNAL_SRC := src/signal/main.cpp
SIGNAL_OUT := bin/signal

SIGNAL_CXXFLAGS := -std=c++20 -O2 -Wall -Wextra

signal: $(SIGNAL_OUT)

$(SIGNAL_OUT): $(SIGNAL_SRC)
	@mkdir -p bin
	clang++ $(SIGNAL_CXXFLAGS) -o $@ $<
