# OQ Mods SDK build environment: a stock Debian cross compiler for the deck's ARM64
# Linux. Nothing from the OPUS firmware is in here.
FROM debian:bullseye-slim
RUN apt-get update && apt-get install -y --no-install-recommends gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu libc6-dev-arm64-cross make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
