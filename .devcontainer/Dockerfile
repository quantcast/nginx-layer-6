FROM ubuntu:latest

RUN apt-get update && \
    apt-get -y install systemd init apport sudo git mercurial make gdb \
    build-essential libpcre3 libpcre3-dev zlib1g \
    zlib1g-dev libssl-dev zsh curl python3 vim netcat

WORKDIR /workspaces

RUN hg clone https://hg.nginx.org/nginx#stable-1.22

# Adding scaffolding for testing envrionment

# Add node
ENV NODE_VERSION=16.13.0
RUN curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.0/install.sh | bash

ENV NVM_DIR=/root/.nvm
RUN . "$NVM_DIR/nvm.sh" && nvm install ${NODE_VERSION}
RUN . "$NVM_DIR/nvm.sh" && nvm use v${NODE_VERSION}
RUN . "$NVM_DIR/nvm.sh" && nvm alias default v${NODE_VERSION}

ENV PATH="/root/.nvm/versions/node/v${NODE_VERSION}/bin/:${PATH}"

COPY test-server ./test-server
COPY python-server ./python-server

RUN node --version
RUN npm --version
