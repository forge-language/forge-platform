FROM alpine:3.23 AS toolchain
RUN apk add --no-cache build-base cmake pkgconf json-c-dev libmicrohttpd-dev curl-dev openssl-dev icu-dev
WORKDIR /src
COPY vendor/toolchain vendor/toolchain
COPY vendor/forge-web vendor/forge-web
COPY vendor/forge-discord vendor/forge-discord
COPY forge-discord/native forge-discord/native
COPY forge-discord/scripts/build-native.sh forge-discord/scripts/build-native.sh
RUN cmake -S vendor/toolchain -B build/discord-native/toolchain -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build/discord-native/toolchain -j4 \
 && cmake -S vendor/forge-web -B build/discord-native/web -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
 && cmake --build build/discord-native/web -j4

FROM toolchain AS build
COPY forge-discord/src forge-discord/src
RUN sh forge-discord/scripts/build-native.sh

FROM alpine:3.23 AS runtime
RUN apk add --no-cache libcurl json-c libmicrohttpd openssl icu-libs ca-certificates \
 && mkdir -p /app/data/guilds && chown -R 65534:65534 /app
WORKDIR /app
COPY --from=build /src/build/forge-discord /usr/local/bin/forge-discord
COPY --from=build /src/build/forge-discord-register /usr/local/bin/forge-discord-register
COPY --from=build /src/build/forge-discord-check /usr/local/bin/forge-discord-check
USER 65534:65534
CMD ["forge-discord"]
