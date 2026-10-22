# The command-line tools (ome-cli, ome-lobster) in a small image.
#
#   docker build -t ome-cli .
#   docker run --rm -v "$PWD/examples:/data" ome-cli /data/input1.txt -o /data/output1.txt

FROM ubuntu:24.04 AS build
# Timeouts and retries: the Ubuntu mirror occasionally hangs.
RUN apt-get -o Acquire::Retries=5 -o Acquire::http::Timeout=30 update \
 && apt-get -o Acquire::Retries=5 -o Acquire::http::Timeout=30 install -y --no-install-recommends g++ cmake ninja-build \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOME_BUILD_TESTS=OFF -DOME_BUILD_BENCH=OFF \
 && cmake --build build --target ome-cli ome-lobster

FROM ubuntu:24.04
COPY --from=build /src/build/ome-cli /src/build/ome-lobster /usr/local/bin/
WORKDIR /data
ENTRYPOINT ["ome-cli"]
