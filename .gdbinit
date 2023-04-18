set follow-fork-mode child
set args -c $PWD/nginx.conf
b httplite_request.c:242
r
