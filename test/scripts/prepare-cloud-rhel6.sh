#!/bin/bash
script prepare.txt
#documentation needs to be updated. linux-headers needs to be added for ubuntu! 
#subversion added for testing!
#sudo apt-get install -y gcc flex bison libssl-dev libdb-dev linux-source perl make linux-headers subversion

#fedora 17
sudo yum -y install gcc gcc-c++ flex bison openssl-devel db4-devel kernel-devel-`uname -r` perl make
sudo yum -y install subversion automake autoconf

sudo rpm -i ~/rpm/RPMS/x86_64/db4*.rpm

rpm -q db4-local

exit
