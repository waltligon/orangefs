#!/bin/bash 

# must script to work around sudo issue


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
        
                echo "Downloading system updates for ubuntu based system $CHOICE..."
                sudo apt-get -y -q update 
                sudo apt-get -y -q dist-upgrade < /dev/null 
                echo "Update successful. Rebooting image..."
                sudo reboot 
                # break
                ;;
                
        cloud-*suse*)
        
                echo "Downloading system updates for suse based system $CHOICE..."
                #sudo yum update --disableexcludes=main -y
                sudo zypper --non-interactive update
                echo "Update successful. Rebooting image..."
                sudo /sbin/reboot
                # break
                ;;
                
        cloud-rhel*|cloud-centos*|cloud-sl6*|cloud-fedora*)
        
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
        
