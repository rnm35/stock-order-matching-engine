# The command-line tools (ome-cli, ome-lobster) in a small image.
#
#   docker build -t ome-cli .
#   docker run --rm -v "$PWD/examples:/data" ome-cli /data/input1.txt -o /data/output1.txt
#
# Alpine rather than Ubuntu: its CDN-backed package mirror has been far more reliable in CI
# than archive.ubuntu.com. The binaries are linked statically (musl + libstdc++), so the
# runtime image needs nothing but the two executables.

FROM alpine:3.24.2 AS build
RUN apk add --no-cache g++ make cmake
WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOME_BUILD_TESTS=OFF -DOME_BUILD_BENCH=OFF \
          -DCMAKE_EXE_LINKER_FLAGS=-static \
 && cmake --build build --target ome-cli ome-lobster --parallel 4

FROM alpine:3.24.2
COPY --from=build /src/build/ome-cli /src/build/ome-lobster /usr/local/bin/
WORKDIR /data
ENTRYPOINT ["ome-cli"]
