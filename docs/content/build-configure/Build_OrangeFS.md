+++
title= "OrangeFS Build Details"
weight=110
+++

| |
|---|
|Building OrangeFS involves downloading the source software from orangefs.org onto a system preconfigured with several standard Linux packages. On this system you will extract and build OrangeFS into a portable directory named /opt/orangefs.|

System Requirements
-------------------

In addition to a supported distribution of Linux, the OrangeFS Build system requires eight more Linux software packages. The names for these packages vary from one Linux distribution to another. For example, following are the package names you would require on a system running RHEL:

-   gcc
-   flex
-   libattr-devel
-   bison
-   openssl-devel
-   db4-devel
-   kernel-devel
-   perl
-   make

The method for installing these packages varies among Linux distributions. For example, to automatically install the required packages on a system running RHEL, you could enter the following command:

yum -y install gcc flex bison openssl-devel db4-devel kernel-devel perl make openldap-devel libattr-devel

****Notes**** If you do not plan to use certificate-based security, omit the option (openldap-devel) from the command.

Procedure
---------

**Important** Because clients have different requirements for the OrangeFS build system, please read through all installation instructions for the client(s) you plan to use BEFORE you build OrangeFS.

To build OrangeFS, follow these steps:

##### 1. Download Archive  
Go to [www.orangefs.org](http://www.orangefs.org) and download the compressed tar file into the /tmp/src directory (or similar directory for temporary storage). The tar file is named as follows:

orangefs-*version*.tar.gz

where...

*version* = version number of the OrangeFS distribution release

Example: orangefs-2.9.tar.gz

 
##### 2. Extract Archive  
Change Directory (cd) to /tmp/src, and extract the compressed tar file, then change to the newly created orangefs directory:

tar -xzf orangefs-*version*.tar.gz  
cd orangefs-*version*  

Following is a sample listing of initial directories and files in the orangefs download directory:

/tmp/src/orangefs-*version* \$ ls
aclocal.m4                     
AUTHORS                        
autom4te.cache                        
cert-utils                        
ChangeLog            
configure            
configure.in          
COPYING
CREDITS
doc
examples
include
INSTALL
maint 
Makefile.in
module.mk.in
patches
prepare 
pvfs2-config.h.in
README
README.name_change
SecuritySetup 
src
test
windows

##### 3. Build OrangeFS 

Build a Makefile for OrangeFS that includes the installation location and the path of the system kernel, using the following command line format:  

./configure --prefix=/opt/orangefs --with-kernel=kernel_path protocol_options security_mode_option   

where...  

*kernel_path* = path to kernel source  

Examples: /usr/src/kernels/2.6.18-194.17.1.el5-x86_64/  

/lib/modules/\`uname -r\`/build  

**Note** In the second example, \`uname -r\` will return the kernel version.  

*protocol\_options* = one of the following:

| | |
|---|---|
|If your network protocol is...|Include these options:|
|TCP|None, enabled by default|
|IB, using Mellanox IB libraries|--with-ib=/usr --without-bmi-tcp|
|IB, using OFED|--with-openib=/usr --without-bmi-tcp|
|MX|--with-mx/user=/usr --without-bmi-tcp|
|GM|--with-gm=/usr --without-bmi-tcp|

****Note**** If you must run OrangeFS on more than one network protocol, depending on the configuration may slow down faster fabrics to match with slower ones.

*security_mode_option* = one of the following:

| | |
|---|---|
|To use this security mode...|Include this option:|
|Default|None, enabled by default|
|Key-based|--enable-security-key|
|Certificate-based|--enable-security-cert|


*with_db_backend* = one of the following:

||
|---|---|
|To use this security mode...|Include this option:|
|Berkeley DB|None, enabled by default|
|LMDB|--with-db-backend=lmdb|

**Note** LMDB is a newer and preferred database backend option but is not yet the default. It will work only for new installs or existing LMDB installs, you cannot upgrade from existing Berkeley DB installations.

#### Example:

./configure --prefix=/opt/orangefs --with-kernel=/lib/modules/\`uname -r\`/build --enable-security-cert 

**Important  **When using the Upstream Kernel Module, omit --with-kernel=/lib/modules/\`uname -r\` build from the above command.

##### 1.  Continue with the standard Linux commands to build and run an executable program:

make  
make install

##### 2.  Compile and install the kernel module that your OrangeFS Linux clients will need later.

**Important** When using the Upstream Kernel Module, do not include the following code.

make kmod
 make kmod\_prefix=/opt/orangefs kmod\_install

**Important  **OrangeFS by default works with SELinux in Permissive Mode
 To set SELinux to permissive mode, use the following command:  
 echo 0 \> /seLinux/enforce
 
 To prevent SELinux from loading at boot time, edit /etc/seLinux/config and set the SELINUX value to “disabled”, for example:  
 SELINUX=disabled. 
 
 The command can vary, depending on your Linux version.


 

 

 

 

 
