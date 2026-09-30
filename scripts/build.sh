#!/bin/sh
set -eu
APP_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$APP_ROOT/build"}
mkdir -p "$BUILD_DIR"
for part in toolchain forge-postgres forge-web; do
 cmake -S "$APP_ROOT/vendor/$part" -B "$BUILD_DIR/$part" -DCMAKE_BUILD_TYPE=Release
 cmake --build "$BUILD_DIR/$part" -j 4
done
FORGE="$BUILD_DIR/toolchain/bin/forge"
for target in backend cli; do
 "$FORGE" "$APP_ROOT/$target/src/main.fg" --emit-c -o "$BUILD_DIR/$target.c" -I "$APP_ROOT/vendor/forge-postgres" -I "$APP_ROOT/vendor/forge-web" -I "$APP_ROOT/backend/src" --forge-root "$APP_ROOT/vendor/toolchain" --lib-dir "$BUILD_DIR/toolchain/lib"
 cat "$APP_ROOT/$target/native/adapter.c" >> "$BUILD_DIR/$target.c"
 cc -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -Werror=implicit-function-declaration -Werror=incompatible-pointer-types -I "$APP_ROOT/vendor/toolchain/include" -I "$APP_ROOT/vendor/forge-web/include" "$BUILD_DIR/$target.c" "$BUILD_DIR/forge-postgres/libforge_postgres.a" "$BUILD_DIR/forge-web/libforge_web.a" "$BUILD_DIR/toolchain/lib/libforge_runtime.a" "$BUILD_DIR/toolchain/lib/libforge_std.a" $(pkg-config --cflags --libs libpq libmicrohttpd json-c libcurl openssl) -lpthread -lm -o "$BUILD_DIR/forge-$target"
done
