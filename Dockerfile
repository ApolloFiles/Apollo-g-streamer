# syntax=docker/dockerfile:1
FROM debian:11

RUN apt-get update &&  \
    apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    wget \
    file \
    bash-completion \
    libgstreamer1.0-dev \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

WORKDIR /app/

COPY CMakeLists.txt ./CMakeLists.txt
COPY src/ ./src/

RUN cmake . && \
    make

CMD bash -c "mkdir --verbose --parents /build-output/ && cp --verbose ./apollo_g_streamer /build-output/ && file ./apollo_g_streamer"
