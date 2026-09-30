FROM ubuntu:22.04
RUN sed -i 's|http://archive.ubuntu.com/ubuntu|http://mirror.kakao.com/ubuntu|g; s|http://security.ubuntu.com/ubuntu|http://mirror.kakao.com/ubuntu|g' /etc/apt/sources.list && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends build-essential cmake pkg-config libpq-dev libmicrohttpd-dev libjson-c-dev libcurl4-openssl-dev libssl-dev ca-certificates git curl && rm -rf /var/lib/apt/lists/*
WORKDIR /src
