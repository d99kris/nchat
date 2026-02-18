# -- Build libsignal (with Rust) --
FROM rust:1-alpine AS rust-builder
RUN apk add --no-cache git make cmake protoc musl-dev g++ clang-dev

WORKDIR /build
# Copy all files needed for Rust build, and no Go files
COPY pkg/libsignalgo/libsignal/. pkg/libsignalgo/libsignal/.
COPY build-rust.sh .

RUN ./build-rust.sh

# -- Build mautrix-signal (with Go) --
FROM golang:1-alpine3.23 AS go-builder
RUN apk add --no-cache git ca-certificates build-base olm-dev zlib-dev

WORKDIR /build
# Copy all files needed for Go build, and no Rust files
COPY *.go go.* *.yaml *.sh ./
COPY pkg/signalmeow/. pkg/signalmeow/.
COPY pkg/libsignalgo/* pkg/libsignalgo/
COPY pkg/libsignalgo/resources/. pkg/libsignalgo/resources/.
COPY pkg/msgconv/. pkg/msgconv/.
COPY pkg/signalid/. pkg/signalid/.
COPY pkg/connector/. pkg/connector/.
COPY cmd/. cmd/.
COPY .git .git

ENV LIBRARY_PATH=.
COPY --from=rust-builder /build/pkg/libsignalgo/libsignal/target/*/libsignal_ffi.a ./
RUN <<EOF
EOF
RUN ./build-go.sh

# -- Run mautrix-signal --
FROM alpine:3.23

ENV UID=1337 \
    GID=1337

RUN apk add --no-cache ffmpeg su-exec ca-certificates bash jq curl yq-go olm

COPY --from=go-builder /build/mautrix-signal /usr/bin/mautrix-signal
COPY --from=go-builder /build/docker-run.sh /docker-run.sh
VOLUME /data

CMD ["/docker-run.sh"]
