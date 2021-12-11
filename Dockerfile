
FROM debian:10

# install build requiremenets
RUN apt update && apt install -y --no-install-recommends \
  make \
  cmake \
  g++ \
  pkg-config

COPY . /root/freediag
RUN cd /root/freediag \
    && mkdir -p build.docker \
    && cd build.docker \
    && cmake .. \
    && make

WORKDIR /root/freediag
