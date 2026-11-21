#!/bin/sh
# Build an add-on mod:   ./build.sh examples/bpm-diff
# Output: <folder>/build/<name>/mod.so -> copy the <name> folder into MODS/ on the
# settings USB. Needs Docker. The first run builds the SDK image (a minute or two).
set -eu
SRC="${1:?usage: build.sh <mod folder with mod.c>}"
NAME="$(basename "$(cd "$SRC" && pwd)")"
SDK="$(cd "$(dirname "$0")" && pwd)"
docker image inspect oqmod-sdk >/dev/null 2>&1 || docker build -t oqmod-sdk "$SDK"
mkdir -p "$SRC/build/$NAME"
docker run --rm -v "$SDK/include:/sdk:ro" -v "$(cd "$SRC" && pwd):/work" oqmod-sdk sh -ec "
  aarch64-linux-gnu-gcc -O2 -fPIC -shared -Wall -Wextra -Werror -Wno-unused-function -fvisibility=hidden \
    -I/sdk -Wl,--no-undefined -Wl,-z,defs -o build/$NAME/mod.so *.c -lpthread -ldl -lc -lm
  # oqmod_init must be exported; nothing newer than the deck's glibc 2.29 may be used
  aarch64-linux-gnu-objdump -T build/$NAME/mod.so | grep -q ' oqmod_init$' || { echo 'oqmod_init is not exported'; exit 1; }
  if aarch64-linux-gnu-objdump -T build/$NAME/mod.so | grep -oE 'GLIBC_2\.[0-9]+' | sort -u | awk -F. '\$2>29{bad=1} END{exit !bad}'; then
    echo 'uses a C library function newer than the deck has (glibc 2.29)'; exit 1; fi
  aarch64-linux-gnu-strip --strip-unneeded build/$NAME/mod.so"
echo "built $SRC/build/$NAME/mod.so -> copy $SRC/build/$NAME to MODS/$NAME on the settings USB"
for asset in icon.pgm menu.order README.md LICENSE NOTICE diagnostics.only keep.running menu.title version.txt description.txt choices.order; do
  if test -f "$SRC/$asset"; then cp "$SRC/$asset" "$SRC/build/$NAME/$asset"; fi
done
# Minimum firmware: the newer of the folder's min-firmware.txt (default R120) and what the
# built mod.so checks the API for (tools/min_firmware.py). The player refuses to load a mod
# that needs newer firmware (R124+); the OQ Mods Patcher warns before installing it.
declared="$(tr -d ' \r\n' < "$SRC/min-firmware.txt" 2>/dev/null || true)"
test -n "$declared" || declared=R120
python3 "$SDK/tools/min_firmware.py" "$SRC/build/$NAME/mod.so" --declared "$declared" > "$SRC/build/$NAME/min-firmware.txt"
echo "minimum firmware: $(cat "$SRC/build/$NAME/min-firmware.txt")"
