#!/bin/bash
# This script launches the WebRTC signalling server after cleaning up the Chrome cache.
sudo rm -rf /home/vagrant/.cache/google-chrome 
sudo rm -rf /home/vagrant/.config/google-chrome
echo "Starting serverRTC ..."
xterm -bg orange -fg blue -e sshpass -p "vagrant" ssh -oStrictHostKeyChecking=no -oGSSAPIAuthentication=no -Y -t vagrant@10.0.0.5 'nodejs /home/vagrant/code/rtcserver/index.js; $SHELL -i' &   #uncomment this line for separate terminal when using GUI
status=$?
if [ $status -eq 0 ]; then
   echo "Starting C1 and C2 ..."
else
   echo "Something went wrong!"
fi



