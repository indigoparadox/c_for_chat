
FROM alpine:latest

#RUN apk update && apk add --no-cache fcgi sqlite curl

RUN apk add --no-cache --virtual .build-deps \
   build-base sqlite-static curl-static openssl-libs-static zlib-static libpsl-static nghttp2-static libidn2-static c-ares-static zstd-static libunistring-static brotli-static fcgi-dev sqlite-dev curl-dev libwebsockets-dev libuv-dev libuv-static libev-dev

COPY . /cchat

WORKDIR /cchat

RUN make clean && make BUILD=STATIC

RUN apk del .build-deps

CMD ["./cchat"]

