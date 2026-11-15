#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
IMAGE=${FORGE_DISCORD_TEST_IMAGE:-forge-discord-native-builder:validation}
export FORGE_DISCORD_TEST_IMAGE="$IMAGE"
docker build --target toolchain -f forge-discord/Native.Containerfile -t "$IMAGE" .
docker run --rm -v "$ROOT:/src" -w /src "$IMAGE" sh -eu -c '
 sh forge-discord/scripts/build-native.sh forge-discord/src/main.fg forge-discord/build/forge-discord
 sh forge-discord/scripts/build-native.sh forge-discord/tests/config_native.fg build/discord-native/config-test
 sh forge-discord/scripts/build-native.sh forge-discord/tests/permissions_native.fg build/discord-native/permissions-test
 sh forge-discord/scripts/build-native.sh vendor/forge-discord/tests/offline.fg build/discord-native/library-offline
 sh forge-discord/scripts/build-native.sh vendor/forge-discord/tests/gateway_state.fg build/discord-native/library-gateway
 sh forge-discord/scripts/build-native.sh vendor/forge-discord/tests/bot_offline.fg build/discord-native/bot-offline
 sh forge-discord/scripts/build-native.sh forge-discord/tests/library-bot.fg build/discord-native/library-bot
 sh forge-discord/scripts/build-native.sh vendor/forge-discord/examples/ping_bot.fg build/discord-native/ping-example
 sh forge-discord/scripts/build-native.sh vendor/forge-discord/examples/register_ping.fg build/discord-native/register-example
 sh forge-discord/scripts/build-native.sh vendor/forge-discord/examples/echo_bot.fg build/discord-native/echo-example
 cc -std=gnu11 -Wall -Wextra -Werror -I vendor/toolchain/include forge-discord/tests/transport-native.c forge-discord/native/transport.c build/discord-native/toolchain/lib/libforge_runtime.a build/discord-native/toolchain/lib/libforge_std.a $(pkg-config --cflags --libs json-c libcurl openssl icu-uc) -lpthread -lm -o build/discord-native/transport-test
'
docker run --rm -v "$ROOT:/src:ro" -w /tmp -e BOT_CONFIG_DIR=/tmp/forge-discord-config-test -e BOT_CONFIG=/tmp/no-legacy "$IMAGE" /src/build/discord-native/config-test
docker run --rm -v "$ROOT:/src:ro" -w /tmp "$IMAGE" /src/build/discord-native/permissions-test
docker run --rm -v "$ROOT:/src:ro" -w /src/vendor/forge-discord "$IMAGE" /src/build/discord-native/library-offline
docker run --rm -v "$ROOT:/src:ro" -w /tmp "$IMAGE" /src/build/discord-native/library-gateway
docker run --rm -v "$ROOT:/src:ro" -w /tmp "$IMAGE" /src/build/discord-native/bot-offline
node forge-discord/tests/transport-native.mjs
node --test forge-discord/tests/native-integration.test.mjs
node --test forge-discord/tests/library-bot.test.mjs
