#!/usr/bin/env bash
set -euo pipefail
VERSION=0.3.0-preview.2
BASE_URL=${FORGE_DOWNLOAD_BASE:-https://github.com/forge-language/forge-preview}
INSTALL_ROOT=${FORGE_HOME:-"$HOME/.forge"}
PROFILE_ROOT=${FORGE_PROFILE_ROOT:-"$HOME"}
MODIFY_PATH=1
ACTION=install
usage() { echo 'Usage: bash install.sh [--version VERSION] [--prefix DIR] [--no-modify-path] [--uninstall]'; }
while (($#)); do
 case "$1" in
  --version|--prefix) [[ $# -ge 2 ]] || { usage; exit 1; }; if [[ $1 == --version ]]; then VERSION=$2; else INSTALL_ROOT=$2; fi; shift 2 ;;
  --no-modify-path) MODIFY_PATH=0; shift ;;
  --uninstall) ACTION=uninstall; shift ;;
  --help|-h) usage; exit 0 ;;
  *) echo "Unknown option: $1" >&2; exit 1 ;;
 esac
done
fail() { echo "forge installer: $*" >&2; exit 1; }
[[ $VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.]+)?$ ]] || fail 'Invalid version'
[[ $INSTALL_ROOT == /* && $INSTALL_ROOT != / && $INSTALL_ROOT != "$HOME" && $INSTALL_ROOT != /usr && $INSTALL_ROOT != /usr/local && $INSTALL_ROOT != /opt ]] || fail 'Use a dedicated absolute installation directory'
[[ $INSTALL_ROOT != *$'\n'* && $INSTALL_ROOT != *"'"* && $INSTALL_ROOT != *'"'* && $INSTALL_ROOT != *'$'* && $INSTALL_ROOT != *'`'* && $INSTALL_ROOT != *'..'* ]] || fail 'Unsupported installation path'
[[ ! -L $INSTALL_ROOT ]] || fail 'Installation root must not be a symlink'
remove_path_block() {
 local profile=$1 temp
 [[ -f $profile ]] || return 0
 temp=$(mktemp "${profile}.forge.XXXXXX")
 awk '/^# >>> forge environment >>>$/{skip=1;next}/^# <<< forge environment <<<$/{skip=0;next}!skip{print}' "$profile" > "$temp"
 cat "$temp" > "$profile"; rm -f "$temp"
}
if [[ $ACTION == uninstall ]]; then
 [[ -f "$INSTALL_ROOT/.forge-install" && $(cat "$INSTALL_ROOT/.forge-install") == forge-install-v1 ]] || fail 'Directory is not managed by Forge'
 for profile in "$PROFILE_ROOT/.profile" "$PROFILE_ROOT/.bashrc" "$PROFILE_ROOT/.zshrc"; do remove_path_block "$profile"; done
 rm -rf -- "$INSTALL_ROOT"
 echo 'Forge uninstalled'; exit 0
fi
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || fail 'This release supports Linux x86_64 with glibc 2.35+ only'
command -v getconf >/dev/null || fail 'glibc is required'
glibc=$(getconf GNU_LIBC_VERSION 2>/dev/null) || fail 'musl is not supported by this release'
glibc=${glibc#glibc }; major=${glibc%%.*}; minor=${glibc#*.}; minor=${minor%%.*}
(( major > 2 || (major == 2 && minor >= 35) )) || fail 'glibc 2.35 or newer is required (Ubuntu 22.04+)'
for tool in curl tar sha256sum cc; do command -v "$tool" >/dev/null || fail "Required command missing: $tool"; done
case "$BASE_URL" in https://*) ;; http://localhost:*|http://127.0.0.1:*) ;; *) fail 'Download URL must use HTTPS; loopback HTTP is allowed for local hosting' ;; esac
[[ $BASE_URL =~ ^https?://[a-zA-Z0-9:./_-]+$ ]] || fail 'Invalid download origin'
BASE_URL=${BASE_URL%/}
if [[ -d $INSTALL_ROOT && ! -f $INSTALL_ROOT/.forge-install ]]; then
 [[ -z $(ls -A "$INSTALL_ROOT") ]] || fail 'Installation directory contains unmanaged files'
fi
mkdir -p "$INSTALL_ROOT/toolchains" "$INSTALL_ROOT/bin"
printf '%s\n' forge-install-v1 > "$INSTALL_ROOT/.forge-install"
[[ ! -L "$INSTALL_ROOT/toolchains" && ! -L "$INSTALL_ROOT/bin" ]] || fail 'Managed directories must not be symlinks'
TEMP_DIR=$(mktemp -d "$INSTALL_ROOT/.install.XXXXXX")
trap 'rm -rf -- "$TEMP_DIR"' EXIT
archive="forge-$VERSION-linux-x86_64.tar.gz"
if [[ $BASE_URL == https://github.com/forge-language/forge-preview || $BASE_URL == https://github.com/Helloworld0822/forge ]]; then
 RELEASE_URL="$BASE_URL/releases/download/v$VERSION/$archive"
 INSTALLER_URL=https://raw.githubusercontent.com/forge-language/forge-platform/main/scripts/install.sh
 REGISTRY_DEFAULT=builtin
else
 RELEASE_URL="$BASE_URL/releases/$VERSION/$archive"
 INSTALLER_URL="$BASE_URL/install.sh"
 REGISTRY_DEFAULT="$BASE_URL"
fi
curl --location --proto-redir '=https' --proto '=https,http' --fail --silent --show-error --connect-timeout 10 --max-time 300 "$RELEASE_URL" -o "$TEMP_DIR/$archive"
curl --location --proto-redir '=https' --proto '=https,http' --fail --silent --show-error --connect-timeout 10 --max-time 30 "$RELEASE_URL.sha256" -o "$TEMP_DIR/checksum"
expected=$(awk 'NR==1{print $1}' "$TEMP_DIR/checksum")
[[ $expected =~ ^[a-fA-F0-9]{64}$ ]] || fail 'Invalid SHA-256 checksum'
actual=$(sha256sum "$TEMP_DIR/$archive"); actual=${actual%% *}
[[ $actual == "$expected" ]] || fail 'SHA-256 mismatch; existing installation was preserved'
# Reject path traversal before extraction; archives contain only our toolchain.
while IFS= read -r entry; do
 [[ $entry != /* && $entry != ../* && $entry != */../* && $entry != *$'\n'* ]] || fail 'Unsafe archive path'
done < <(tar -tzf "$TEMP_DIR/$archive")
mkdir "$TEMP_DIR/toolchain"
tar -xzf "$TEMP_DIR/$archive" -C "$TEMP_DIR/toolchain" --no-same-owner
[[ -x "$TEMP_DIR/toolchain/build/bin/forge" && -x "$TEMP_DIR/toolchain/libexec/forge-pm" ]] || fail 'Release is incomplete'
"$TEMP_DIR/toolchain/build/bin/forge" --help >/dev/null 2>&1 || fail 'Compiler does not run on this platform'
# Each update installs into a fresh directory and changes one symlink atomically.
DEST="$INSTALL_ROOT/toolchains/$VERSION-$(date +%s)-$$"
mv "$TEMP_DIR/toolchain" "$DEST"
ln -s "$DEST" "$TEMP_DIR/current"
mv -Tf "$TEMP_DIR/current" "$INSTALL_ROOT/current"
cat > "$TEMP_DIR/forge" <<WRAPPER
#!/usr/bin/env bash
set -euo pipefail
export FORGE_ROOT='$INSTALL_ROOT/current'
export FORGE_REGISTRY=\${FORGE_REGISTRY:-'$REGISTRY_DEFAULT'}
case \${1:-} in
 pkg) shift; exec '$INSTALL_ROOT/bin/forge-pm' "\$@" ;;
 init|build|run) exec '$INSTALL_ROOT/bin/forge-pm' "\$@" ;;
 update) curl -fsSL '$INSTALLER_URL' | FORGE_DOWNLOAD_BASE='$BASE_URL' bash -s -- --prefix '$INSTALL_ROOT' ;;
 uninstall) curl -fsSL '$INSTALLER_URL' | bash -s -- --prefix '$INSTALL_ROOT' --uninstall ;;
 --version) echo 'forge $VERSION'; exit 0 ;;
 *) exec "\$FORGE_ROOT/build/bin/forge" "\$@" ;;
esac
WRAPPER
cat > "$TEMP_DIR/forge-pm" <<WRAPPER
#!/usr/bin/env bash
set -euo pipefail
export FORGE_ROOT='$INSTALL_ROOT/current'
export FORGE_REGISTRY=\${FORGE_REGISTRY:-'$REGISTRY_DEFAULT'}
exec "\$FORGE_ROOT/libexec/ld-linux-x86-64.so.2" --library-path "\$FORGE_ROOT/libexec/lib" "\$FORGE_ROOT/libexec/forge-pm" "\$@"
WRAPPER
chmod 755 "$TEMP_DIR/forge" "$TEMP_DIR/forge-pm"
mv -f "$TEMP_DIR/forge" "$INSTALL_ROOT/bin/forge"
mv -f "$TEMP_DIR/forge-pm" "$INSTALL_ROOT/bin/forge-pm"
printf 'export PATH="%s/bin:$PATH"\n' "$INSTALL_ROOT" > "$INSTALL_ROOT/env"
if (( MODIFY_PATH )); then
 for profile in "$PROFILE_ROOT/.profile" "$PROFILE_ROOT/.bashrc"; do
  touch "$profile"; remove_path_block "$profile"
  printf '\n# >>> forge environment >>>\n. "%s/env"\n# <<< forge environment <<<\n' "$INSTALL_ROOT" >> "$profile"
 done
 if [[ ${SHELL:-} == */zsh || -f "$PROFILE_ROOT/.zshrc" ]]; then
  touch "$PROFILE_ROOT/.zshrc"; remove_path_block "$PROFILE_ROOT/.zshrc"
  printf '\n# >>> forge environment >>>\n. "%s/env"\n# <<< forge environment <<<\n' "$INSTALL_ROOT" >> "$PROFILE_ROOT/.zshrc"
 fi
fi
"$INSTALL_ROOT/bin/forge-pm" --version
printf 'Forge %s installed. Activate: source "%s/env"\n' "$VERSION" "$INSTALL_ROOT"
