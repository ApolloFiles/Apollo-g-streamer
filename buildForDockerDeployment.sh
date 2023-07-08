#!/bin/sh
set -e

docker build --pull -t apollo_g_streamer_cpp:builder .
docker run --rm --volume "$(realpath ./docker-build-output/):/build-output/" apollo_g_streamer_cpp:builder
