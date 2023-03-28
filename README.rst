===============================================================================
HTTP Layer 6 Load Balancing with NGINX
===============================================================================

This module provides HTTP load balancing in NGINX at Optimized for Layer 6,
trading features for efficiency.

You can run this module using a linux system and the run script in the scripts folder 
of this directory.

Run using ``./scripts/run -c`` when running the module and changes have been made to the
nginx configuration. Otherwise, use ``./scripts/run`` to recompile and run the module.

If developing on windows there is a dockerfile that will be used for a remote
development container. More information on this to come.

Furthermore, to test a localhost connection, there are sample bodies provided in the `test/requests`
directory. These sample bodies can be retrieved in either one recv call (`post_body_1024.txt`) or two
(`post_body_2048.txt`), assuming a read size of 1024 bytes. You can make such requests using `curl`, as shown:
```curl -X POST '127.0.0.1:8888' -d "$(cat test/requests/<file_name>.txt)"```

However, we made it easier that you make request calls by using a file that assembles them for us.
For example to make a POST call to post_body_2048.txt file, use this script

./scripts/make-request -h localhost -p 8888 -f ./test/requests/post_body_2048.txt

for a GET request, omit the -f part.