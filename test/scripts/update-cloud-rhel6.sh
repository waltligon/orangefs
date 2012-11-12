#!/bin/bash
script update.txt
echo "Downloading system updates..."
sudo yum update --disableexcludes=main -y
# Uninstall the old kernel
echo "Removing old kernel `uname -r`..."
sudo rpm -e kernel-`uname -r`
# Update grub from current kernel to installed kernel
echo "Updating /boot/grub/grub.conf to kernel `rpm -q --queryformat '%{VERSION}-%{RELEASE}.%{ARCH}\n' kernel`"
sudo perl -e "s/`uname -r`/`rpm -q --queryformat '%{VERSION}-%{RELEASE}.%{ARCH}\n' kernel`/g" -p -i /boot/grub/grub.conf 
echo "Update successful. Rebooting image..."
sudo reboot
exit
exit
