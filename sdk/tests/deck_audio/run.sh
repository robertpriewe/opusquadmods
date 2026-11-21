#!/bin/sh
# Deck audio insert test (Linux ARM64 in the SDK's Docker image): a fake EP136 with an
# exported snd_pcm_writei and two mods (two .so files) chained on the same slot.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)";SDK="$(cd "$HERE/../.." && pwd)"
docker image inspect oqmod-sdk >/dev/null 2>&1 || docker build -t oqmod-sdk "$SDK"
docker run --rm -v "$HERE:/work" -v "$SDK/include:/sdk:ro" oqmod-sdk sh -ec '
CC=aarch64-linux-gnu-gcc; T=$(mktemp -d)
$CC -O2 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden -I/sdk -DMOD_A -o $T/mod_a.so fake_mod.c
$CC -O2 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden -I/sdk -DMOD_B -o $T/mod_b.so fake_mod.c
$CC -O2 -Wall -Werror -I/sdk -rdynamic -o $T/host host.c -ldl -lm
cd $T && ./host'
