#!/bin/sh
set -eu
BASE=${FORGE_DOWNLOAD_BASE:-http://localhost:18101}
case "$BASE" in *[!A-Za-z0-9:./-]*) echo 'Invalid Forge download origin' >&2; exit 1;; esac
sed -i "s|^BASE_URL=.*$|BASE_URL=\${FORGE_DOWNLOAD_BASE:-$BASE}|" /usr/share/nginx/html/install.sh
