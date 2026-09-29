# This Dockerfile assumes that it is being built from the project root directory, e.g.:
# $ docker build -f docker/azahar-room/Dockerfile -t azahar-room .

# --- Builder ----------------
FROM opensauce04/azahar-build-environment:latest AS builder

# unison_core's streaming/software_video_encoder.cpp (src/core/CMakeLists.txt)
# needs x264/x265 dev headers, which the base image doesn't carry. USER root
# guarantees this RUN can apt-get install regardless of the base image's own
# default user; this stage is discarded after the final COPY below, so it
# doesn't affect the shipped image's user.
USER root
RUN apt-get update && apt-get install -y libx264-dev libx265-dev

RUN mkdir /var/azahar-src/
COPY ./ /var/azahar-src/

RUN mkdir ./builddir/
WORKDIR ./builddir/
RUN cmake /var/azahar-src -G Ninja \
                          -DENABLE_QT=OFF \
                          -DENABLE_GDBSTUB=OFF \
                          -DENABLE_TESTS=OFF \
                          -DENABLE_ROOM=ON \
                          -DENABLE_ROOM_STANDALONE=ON
RUN ninja
RUN mv ./bin/Release/azahar-room /usr/local/bin/

# --- Final ------------------
FROM debian:trixie AS final

RUN apt-get update && apt-get -y full-upgrade
RUN apt-get install -y iputils-ping net-tools

COPY --from=builder /usr/local/bin/azahar-room /usr/local/bin/azahar-room
