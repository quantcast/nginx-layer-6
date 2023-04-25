===============================================================================
HTTP Layer 6 Load Balancing with NGINX
===============================================================================

This module provides HTTP load balancing in NGINX at Optimized for Layer 6,
trading features for efficiency.

You can run this module using a linux system and the run script in the scripts folder 
of this directory.

A Docker image has been provided in the ``.devcontainer`` folder. 

A run script has been provided for ease of use when developing.
Before using the script, however, a ``.env`` file must be made which exports an ``NGINX_PATH`` variable with the directory to the Nginx path.
If you are running the Docker image in a container, you can put ``export NGINX_PATH=../nginx`` in your ``.env`` file.

You can use ``./scripts/run`` to recompile and run the module.
If changes have been made to the module's configuration (i.e. files have been added, configs have been changed, etc.), then run with the ``-c`` argument.
If you want a list of all options when running, use the ``-h`` flag.
To run integration tests, you can run ``./scripts/test.``

Finally, to start a mock server, you can use the ``test-server`` folder in ``.devcontainer,`` or if you are in a Docker container you can ``cd`` into ``/workspaces/test-server.``
To start a simple echo server, run ``npm start <port>``.
