FROM ubuntu

RUN apt-get update && \
    apt-get -y install sudo && \
    sudo apt-get -y install git && \
    sudo apt-get -y install mercurial

RUN hg clone https://hg.nginx.org/nginx#stable-1.22

ADD nginx-layer-6-key /
RUN chmod 600 /nginx-layer-6-key && \
    echo "IdentityFile /nginx-layer-6-key" >> /etc/ssh/ssh_config && \  
    echo -e "StrictHostKeyChecking no" >> /etc/ssh/ssh_config && \  
    git clone https://github.com/quantcast/nginx-layer-6/tree/development