# Top-level Makefile. Per-subproject build rules live in build.mk
# fragments next to the source they describe; this file just stitches
# them together with shared phony targets.

.PHONY: all clean serve client server fmt push deploy docker-auth

all: client server

include src/client/build.mk
include src/server/build.mk

serve: client
	cd web && python3 -m http.server 8080

fmt:
	@find src -type f \( -name '*.cpp' -o -name '*.h' \) | xargs clang-format -i

clean:
	rm -f $(CLIENT_OUT) $(SERVER_OUT)

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
