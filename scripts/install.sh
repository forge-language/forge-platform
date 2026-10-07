#!/usr/bin/env bash
set -euo pipefail
VERSION=0.3.0-preview.5
BASE_URL=${FORGE_DOWNLOAD_BASE:-https://github.com/forge-language/forge}
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
[[ $INSTALL_ROOT == /* ]] || fail 'Use a dedicated absolute installation directory'
[[ $INSTALL_ROOT != *$'\n'* && $INSTALL_ROOT != *"'"* && $INSTALL_ROOT != *'"'* && $INSTALL_ROOT != *'$'* && $INSTALL_ROOT != *'`'* && $INSTALL_ROOT != *'..'* ]] || fail 'Unsupported installation path'
command -v realpath >/dev/null || fail 'GNU coreutils realpath is required'
# Normalize lexical separators and dot components without following symlinks.
# A trailing slash would otherwise make bash -L dereference the root symlink.
INSTALL_ROOT=$(realpath -m -s -- "$INSTALL_ROOT") || fail 'Invalid installation path'
[[ $INSTALL_ROOT != / && $INSTALL_ROOT != "$HOME" && $INSTALL_ROOT != /usr && $INSTALL_ROOT != /usr/local && $INSTALL_ROOT != /opt ]] || fail 'Use a dedicated absolute installation directory'
[[ ! -L $INSTALL_ROOT ]] || fail 'Installation root must not be a symlink'
[[ ! -L "$INSTALL_ROOT/toolchains" && ! -L "$INSTALL_ROOT/bin" && ! -L "$INSTALL_ROOT/.forge-install" && ! -L "$INSTALL_ROOT/env" ]] || fail 'Managed paths must not be symlinks'
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
# Installation unpacks prebuilt binaries. A host C compiler is only needed to build a program.
for tool in curl tar sha256sum; do command -v "$tool" >/dev/null || fail "Required command missing: $tool"; done
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
if [[ $BASE_URL == https://github.com/forge-language/forge || $BASE_URL == https://github.com/Helloworld0822/forge ]]; then
 RELEASE_URL="$BASE_URL/releases/download/v$VERSION/$archive"
 INSTALLER_URL=https://raw.githubusercontent.com/forge-language/forge-platform/main/scripts/install.sh
 REGISTRY_DEFAULT=builtin
else
 RELEASE_URL="$BASE_URL/releases/$VERSION/$archive"
 INSTALLER_URL="$BASE_URL/install.sh"
 REGISTRY_DEFAULT="$BASE_URL"
fi
curl --location --proto-redir '=https' --proto '=https,http' --fail --silent --show-error --connect-timeout 10 --max-time 30 --retry 2 --retry-delay 1 "$RELEASE_URL.sha256" -o "$TEMP_DIR/checksum"
expected=$(awk 'NR==1{print $1}' "$TEMP_DIR/checksum")
[[ $expected =~ ^[a-fA-F0-9]{64}$ ]] || fail 'Invalid SHA-256 checksum'
# Keep partial data between attempts: curl's built-in retry discards it.
# Each attempt is bounded to five minutes; three attempts allow slow links
# to finish without changing the active toolchain before checksum validation.
download_archive() {
 local attempt status result
 for attempt in 1 2 3; do
  if status=$(curl --location --proto-redir '=https' --proto '=https,http' --fail --silent --show-error --connect-timeout 10 --max-time 300 --continue-at - --write-out '%{http_code}' "$RELEASE_URL" -o "$TEMP_DIR/$archive"); then
   return 0
  else
   result=$?
  fi
  case "$result" in
   5|6|7|16|18|28|35|52|55|56|92) ;;
   33) rm -f -- "$TEMP_DIR/$archive" ;;
   22)
    case "$status" in
     408|429|500|502|503|504) ;;
     416) rm -f -- "$TEMP_DIR/$archive" ;;
     *) return "$result" ;;
    esac ;;
   *) return "$result" ;;
  esac
  if (( attempt < 3 )); then
   echo "forge installer: download interrupted; retrying ($((attempt + 1))/3)" >&2
   sleep "$attempt"
  fi
 done
 return "$result"
}
download_archive || fail 'Release download failed after retries; existing installation was preserved'
actual=$(sha256sum "$TEMP_DIR/$archive"); actual=${actual%% *}
[[ $actual == "$expected" ]] || fail 'SHA-256 mismatch; existing installation was preserved'
# GNU tar is available on the supported glibc Linux platform. Inspect both
# names and entry types before extracting anything or executing the compiler.
# SDK archives contain only directories and regular files, never links/devices.
LC_ALL=C tar --quoting-style=escape -tzf "$TEMP_DIR/$archive" > "$TEMP_DIR/names" || fail 'Invalid release archive'
LC_ALL=C tar --numeric-owner --quoting-style=escape -tvzf "$TEMP_DIR/$archive" > "$TEMP_DIR/metadata" || fail 'Invalid release archive'
while IFS= read -r entry; do
 [[ $entry != /* && $entry != ../* && $entry != */../* && $entry != */.. && $entry != *\\* ]] || fail 'Unsafe archive path'
done < "$TEMP_DIR/names"
awk '
 substr($0,1,1)!="-" && substr($0,1,1)!="d" {exit 1}
 substr($0,1,10) ~ /[sStT]/ {exit 1}
 $3 !~ /^[0-9]+$/ {exit 1}
 {bytes+=$3; count++; if (bytes>1073741824 || count>100000) exit 1}
 END {if (count==0) exit 1}
' "$TEMP_DIR/metadata" || fail 'Unsafe archive type, permissions or size'
mkdir "$TEMP_DIR/toolchain"
tar -xzf "$TEMP_DIR/$archive" -C "$TEMP_DIR/toolchain" --no-same-owner --no-same-permissions
[[ -x "$TEMP_DIR/toolchain/build/bin/forge" && -x "$TEMP_DIR/toolchain/libexec/forge-pm" ]] || fail 'Release is incomplete'
[[ -x "$TEMP_DIR/toolchain/libexec/ld-linux-x86-64.so.2" && -d "$TEMP_DIR/toolchain/libexec/lib" ]] || fail 'Release is incomplete'
"$TEMP_DIR/toolchain/build/bin/forge" --help >/dev/null 2>&1 || fail 'Compiler does not run on this platform'
"$TEMP_DIR/toolchain/libexec/ld-linux-x86-64.so.2" --library-path "$TEMP_DIR/toolchain/libexec/lib" "$TEMP_DIR/toolchain/libexec/forge-pm" --version >/dev/null 2>&1 || fail 'Package manager does not run on this platform'
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
 init|trust|build|run) exec '$INSTALL_ROOT/bin/forge-pm' "\$@" ;;
 update) curl -fsSL --proto '=https,http' --proto-redir '=https' --connect-timeout 10 --max-time 30 '$INSTALLER_URL' | FORGE_DOWNLOAD_BASE='$BASE_URL' bash -s -- --prefix '$INSTALL_ROOT' ;;
 uninstall) curl -fsSL --proto '=https,http' --proto-redir '=https' --connect-timeout 10 --max-time 30 '$INSTALLER_URL' | bash -s -- --prefix '$INSTALL_ROOT' --uninstall ;;
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
