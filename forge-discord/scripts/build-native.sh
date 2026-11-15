#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD=${FORGE_DISCORD_BUILD_DIR:-"$ROOT/build/discord-native"}
mkdir -p "$BUILD"
cmake -S "$ROOT/vendor/toolchain" -B "$BUILD/toolchain" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD/toolchain" -j 4
cmake -S "$ROOT/vendor/forge-web" -B "$BUILD/web" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$BUILD/web" -j 4
compile() {
 source=$1
 output=$2
 mkdir -p "$(dirname -- "$output")"
 "$BUILD/toolchain/bin/forge" "$source" --emit-c -o "$output.c" \
  -I "$ROOT/forge-discord/src" -I "$ROOT/vendor/forge-discord" \
  -I "$ROOT/vendor/forge-discord/tests" -I "$ROOT/vendor/forge-web" \
  --forge-root "$ROOT/vendor/toolchain" --lib-dir "$BUILD/toolchain/lib"
 cc -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function \
  -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
  -I "$ROOT/vendor/toolchain/include" -I "$ROOT/vendor/forge-web/include" \
  "$output.c" "$ROOT/forge-discord/native/transport.c" \
  "$BUILD/web/libforge_web.a" "$BUILD/toolchain/lib/libforge_runtime.a" "$BUILD/toolchain/lib/libforge_std.a" \
  $(pkg-config --cflags --libs libmicrohttpd json-c libcurl openssl icu-uc) -lpthread -lm -o "$output"
}
if [ "$#" = 2 ]; then
 compile "$1" "$2"
elif [ "$#" = 0 ]; then
 compile "$ROOT/forge-discord/src/main.fg" "$ROOT/build/forge-discord"
 compile "$ROOT/forge-discord/src/register.fg" "$ROOT/build/forge-discord-register"
 compile "$ROOT/forge-discord/src/check.fg" "$ROOT/build/forge-discord-check"
else
 echo 'Usage: build-native.sh [SOURCE OUTPUT]' >&2
 exit 2
fi
