# Top-level Makefile. Per-subproject build rules live in build.mk
# fragments next to the source they describe; this file just stitches
# them together with shared phony targets.

.PHONY: all clean serve client signal

all: client signal

include src/client/build.mk
include src/signal/build.mk

serve: client
	cd web && python3 -m http.server 8080

clean:
	rm -f $(CLIENT_OUT) $(SIGNAL_OUT)
