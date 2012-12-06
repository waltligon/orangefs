#!/bin/bash 

# must script to work around sudo issue
script update.txt

CHOICE=$VMSYSTEM

# get the architecture
if [[ $CHOICE =~ 'i386' ]]
then
	$ARCH=i386
else
	$ARCH=x86_64
fi
	
$SYSTEM=`echo $CHOICE | sed s/^cloud-// | sed s/-i386//`

# switch on Choice

# ubuntu/debian based distributions

case $CHOICE in
	cloud-ubuntu-12.04)
	cloud-ubuntu-11.04)
	cloud-ubuntu-10.10)
	cloud-ubuntu-8.04)
	cloud-ubuntu-12.04-i386)
	cloud-ubuntu-11.10)
	cloud-ubuntu-11.10-i386)
	cloud-ubuntu-10.04)
	cloud-ubuntu-11.04-i386)
	cloud-ubuntu-10.10-i386)
	cloud-ubuntu-10.04-i386)
	cloud-ubuntu-8.04-i386)

		echo "Downloading system updates for ubuntu based system $CHOICE..."
		sudo apt-get -y update &> update.out
		sudo apt-get -y dist-upgrade &> dist-upgrade.out
		echo "Update successful. Rebooting image..."
		sudo reboot 
		# break
		;;
		
	cloud-opensuse-12.2)
	
		echo "Downloading system updates for suse based system $CHOICE..."
		#sudo yum update --disableexcludes=main -y
		sudo zypper --non-interactive update
		echo "Update successful. Rebooting image..."
		sudo /sbin/reboot
		# break
		;;
		
	cloud-rhel6)
	cloud-rhel5)
	cloud-centos6)
	cloud-rhel5-i386)
	cloud-sl6-i386)
	cloud-sl6)
	cloud-fedora17)
	cloud-fedora17-i386)
	
		echo "Downloading system updates for RedHat based system $CHOICE..."
		sudo yum update --disableexcludes=main -y
		# Uninstall the old kernel
		echo "Removing old kernel `uname -r`..."
		sudo rpm -e kernel-`uname -r`
		#Update grub from current kernel to installed kernel
		echo "Updating /boot/grub/grub.conf to kernel `rpm -q --queryformat '%{VERSION}-%{RELEASE}.%{ARCH}\n' kernel`"
				sudo perl -e "s/`uname -r`/`rpm -q --queryformat '%{VERSION}-%{RELEASE}.%{ARCH}\n' kernel`/g" -p -i /boot/grub/grub.conf 
		echo "Update successful. Rebooting image..."
		sudo reboot
		# break
		;;
	*)
		echo "System $CHOICE not supported."
		exit 1
		exit 1
		;;
esac

exit
exit
	