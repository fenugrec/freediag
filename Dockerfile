# @v0.0.1
FROM sea5kg/wsjcpp:latest
# install build requiremenets
RUN apt update && apt install -y --no-install-recommends \
  libfltk1.3-dev \

COPY . /root/freediag
RUN cd /root/freediag \
    && mkdir -p tmp.docker \
    && cd tmp.docker \
    && cmake .. \
    && make

WORKDIR /root/project