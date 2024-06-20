
FROM alpine:latest

#RUN apk update && apk add --no-cache fcgi sqlite curl

RUN apk add --no-cache --virtual .rt-deps \
   sqlite-libs curl openssl fcgi libwebsockets

RUN apk add --no-cache --virtual .build-deps \
   build-base fcgi-dev sqlite-dev curl-dev libwebsockets-dev libuv-dev libev-dev

#RUN apk add --no-cache --virtual .build-deps-static \
#   sqlite-static curl-static openssl-libs-static zlib-static libpsl-static nghttp2-static libidn2-static c-ares-static zstd-static libunistring-static brotli-static libuv-static

# Build cchat.
COPY . /cchat
WORKDIR /cchat
RUN make clean && make && mv cchat /usr/local/bin

# Cleanup build env.
RUN apk del .build-deps
RUN rm -rf /cchat

# Create data path.
RUN mkdir /data
WORKDIR /data

CMD ["/usr/local/bin/cchat", "-l", "/data/cchat.log", "-b", "/data/chat.db"]

