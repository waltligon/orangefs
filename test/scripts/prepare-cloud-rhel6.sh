#!/bin/bash
script prepare.txt
#documentation needs to be updated. linux-headers needs to be added for ubuntu! 
#subversion added for testing!
#sudo apt-get install -y gcc flex bison libssl-dev libdb-dev linux-source perl make linux-headers subversion

sudo yum -y install gcc gcc-c++ flex bison openssl-devel db4-devel kernel-devel-`uname -r` perl make subversion automake autoconf zip &> yum.out

#install db4
cd ~
wget -q http://devorange.clemson.edu/pvfs/db-4.8.30.tar.gz
tar zxf db-4.8.30.tar.gz &> /dev/null
cd db-4.8.30/build_unix
../dist/configure --prefix=/opt/db4 &> db4conf.out
make 1&2> db4make.out
sudo make install 1&2> db4install.out

echo '[atrpms]
name=Fedora Core $releasever - $basearch - ATrpms
baseurl=http://dl.atrpms.net/el$releasever-$basearch/atrpms/stable
gpgkey=http://ATrpms.net/RPM-GPG-KEY.atrpms
gpgcheck=1' > ~/atrpms.repo

sudo cp ~/atrpms.repo /etc/yum.repos.d/
sudo rpm --import http://packages.atrpms.net/RPM-GPG-KEY.atrpms &> importkey.out
sudo yum -y install torque torque-server &> torque.out


exit