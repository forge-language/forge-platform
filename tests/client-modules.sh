#!/bin/sh
set -eu
# Run inside the Ubuntu 22.04 release image with development libraries installed.
ROOT=$(mktemp -d /tmp/forge-client-check.XXXXXX)
trap 'rm -rf "$ROOT"' EXIT
mkdir -p "$ROOT/home" "$ROOT/project"
export FORGE_DOWNLOAD_BASE=${FORGE_DOWNLOAD_BASE:-http://localhost:18101}
export FORGE_REGISTRY=${FORGE_REGISTRY:-builtin}
export FORGE_PROFILE_ROOT="$ROOT/home"
bash /src/scripts/install.sh --prefix "$ROOT/toolchain" --no-modify-path >/dev/null
export PATH="$ROOT/toolchain/bin:$PATH"
cd "$ROOT/project"
forge init module-app
cd module-app
forge pkg add forge-postgres 0.1.1
forge pkg add forge-web 0.1.2
# Current release checks require exact execution grants. Legacy public SDK
# smoke tests must opt in explicitly; other trust failures remain fatal.
case "${FORGE_TEST_ALLOW_LEGACY_TRUST:-0}" in 0|1) ;; *) echo 'Invalid legacy trust opt-in' >&2; exit 1;; esac
trust_test_dependency() {
 package=$1
 if forge trust "$package" > "$ROOT/trust-output" 2>&1; then
  python3 - "$package" <<'PYTHON'
import json, sys
from pathlib import Path
name = sys.argv[1]
pin = json.loads(Path('forge.lock').read_text())['packages'][name]
grant = json.loads(Path('forge.json').read_text())['trust'][name]
assert pin['repository_url'].startswith('https://github.com/forge-language/')
assert grant['repository_url'] == pin['repository_url'].removesuffix('.git')
assert grant['git_commit'] == pin['git_commit'] and grant['native'] is True
assert not grant.get('npm_scripts', False)
print('Verified pinned native test trust:', name, pin['git_commit'])
PYTHON
 else
  if [ "${FORGE_TEST_ALLOW_LEGACY_TRUST:-0}" = 1 ] &&
     grep -q '^forge-pm: Unknown command\. Commands:' "$ROOT/trust-output" &&
     ! grep -q 'trust' "$ROOT/trust-output"; then
   echo "Legacy SDK: trust command unavailable for $package; execution-trust checks are not covered"
  else
   cat "$ROOT/trust-output" >&2
   echo 'Dependency trust failed; legacy public SDK checks require explicit FORGE_TEST_ALLOW_LEGACY_TRUST=1' >&2
   exit 1
  fi
 fi
}
trust_test_dependency forge-postgres
trust_test_dependency forge-web
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
