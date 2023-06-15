FROM docker.io/library/debian:12 AS builder

RUN apt update
RUN apt install -y \
      build-essential \
      pkg-config \
      cmake \
      clang

RUN apt install -y \
      libsecp256k1-dev \
      libreadline-dev \
      libunac1-dev \
      libssl-dev \
      libleveldb-dev \
      git

WORKDIR /src
ADD . /src

RUN make all_release -j4

# Build final image
FROM debian:12-slim
RUN apt update
RUN apt install -y \
      libleveldb1d \
      libsecp256k1-1 \
      libssl3 \
      libunac1

WORKDIR /app

COPY --from=builder /src/build/release/bin /app/bin
COPY --from=builder /src/conf /app/conf
COPY --from=builder /src/www /app/www

EXPOSE 10000
VOLUME /app/data

CMD /app/bin/nostr_server
