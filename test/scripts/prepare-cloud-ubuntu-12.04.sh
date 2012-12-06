#!/bin/bash
#script prepare.txt
#documentation needs to be updated. linux-headers needs to be added for ubuntu! 
#subversion added for testing!
#sudo apt-get install -y gcc flex bison libssl-dev libdb-dev linux-source perl make linux-headers subversion
sudo apt-get update > /dev/null
#documentation needs to be updated. linux-headers needs to be added for ubuntu!
sudo apt-get install -y gcc g++ flex bison libssl-dev linux-source perl make linux-headers-`uname -r` zip subversion automake autoconf &> apt.out


#prepare source
SOURCENAME=`find /usr/src -name "linux-source*" -type d -prune -printf %f`
cd /usr/src/${SOURCENAME}
sudo tar -xjf ${SOURCENAME}.tar.bz2  &> /dev/null
cd ${SOURCENAME}/
sudo cp /boot/config-`uname -r` .config
sudo make oldconfig &> /dev/null
sudo make prepare &>/dev/null

#install db4
cd ~
wget -q http://devorange.clemson.edu/pvfs/db-4.8.30.tar.gz
tar zxf db-4.8.30.tar.gz &> /dev/null
cd db-4.8.30/build_unix 
../dist/configure --prefix=/opt/db4 &> db4conf.out
make &> db4make.out
sudo make install &> db4install.out

#install torque
sudo apt-get install -y torque-server torque-scheduler torque-client &> torque.out

#exit
