#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=${VERSION:-0.3.0-preview.6}
BUILD_DIR=${BUILD_DIR:-"$ROOT/build"}
STAGE="$BUILD_DIR/release-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/build/bin" "$STAGE/build/lib" "$STAGE/libexec/lib" "$STAGE/licenses"
cp "$BUILD_DIR/toolchain/bin/forge" "$STAGE/build/bin/forge"
cp "$BUILD_DIR/toolchain/lib/"*.a "$STAGE/build/lib/"
cp -R "$ROOT/vendor/toolchain/include" "$STAGE/include"
cp "$BUILD_DIR/forge-cli" "$STAGE/libexec/forge-pm"
cp "$ROOT/backend/seed.json" "$STAGE/registry-seed.json"
cp "$ROOT/frontend/node_modules/@esbuild/linux-x64/bin/esbuild" "$STAGE/libexec/esbuild"
cp "$ROOT/frontend/node_modules/esbuild/LICENSE.md" "$STAGE/licenses/esbuild-MIT"
# Bundle dynamic runtime libraries and loader for the manager. The compiler
# and libraries are built on Ubuntu 22.04 (glibc 2.35), not on musl.
ldd "$BUILD_DIR/forge-cli" | awk '/=> \//{print $3} /^\s*\//{print $1}' | while read -r lib; do cp -L "$lib" "$STAGE/libexec/lib/"; done
cp -L /lib64/ld-linux-x86-64.so.2 "$STAGE/libexec/ld-linux-x86-64.so.2"
cp "$ROOT/vendor/toolchain/LICENSE" "$STAGE/licenses/forge-MIT"
cp "$ROOT/vendor/forge-web/LICENSE" "$STAGE/licenses/forge-web-MIT"
cp "$ROOT/vendor/forge-postgres/LICENSE" "$STAGE/licenses/forge-postgres-MIT"
cp "$ROOT/vendor/forge-storage/LICENSE" "$STAGE/licenses/forge-storage-MIT"
if [ -f /opt/forge-archive/share/licenses/libarchive/COPYING ]; then cp /opt/forge-archive/share/licenses/libarchive/COPYING "$STAGE/licenses/libarchive-COPYING"; fi
cp -R /usr/share/doc/libc6 /usr/share/doc/libcurl4 /usr/share/doc/libjson-c5 /usr/share/doc/libssl3 /usr/share/doc/libmicrohttpd12 /usr/share/doc/libpq5 "$STAGE/licenses/"
find /usr/share/doc -name copyright -exec cp --parents '{}' "$STAGE/licenses" ';'
mkdir -p "$ROOT/releases/$VERSION"
ARCHIVE="forge-$VERSION-linux-x86_64.tar.gz"
tar -czf "$ROOT/releases/$VERSION/$ARCHIVE" -C "$STAGE" .
(cd "$ROOT/releases/$VERSION" && sha256sum "$ARCHIVE" > "$ARCHIVE.sha256")
printf '{"version":"%s","platforms":["linux-x86_64-glibc-2.35"],"channel":"preview"}\n' "$VERSION" > "$ROOT/releases/latest.json"
