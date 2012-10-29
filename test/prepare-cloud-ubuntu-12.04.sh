#!/bin/bash
#script prepare.txt
#documentation needs to be updated. linux-headers needs to be added for ubuntu! 
#subversion added for testing!
#sudo apt-get install -y gcc flex bison libssl-dev libdb-dev linux-source perl make linux-headers subversion
sudo apt-get update
#documentation needs to be updated. linux-headers needs to be added for ubuntu!
sudo apt-get install -y gcc g++ flex bison libssl-dev linux-source perl make linux-headers-`uname -r` zip
# needed for testing
sudo apt-get install -y subversion automake autoconf


#prepare source
cd /usr/src/linux-source-3.2.0/
sudo tar -xjf linux-source-3.2.0.tar.bz2  
cd linux-source-3.2.0/
sudo cp /boot/config-`uname -r` .config
sudo make oldconfig
sudo make prepare 

#install db4
cd ~
wget http://devorange.clemson.edu/pvfs/db-4.8.30.tar.gz
tar zxf db-4.8.30.tar.gz
cd db-4.8.30/build_unix
../dist/configure --prefix=/opt/db4
make 
sudo make install

#install torque
sudo apt-get install -y torque-server torque-scheduler torque-client

#exit
