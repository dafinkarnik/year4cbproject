This directory contains the testbed.

Please refer to Instructions_to_use.txt to learn how to run it.
The code directory contains the WebRTC JS application in /rtcclient and the signalling server in /rtcserver.
The chromium.tar.gz archive contains a Linux debug build of Chromium v65.0.3287.0 with the Circuit Breaker implementation. 
The Vagrantfile is used to run the VM with Vagrant.
The file headless.py contains the Mininet environment that runs the setup in headless mode.
The file gui.py runs the browsers with their GUIs.
The run_signal_server.sh script is used in headless.py and run_signal_server_gui.sh is used in gui.py.
They contain the command for running the signal server.
The two .y4m files were obtained from xiph.org and are used in the testbed but are not a product of this project.