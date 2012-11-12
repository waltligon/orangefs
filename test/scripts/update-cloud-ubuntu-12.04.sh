#!/bin/bash -v
#script update.txt
echo "Downloading system updates..."
sudo apt-get -y update &> update.out
sudo apt-get -y dist-upgrade &> dist-upgrade.out
echo "Update successful. Rebooting image..."
sudo reboot 
#exit
exit
