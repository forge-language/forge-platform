#!/bin/sh
set -eu
PORT=${PORT:-80}
API_UPSTREAM=${FORGE_API_UPSTREAM:-api:8080}
CANONICAL_HOST=${FORGE_CANONICAL_HOST-forge-lang.org}
case "$PORT" in ''|*[!0-9]*) echo 'Invalid HTTP port' >&2; exit 1;; esac
if [ "$PORT" -lt 1 ] || [ "$PORT" -gt 65535 ]; then echo 'Invalid HTTP port' >&2; exit 1; fi
case "$API_UPSTREAM" in ''|*[!A-Za-z0-9.:-]*) echo 'Invalid API upstream' >&2; exit 1;; esac
case "$CANONICAL_HOST" in *[!A-Za-z0-9.:-]*) echo 'Invalid canonical host' >&2; exit 1;; esac
sed -i "s|__FORGE_PORT__|$PORT|g; s|__FORGE_API_UPSTREAM__|$API_UPSTREAM|g; s|__FORGE_CANONICAL_HOST__|$CANONICAL_HOST|g" /etc/nginx/conf.d/default.conf

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
