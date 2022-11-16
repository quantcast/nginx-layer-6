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
```curl '127.0.0.1:8888' -d "$(cat test/requests/<file_name>.txt)"```
If you print the `request->start` parameter as a string, you should see the contents of the presentation http request.
