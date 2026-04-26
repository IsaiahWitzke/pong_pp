# Top-level Makefile. Per-subproject build rules live in build.mk
# fragments next to the source they describe; this file just stitches
# them together with shared phony targets.

.PHONY: all clean serve client server fmt

all: client server

include src/client/build.mk
include src/server/build.mk

serve: client
	cd web && python3 -m http.server 8080

fmt:
	@find src -type f \( -name '*.cpp' -o -name '*.h' \) | xargs clang-format -i

clean:
	rm -f $(CLIENT_OUT) $(SERVER_OUT)
