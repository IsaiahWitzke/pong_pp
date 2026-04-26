# --- build stage: compile the server with Linux clang ---
FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        clang make libstdc++-12-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN make server

# --- runtime stage: minimal image with just the binary ---
FROM gcr.io/distroless/cc-debian12

COPY --from=build /src/bin/server /server

# Cloud Run sets PORT to 8080 by default; main.cpp reads it.
ENV PORT=8080
EXPOSE 8080

CMD ["/server"]
