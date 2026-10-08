#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
python3 scripts/sync-content.py
PROJECT="forge-registry-check-$$"
compose() { docker compose --env-file tests/config.env -p "$PROJECT" -f compose.yml -f tests/compose.yml -f "${FORGE_TEST_COMPOSE_FILE:-tests/compose.yml}" "$@"; }
trap 'compose down -v --remove-orphans >/dev/null 2>&1' EXIT
compose up -d --no-build --wait
REGISTRY_BASE=http://127.0.0.1:18103 TEST_JWT_SECRET=registry-integration-only python3 tests/registry.py
REGISTRY_BASE=http://127.0.0.1:18103 TEST_JWT_SECRET=registry-integration-only CACHE_REDIS_CONTAINER="$(compose ps -q redis)" python3 tests/cache.py
python3 tests/security.py --base http://127.0.0.1:18103 --rate-tests
npm run build --prefix frontend
node frontend/node_modules/@playwright/test/cli.js test --config tests/playwright.config.ts
# Stateless endpoints must stay responsive when the disposable DB is offline.
compose stop postgres >/dev/null
curl -fsS --max-time 1 http://127.0.0.1:18103/api/health >/dev/null
STATUS=$(curl -sS --max-time 1 -o /dev/null -w '%{http_code}' http://127.0.0.1:18103/api/me)
[ "$STATUS" = 401 ]
