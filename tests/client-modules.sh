#!/bin/sh
set -eu
# Run inside the Ubuntu 22.04 release image with development libraries installed.
ROOT=$(mktemp -d /tmp/forge-client-check.XXXXXX)
trap 'rm -rf "$ROOT"' EXIT
mkdir -p "$ROOT/home" "$ROOT/project"
export FORGE_DOWNLOAD_BASE=http://localhost:18101
bash /src/scripts/install.sh --prefix "$ROOT/toolchain" --no-modify-path >/dev/null
export PATH="$ROOT/toolchain/bin:$PATH"
cd "$ROOT/project"
forge init module-app
forge pkg add forge-postgres 0.1.1
forge pkg add forge-web 0.1.2
cat > main.fg <<'PROGRAM'
import postgres;
import web;
native main {
 web.scope_begin();
 let params: int=postgres.params();postgres.push(params,"bound value");postgres.params_close(params);
 let value: int=web.object();web.set(value,"number",web.number(9007199254740993));println(web.dump(value));
 web.scope_end();return 0;
}
PROGRAM
forge build
./build/app | grep '9007199254740993'
forge pkg list | grep 'git_commit'
echo 'Installed native module build passed'
