#!/bin/bash
#documentation needs to be updated. linux-headers needs to be added for ubuntu! 
#subversion added for testing!
#sudo apt-get install -y gcc flex bison libssl-dev libdb-dev linux-source perl make linux-headers subversion
script prepare.txt
echo "Installing prereqs via yum..."
sudo yum -y install gcc gcc-c++ flex bison openssl-devel db4-devel kernel-devel-`uname -r` kernel-headers-`uname -r` perl make subversion automake autoconf zip &> yum.out

#install db4
echo "Downloading Berkeley DB 4.8.30 from devorange.clemson.edu..."
cd ~
wget -q http://devorange.clemson.edu/pvfs/db-4.8.30.tar.gz
tar zxf db-4.8.30.tar.gz &> /dev/null
cd db-4.8.30/build_unix
echo "Configuring Berkeley DB 4.8.30..."
../dist/configure --prefix=/opt/db4 &> db4conf.out
echo "Building Berkeley DB 4.8.30..."
make &> db4make.out
echo "Installing Berkeley DB 4.8.30 to /opt/db4..."
sudo make install &> db4install.out

echo "Installing TORQUE..."

wget -q -np -r -nd http://devorange.clemson.edu/pvfs/rhel6/RPMS/x86_64/

sudo rpm -i torque*.rpms

exit
exit

