#!/bin/bash
script prepare.txt
#documentation needs to be updated. linux-headers needs to be added for ubuntu! 
#subversion added for testing!
#sudo apt-get install -y gcc flex bison libssl-dev libdb-dev linux-source perl make linux-headers subversion

#fedora 17
sudo yum -y install gcc gcc-c++ flex bison openssl-devel db4-devel kernel-devel-`uname -r` perl make
sudo yum -y install subversion automake autoconf

#install db4
cd ~
wget http://devorange.clemson.edu/pvfs/db-4.8.30.tar.gz
tar zxf db-4.8.30.tar.gz
cd db-4.8.30/build_unix
../dist/configure --prefix=/opt/db4
make 
sudo make install


exit
