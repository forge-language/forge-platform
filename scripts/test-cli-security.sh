#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${FORGE_CLI_BUILD_DIR:-"$ROOT/build/glibc"}
export FORGE_CLI_STAGE="$BUILD_DIR/release-stage"
[ -x "$FORGE_CLI_STAGE/libexec/forge-pm" ]
RUN_DIR=$(mktemp -d)
trap 'rm -rf "$RUN_DIR"' EXIT
MANAGER="$RUN_DIR/manager"
cat > "$MANAGER" <<'WRAPPER'
#!/bin/sh
exec "$FORGE_CLI_STAGE/libexec/ld-linux-x86-64.so.2" --library-path "$FORGE_CLI_STAGE/libexec/lib" "$FORGE_CLI_STAGE/libexec/forge-pm" "$@"
WRAPPER
chmod 700 "$MANAGER"
cd "$ROOT"
GIT_MASTER=1 FORGE_PM="$MANAGER" python3 tests/build-cache.py
GIT_MASTER=1 FORGE_PM="$MANAGER" python3 tests/package-security.py
GIT_MASTER=1 FORGE_PM_BINARY="$MANAGER" python3 tests/package-artifacts.py
