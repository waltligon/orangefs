+++
title= "Source Build Guide"
weight=30
+++


This topic provides an example of a complete installation of OrangeFS
from source in a single procedure. It can also be used as a *Quick
Start* reference for experienced users who wish to bypass the more
detailed and segmented instructions in the earlier topics of this
manual.

{{<notice note>}}Most of the following steps require that you have root
permissions.{{</notice>}}

If your distro includes the OrangeFS kernel module, you may skip tasks
related to building and installing the kernel module. To determine if
your distro includes the OrangeFS kernel module, issue this command:

{{<code>}}modprobe orangefs{{</code>}}
  
You must install OrangeFS every time you update the kernel.

### Assumptions

The following assumptions apply to this example installation:

-   Network protocol is TCP/IP

-   Uses Default security mode
-   Any firewall must be configured to allow clients and servers to
    communicate.

### Build Prerequisites

Prerequisites for RHEL, SUSE and Ubuntu are documented below.

#### RHEL

The system on which you build OrangeFS requires eight additional Linux
software packages. Following are the names for these packages on a
system running RHEL:

|  |
|---|
| gcc |
| flex |
| bison |
| openssl-devel|
| libattr-devel |
| kernel-devel|
| perl |
| make |

To automatically install these packages, enter the following command:

{{<code>}}yum -y install gcc flex bison openssl-devel libattr-devel kernel-devel perl make
{{</code>}}
 

#### SUSE

Following are the names for the packages required on a system running
SUSE:

|  |
|---|
| automake |
| bison |
| kernel-source | 
| libopenssl-devel |
| gcc |
| flex |
| kernel-syms | 
| libattr-devel |


To automatically install these packages, enter the following command
using zypper:

{{<code>}}zypper install automake gcc bison flex kernel-source kernel-syms libopenssl-devel libattr-devel{{</code>}}
 
Additionally, for SUSE, you must prepare the kernel source using the
following commands:

{{<code>}}cp /boot/config-`uname -r` /usr/src/linux-`uname -r | sed s/-[\d].*//`/.config
cd /usr/src/linux-`uname -r | sed s/-[\d].*//`
make oldconfig
make modules_prepare
make prepare
ln -s /lib/modules/`uname -r`/build/Module.symvers /lib/modules/`uname -r`/source
{{</code>}}

{{<notice note>}}Ensure SELinux is set to "permissive" or "disabled", or go through the details of enabling all components for SELinux.{{</notice>}}

#### Ubuntu

Following are the names for the packages required on a system running
Ubuntu:
| |
|---|
| automake | 
| bison | 
| libattr-devel |
| build-essential | 
| flex | 
| libattrl | 

To automatically install these packages, enter the following command
using apt:

{{<code>}}apt install automake build-essential bison flex libattr1 libattr1-dev{{</code>}}

### Installation Steps

To build OrangeFS, complete the following steps:

##### 1. Download and extract the OrangeFS software:

-   Download the source from  
    [http://orangefs.com/download/](http://orangefs.com/download/).

-   Extract the source tar archive:

    {{<code>}}tar -xzf orangefs-VERSION.tar.gz{{</code>}}

-   Change Directory (cd) to the extracted directory:

    {{<code>}}cd orangefs-VERSION{{</code>}}

-   Configure the OrangeFS installation location and the path of the
    system kernel:

    {{%notice note%}}If using a distro which includes the upstream OrangeFS
kernel module, omit `--with-kernel=KERNEL_PATH`{{%/notice%}}

    {{<code>}}./configure --prefix=/opt/orangefs --with-kernel=KERNEL_PATH --with-db-backend=lmdb{{</code>}}

    where...

    `KERNEL_PATH` = path to kernel source

    Examples:  
    RHEL:  /lib/modules/\`uname -r\`/build  
    SUSE:  /lib/modules/\`uname -r\`/source  
    Ubuntu 16.04 (or earlier):  /lib/modules/\`uname -r\`/build  

    {{<notice note>}}Every update will require rebuilding if the distro does not include the OrangeFS kernel module.{{</notice>}}

##### 2. Build and install the software:

{{<code>}}make
make install{{</code>}}

If using a distro which does not include the upstream OrangeFS kernel
module, type the following additional commands:

{{<code>}}make kmod
make kmod_prefix=/opt/orangefs kmod_install{{</code>}}

##### 3.  Create a server configuration file  
 by running the automatic file generation program (pvfs2-genconfig) and answering the prompts.

{{<code>}}/opt/orangefs/bin/pvfs2-genconfig /opt/orangefs/etc/orangefs-server.conf{{</code>}}

<!-- TODO: is the first bullet necessary? which step 3 are we referring to? -->
{{%notice note%}}During the pvfs2-genconfig process:  

 - Use the directories you created in Step 3 for your storage and log
file locations.  
 - Each host you specify should be the value returned by the hostname
command.{{%/notice%}}

This places a server configuration file (named orangefs-server.conf in
this example) in the etc directory.

### Add Clients (Kernel Module)

##### 1.  To add the software required for an OrangeFS Linux client interface  

Change Directory (cd) to /opt on the Client system and copy the /opt/orangefs directory from the Build system:

{{<code>}}scp -rp HOSTNAME:/opt/orangefs /opt{{</code>}}

where...

`HOSTNAME` = host name of the build system

##### 2.  Insert the client kernel module.

This module (pvfs2.ko) resides in the OrangeFS installation directory
several directory layers deep. To insert the module without specifying a
long path, include this find statement:   

{{<code>}}insmod ‘find /opt/orangefs -name pvfs2.ko‘{{</code>}}

{{<notice note>}}If using a distro which includes OrangeFS, omit Step 2.{{</notice>}}

##### 3.  Start the client process on each Client system:

{{<code>}}/opt/orangefs/sbin/pvfs2-client{{</code>}}

##### 4.  Create a directory in the Client system's /mnt directory

This is where the client will mount OrangeFS:

{{<code>}}mkdir /mnt/orangefs{{</code>}}

##### 5.  Determine the URL of the OrangeFS server you will mount.

You can retrieve this information from the orangefs-server.conf file.
For example, the first server URL listed in that file can be extracted
with the following command:

{{<code>}}grep "Alias " /opt/orangefs/etc/orangefs-server.conf | awk '{ print $3 }' | head -n 1{{</code>}}

The format to use for server URL is protocol://hostname:port.

Example:  tcp://server1:3334

##### 6.  Create a file named pvfs2tab in the /etc directory
This tells the system how to mount OrangeFS. Assign read access to the file.

{{<code>}}echo "tcp://server1:3334/orangefs /mnt/orangefs pvfs2" >> /etc/pvfs2tab{{</code>}}

### Add Servers

##### 1.  Add the required software to an OrangeFS server 

Change Directory (cd) to /opt on the Server system and copy the /opt/orangefs directory from the Build system:

{{<code>}}scp -rp HOSTNAME:/opt/orangefs /opt{{</code>}}

where...

`HOSTNAME` = host name of the build system

##### 2.  Initialize the server storage space on each server:  

<!-- TODO: should we include the -a option for the quickstart? -->
{{<code>}}/opt/orangefs/sbin/pvfs2-server -f /opt/orangefs/etc/orangefs-server.conf -a ALIAS_NAME{{</code>}}

##### 3.  Start the server processes on each server:

{{<code>}}/opt/orangefs/sbin/pvfs2-server /opt/orangefs/etc/orangefs-server.conf -a ALIAS_NAME{{</code>}}

### Mount the File System

Mount OrangeFS through the server URL you retrieved earlier:

{{<code>}}mount -t pvfs2 tcp://server1:3334/orangefs /mnt/orangefs{{</code>}}

 

 

 

 

 

 

 

 

 

 
