# Top-level Makefile. Per-subproject build rules live in build.mk
# fragments next to the source they describe; this file just stitches
# them together with shared phony targets.

.PHONY: all clean serve client server web fmt push deploy docker-auth smoke

all: client server web

include src/client/build.mk
include src/server/build.mk

# Compile the TypeScript sources under web/src/ into ES modules emitted
# alongside web/index.html. tsc only — no bundler — so the dev server
# stays a plain `python3 -m http.server`.
WEB_SRC := $(wildcard web/src/*.ts)
WEB_OUT := web/loader.js web/signaling.js web/rtc.js web/types.js

web/node_modules: web/package.json
	cd web && npm install --silent
	@touch $@

$(WEB_OUT): $(WEB_SRC) web/tsconfig.json web/node_modules
	cd web && npx tsc

web: $(WEB_OUT)

serve: client web
	cd web && python3 -m http.server 8080

fmt:
	@find src -type f \( -name '*.cpp' -o -name '*.h' \) | xargs clang-format -i

clean:
	rm -f $(CLIENT_OUT) $(SERVER_OUT) $(WEB_OUT)

# Smoke test: boot the server, fire a real WebSocket request via websocat
# (expect a `ROOMS` reply) and a plain HTTP GET via curl (expect `426 Upgrade
# Required`), then tear down. Requires websocat (`brew install websocat`).
smoke: server
	@echo "==> starting server on :9000 (logs -> /tmp/pong-server.log)"
	@bin/server > /tmp/pong-server.log 2>&1 & \
	  SERVER_PID=$$!; \
	  trap "kill $$SERVER_PID 2>/dev/null; echo; echo '--- server log ---'; cat /tmp/pong-server.log" EXIT; \
	  sleep 0.5; \
	  echo; \
	  echo "==> WS test (sending LIST, expect ROOMS reply):"; \
	  { echo LIST; sleep 0.3; } | websocat ws://localhost:9000; \
	  echo; \
	  echo "==> HTTP test (GET /, expect 426 Upgrade Required):"; \
	  curl -sSi http://localhost:9000/

# ----- Deploy: build & push the server image, then roll a new Cloud Run revision -----
# Override on the command line, e.g.: make deploy IMAGE_TAG=$(git rev-parse --short HEAD)
GCP_PROJECT_ID ?= pong-pp-494519
GCP_REGION     ?= us-east1
GCP_REPO       ?= pong-pp
CR_SERVICE     ?= pong-signal
IMAGE_TAG      ?= latest
IMAGE          := $(GCP_REGION)-docker.pkg.dev/$(GCP_PROJECT_ID)/$(GCP_REPO)/server:$(IMAGE_TAG)

# One-time-per-machine: teach Docker how to auth to Artifact Registry.
# Safe to re-run; idempotent.
docker-auth:
	gcloud auth configure-docker $(GCP_REGION)-docker.pkg.dev --quiet

# Build the server image locally and push it to Artifact Registry.
# `--platform linux/amd64` is required because Cloud Run runs amd64 and
# Apple Silicon would otherwise produce an arm64 image that won't boot.
push:
	docker buildx build --platform linux/amd64 -t $(IMAGE) --push .

# Push a fresh image and roll out a new Cloud Run revision pointing at it.
# Note: re-pushing :latest alone does NOT redeploy -- Cloud Run pins each
# revision to the resolved image digest, so we need an explicit `run deploy`.
deploy: push
	gcloud run deploy $(CR_SERVICE) \
		--image $(IMAGE) \
		--region $(GCP_REGION) \
		--project $(GCP_PROJECT_ID)
