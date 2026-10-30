#!/bin/sh
set -eu
BASE=${FORGE_DOWNLOAD_BASE:-http://localhost:18101}
case "$BASE" in *[!A-Za-z0-9:./-]*) echo 'Invalid Forge download origin' >&2; exit 1;; esac
sed -i "s|^BASE_URL=.*FORGE_DOWNLOAD_BASE.*$|BASE_URL=\${FORGE_DOWNLOAD_BASE:-$BASE}|" /usr/share/nginx/html/install.sh

WRITE_RATE=${FORGE_WRITE_RATE:-20r/m}
WRITE_BURST=${FORGE_WRITE_BURST:-5}
case "$WRITE_RATE" in *[!0-9r/sm]*) echo 'Invalid write rate' >&2; exit 1;; esac
case "$WRITE_BURST" in ''|*[!0-9]*) echo 'Invalid write burst' >&2; exit 1;; esac
sed -i "s|__FORGE_WRITE_RATE__|$WRITE_RATE|g; s|__FORGE_WRITE_BURST__|$WRITE_BURST|g" /etc/nginx/conf.d/default.conf

# The release mount can be empty in a fresh checkout. Always provide Nginx's
# map internally; copy a reviewed host map without writing the read-only mount.
mkdir -p /etc/nginx/forge
if [ -f /srv/releases/artifacts.conf ]; then
 cp /srv/releases/artifacts.conf /etc/nginx/forge/release-artifacts.conf.new
else
 printf '%s\n' 'map $uri $forge_release_url { default ""; }' > /etc/nginx/forge/release-artifacts.conf.new
fi
mv /etc/nginx/forge/release-artifacts.conf.new /etc/nginx/forge/release-artifacts.conf
