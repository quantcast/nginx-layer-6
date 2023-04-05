set follow-fork-mode child
set args -c $PWD/nginx.conf
b src/event/modules/ngx_epoll_module.c:901
