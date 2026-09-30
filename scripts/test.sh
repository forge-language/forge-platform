#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
PROJECT="forge-registry-check-$$"
compose() { docker compose --env-file tests/config.env -p "$PROJECT" -f compose.yml -f tests/compose.yml "$@"; }
trap 'compose down -v --remove-orphans >/dev/null 2>&1' EXIT
compose up -d --no-build --wait
REGISTRY_BASE=http://127.0.0.1:18103 TEST_JWT_SECRET=registry-integration-only python3 tests/registry.py
npm run build --prefix frontend
node frontend/node_modules/@playwright/test/cli.js test --config tests/playwright.config.ts
