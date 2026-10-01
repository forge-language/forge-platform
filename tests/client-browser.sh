#!/bin/sh
set -eu
ROOT=${CLIENT_ROOT:-$(mktemp -d /tmp/forge-browser-client.XXXXXX)}
mkdir -p "$ROOT/home" "$ROOT/project"
export FORGE_DOWNLOAD_BASE=http://localhost:18101
bash scripts/install.sh --prefix "$ROOT/toolchain" --no-modify-path >/dev/null
export PATH="$ROOT/toolchain/bin:$PATH"
cd "$ROOT/project"
forge init browser-app
forge pkg add forge-browser 0.1.3
cat > main.fg <<'PROGRAM'
import browser;
import web;
native main {
 let root: int=browser.root();browser.el(root,"h1","설치된 Forge 브라우저 앱","");
 let value: int=web.parse("{\"id\":9007199254740993}");browser.el(root,"p",web.dump(web.get(value,"id")),"");return 0;
}
PROGRAM
forge build --emit-js
printf '%s\n' '<!doctype html><meta charset="utf-8"><div id="app"></div><script src="build/app.js"></script>' > index.html
echo 'Installed browser module build passed'
echo "Browser client project: $ROOT/project"
