+++
title= "Package Installation Guide"
weight=20
+++

Packages for OrangeFS are available in the Fedora repositories. Packages
for CentOS are available in Fedora's EPEL ([Extra Packages for
Enterprise Linux](https://fedoraproject.org/wiki/EPEL)) repository.  The
packages are available on Fedora 27 and later and CentOS 7 and later.

A number of packages are available.  The OrangeFS package provides the
client and the orangefs-server package provides the server.  The
orangefs-devel package is required to build additional programs against
OrangeFS.  There is also an orangefs-fuse package providing FUSE
support, which is not described by this guide.

The kernel module is not provided by any of the above packages.  It is
now available in mainline Linux.  In Fedora, it is installed by the
kernel-core package, which means it is installed by default.  On CentOS,
it is not available with the default kernel.  A newer kernel can be
obtained from the [ELRepo
project](http://www.elrepo.org/tiki/tiki-index.php).

The kernel module requires a userspace client (often called the
pvfs2-client-core).  This is provided by the OrangeFS package.  The
userspace client is not required to run non-kernel OrangeFS clients,
such as the administrative programs also provided by the OrangeFS
package.

Install the Packages
--------------------

### Install on Fedora

To install on Fedora, issue the following command:  
dnf -y install orangefs orangefs-server

### Install on CentOS

To install on CentOS, issue the following commands:

yum -y install epel-release  
yum -y install orangefs orangefs-server

#### Install the ELRepo Kernel

If the kernel module will be used on CentOS, the ELRepo kernel must be
installed. To install the ELRepo kernel, issue the following commands:

rpm --import https://www.elrepo.org/RPM-GPG-KEY-elrepo.org  
rpm -Uvh
 http://www.elrepo.org/elrepo-release-7.0-3.el7.elrepo.noarch.rpm  
yum -y --enablerepo=elrepo-kernel install kernel-ml

### Add Servers

To add servers, complete the following steps:

1.  Install the necessary packages on each machine as described above.
    An example server configuration is provided in
    /etc/orangefs/orangefs.conf.  It will suffice for a single-server
    installation.  If necessary, the default hostname localhost should
    be changed. Otherwise, if multiple servers will be used, generate a
    configuration using:  
    pvfs2-genconfig

2.  Copy the configuration to each server machine.  
    scp -pr hostname:/etc/orangefs/orangefs.conf /etc/orangefs/orangefs.conf

3.  Initialize the filesystem and start on each machine.  
    pvfs2-server -f /etc/orangefs/orangefs.conf  
    systemctl start orangefs-server  
    **Note** The filesystem should only be initialized once.    

4.  In the future each server can be started manually.  
    systemctl start orangefs-server  

5.  Start the server at boot.  
    systemctl enable orangefs-server  

### Add Clients

To add clients, complete the following steps:

1.  Install the necessary packages on each machine as described above.
2.  An example client configuration is provided in /etc/pvfs2tab.  It is
    commented by default.  Uncomment it by removing the leading '\#',
    then change the hostname if necessary.  
3.  Change the default mount point, /orangefs, if necessary.
4.  Copy the client configuration to each client machine.  
    scp -pr hostname:/etc/pvfs2tab /etc/pvfs2tab

5.  Test connectivity to the server  
    pvfs2-ping -m /orangef  

6.  If everything is working correctly, the pvfs2-ping utility will
    output similar to the following:
{{<code>}}
$ pvfs2-ping -m /orangefs

(1) Parsing tab file...

(2) Initializing system interface...

(3) Initializing each file system found in tab file: /etc/pvfs2tab...

   PVFS2 servers: tcp://localhost:3334
   Storage name: orangefs
   Local mount point: /orangefs
   /orangefs: Ok

(4) Searching for /orangefs in pvfstab...

   PVFS2 servers: tcp://localhost:3334
   Storage name: orangefs
   Local mount point: /pvfsmnt

   meta servers:
   tcp://localhost:3334

   data servers:
   tcp://localhost:3334

(5) Verifying that all servers are responding...

   meta servers:
   tcp://localhost:3334 Ok

   data servers:
   tcp://localhost:3334 Ok

(6) Verifying that fsid 1 is acceptable to all servers...

   Ok; all servers understand fs_id 1

(7) Verifying that root handle is owned by one server...

   Root handle: 1048576
     Ok; root handle is owned by exactly one server.

=============================================================

The PVFS2 filesystem at /pvfsmnt appears to be correctly configured.

{{</code>}}

7.   If the kernel module will not be used, OrangeFS is now installed;
    otherwise, the kernel module will be used. To load the kernel
    module, issue the following command:  
    modprobe orangefs  

8.  Start the client with the following command:  
    systemctl start orangefs-client  

9.  Next, mount the filesystem.  Change the hostname and mountpoint if
    necessary.  
    mount -t pvfs2 tcp://localhost:3334/orangefs /pvfsmnt  

The filesystem is now mounted.

 

 

 

 

 

 

 

 

 
