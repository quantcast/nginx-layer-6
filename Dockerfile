FROM ubuntu:latest

RUN apt-get update && \
    apt-get -y install sudo git mercurial make \
    build-essential libpcre3 libpcre3-dev zlib1g \
    zlib1g-dev libssl-dev zsh curl

RUN hg clone https://hg.nginx.org/nginx#stable-1.22
