FROM ubuntu:22.04
RUN sed -i 's|http://archive.ubuntu.com/ubuntu|http://mirror.kakao.com/ubuntu|g; s|http://security.ubuntu.com/ubuntu|http://mirror.kakao.com/ubuntu|g' /etc/apt/sources.list && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends build-essential cmake pkg-config libpq-dev libmicrohttpd-dev libjson-c-dev libcurl4-openssl-dev libssl-dev libhiredis-dev libarchive-dev ca-certificates git curl && rm -rf /var/lib/apt/lists/*
# Pin the official source and published GitHub release asset SHA-256. Only the
# gzip filter and tar reader are registered by forge-pm. Disabling XML avoids
# dragging libxml2 and its large ICU runtime into the downloadable SDK.
ARG LIBARCHIVE_VERSION=3.8.9
ARG LIBARCHIVE_SHA256=f5a6539059cf5e597dbeda37bfa4874b1e8dea063c8d93bf85a2b44af90a5bd4
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends zlib1g-dev && rm -rf /var/lib/apt/lists/* \
 && curl --fail --location --retry 2 --connect-timeout 10 --max-time 60 "https://github.com/libarchive/libarchive/releases/download/v${LIBARCHIVE_VERSION}/libarchive-${LIBARCHIVE_VERSION}.tar.gz" -o /tmp/libarchive.tar.gz \
 && printf '%s  /tmp/libarchive.tar.gz\n' "$LIBARCHIVE_SHA256" | sha256sum -c - \
 && mkdir /tmp/libarchive-source && tar -xzf /tmp/libarchive.tar.gz -C /tmp/libarchive-source --strip-components=1 \
 && cd /tmp/libarchive-source \
 && ./configure --prefix=/opt/forge-archive --enable-shared --disable-static --disable-bsdtar --disable-bsdcpio --disable-bsdcat --disable-bsdunzip --disable-acl --disable-xattr --without-xml2 --without-expat --without-iconv --without-bz2lib --without-lzma --without-lz4 --without-zstd --without-lzo2 --without-libb2 --without-nettle --without-openssl \
 && make -j4 && make install \
 && mkdir -p /opt/forge-archive/share/licenses/libarchive && cp COPYING /opt/forge-archive/share/licenses/libarchive/ \
 && rm -rf /tmp/libarchive-source /tmp/libarchive.tar.gz
ENV PKG_CONFIG_PATH=/opt/forge-archive/lib/pkgconfig
ENV LIBRARY_PATH=/opt/forge-archive/lib
ENV LD_LIBRARY_PATH=/opt/forge-archive/lib
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends python3 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
