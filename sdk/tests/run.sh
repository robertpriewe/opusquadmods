#!/bin/sh
# SDK-only tests: no firmware, native addresses, Docker or keys required.
set -eu
SDK="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$SDK/tests/build"
mkdir -p "$OUT"
for test in events native_feature fps_overlay master_audio fx_engine quick_usb zone_engine render_engine target_engine slip_engine title_engine; do
  "${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -I"$SDK/include" \
    "$SDK/tests/${test}_test.c" -o "$OUT/$test" -ldl -pthread -lm
  "$OUT/$test"
done
