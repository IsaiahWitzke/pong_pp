# --- build stage: compile the server with Linux clang ---
# Trixie ships GCC 14 / libstdc++-14, which provides <format> (added in libstdc++ 13).
# Bookworm's libstdc++-12 doesn't have it.
FROM debian:trixie-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        clang make libstdc++-14-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN make server

# --- runtime stage: minimal image with just the binary ---
# Must match (or be newer than) the build base's glibc. We build on trixie
# (glibc 2.41), so we need distroless/cc-debian13; cc-debian12 ships glibc 2.36
# and the binary fails to start with "GLIBC_2.38 not found".
FROM gcr.io/distroless/cc-debian13

COPY --from=build /src/bin/server /server

# Cloud Run sets PORT to 8080 by default; main.cpp reads it.
ENV PORT=8080
EXPOSE 8080

CMD ["/server"]
