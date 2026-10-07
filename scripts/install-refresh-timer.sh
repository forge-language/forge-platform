#!/bin/sh
# Explicitly installs user service templates; does not alter the production stack.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case "$ROOT" in *"'"*|*'
'*) echo 'Unsupported workspace path' >&2; exit 1;; esac
UNIT_DIR="$HOME/.config/systemd/user"
RUN_DIR="$HOME/.local/lib/forge-public-refresh"
mkdir -p "$UNIT_DIR" "$RUN_DIR"
cat > "$RUN_DIR/run" <<RUN
#!/bin/sh
exec /usr/bin/python3 '$ROOT/scripts/refresh-public-data.py' --output '$ROOT/public-data'
RUN
chmod 700 "$RUN_DIR/run"
cp "$ROOT/deployment/systemd/forge-public-refresh.service" "$UNIT_DIR/"
cp "$ROOT/deployment/systemd/forge-public-refresh.timer" "$UNIT_DIR/"
systemctl --user daemon-reload
systemctl --user enable --now forge-public-refresh.timer
printf '%s\n' 'Public refresh timer installed; existing Forge services were not changed.'
