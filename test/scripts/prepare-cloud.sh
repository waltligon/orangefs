#!/bin/bash

# use: prepare-cloud <system>

# must script to work around sudo issue

if [ $1 ] 
then
	VMSYSTEM=$1
fi

CHOICE=$VMSYSTEM

# get the architecture
if [[ $CHOICE =~ 'i386' ]]
then
        ARCH=i386
else
        ARCH=x86_64
fi
        
SYSTEM=`echo $CHOICE | sed s/^cloud-// | sed s/-i386//`

# switch on Choice

# ubuntu/debian based distributions

case $CHOICE in
        cloud-*buntu*|cloud-*mint*|cloud-debian*)

                echo "Preparing Ubuntu based distribution $CHOICE"
        
                sudo apt-get update > /dev/null
                #documentation needs to be updated. linux-headers needs to be added for ubuntu!
                sudo apt-get install -y -q gcc g++ flex bison libssl-dev linux-source perl make linux-headers-`uname -r` zip subversion automake autoconf < /dev/null


                #prepare source
                SOURCENAME=`find /usr/src -name "linux-source*" -type d -prune -printf %f`
                cd /usr/src/${SOURCENAME}
                sudo tar -xjf ${SOURCENAME}.tar.bz2  &> /dev/null
                cd ${SOURCENAME}/
                sudo cp /boot/config-`uname -r` .config
                sudo make oldconfig &> /dev/null
                sudo make prepare &>/dev/null

                #install torque
                echo "Installing TORQUE from apt-get"
                sudo apt-get install -y -q torque-server torque-scheduler torque-client < /dev/null 

                #break
                ;;
        cloud-*suse*)

                echo "Preparing SUSE based distribution $CHOICE"
        
                echo "Installing prereqs via zypper..."
                #sudo yum -y install gcc gcc-c++ flex bison openssl-devel db4-devel kernel-devel-`uname -r` kernel-headers-`uname -r` perl make subversion automake autoconf zip &> yum.out
                zypper --non-interactive install gcc gcc-c++ flex bison libopenssl-devel kernel-source kernel-syms kernel-devel perl make subversion automake autoconf zip
                #install db4

                cd /usr/src/linux-`uname -r | sed s/-[\d].*//`
                sudo cp /boot/config-`uname -r` .config
                sudo make oldconfig &> /dev/null
                sudo make modules_prepare &>/dev/null
                sudo ln -s /lib/modules/`uname -r`/build/Module.symvers /lib/modules/`uname -r`/source


                echo "Installing TORQUE from devorange: "
                echo "wget -r -np -nd http://devorange.clemson.edu/pvfs/${SYSTEM}/RPMS/${ARCH}/"
                wget -r -np -nd http://devorange.clemson.edu/pvfs/${SYSTEM}/RPMS/${ARCH}/
                #cd  devorange.clemson.edu/pvfs/openSUSE-12.2/RPMS/x86_64
                ls *.rpm
                sudo rpm -e libtorque2
                sudo rpm -ivh *.rpm
                cd -
                
                # break
                ;;
                
        cloud-rhel*|cloud-centos*|cloud-sl6*|cloud-fedora*)
        
                echo "Preparing RedHat based distribution $CHOICE"
                
                echo "Installing prereqs via yum..."
                sudo yum -y install gcc gcc-c++ flex bison openssl-devel db4-devel kernel-devel-`uname -r` kernel-headers-`uname -r` perl make subversion automake autoconf zip &> yum.out


                echo "Installing TORQUE from devorange: "
                echo "wget -r -np -nd http://devorange.clemson.edu/pvfs/${SYSTEM}/RPMS/${ARCH}/"
                wget -r -np -nd http://devorange.clemson.edu/pvfs/${SYSTEM}/RPMS/${ARCH}/
                ls *.rpm
                sudo rpm -ivh torque*.rpm

                ;;
        *)
                echo "System $CHOICE not supported."
                exit 1
                exit 1
                ;;
esac

#install db4 - all systems
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
exit
exit

