FROM ubuntu:22.04

RUN apt-get update
RUN apt-get install -y gcc
RUN apt-get install -y netcat iputils-ping

ADD proj2.c /app/
ADD proj2.h /app/
ADD hostsfile.txt /app/
WORKDIR /app/
RUN gcc proj2.c -o proj2


ENTRYPOINT ["/app/proj2"]