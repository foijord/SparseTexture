FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt update
RUN apt install -y wget cmake build-essential

RUN wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | apt-key add -
RUN wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.4.304-noble.list https://packages.lunarg.com/vulkan/1.4.304/lunarg-vulkan-1.4.304-noble.list
RUN apt update
RUN apt install -y vulkan-sdk

WORKDIR /SparseTexture
COPY ./build.sh /usr/local/bin/build
RUN chmod +x /usr/local/bin/build

CMD ["build"]
