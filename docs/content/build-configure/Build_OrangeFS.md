+++
title= "Build OrangeFS"
weight=120
+++

  --------------------- 
  ![](icon_build.png)   Building OrangeFS involves downloading the source software from orangefs.org onto a system preconfigured with several standard Linux packages. On this system you will extract and build OrangeFS into a portable directory named /opt/orangefs.
                        
  This topic provides the procedure for building OrangeFS.
  --------------------- 

#### System Requirements

In addition to a supported distribution of Linux, the OrangeFS Build
system requires eight more Linux software packages. The names for these
packages vary from one Linux distribution to another. For example,
following are the package names you would require on a system running
RHEL:

  ------------------- ------------------- ------------------ ----------
  -   gcc             -   bison           -   db4-devel      -   perl
  -   flex            -   openssl-devel   -   kernel-devel   -   make
  -   libattr-devel                                          
                                                             
  ------------------- ------------------- ------------------ ----------

The method for installing these packages varies among Linux
distributions. For example, to automatically install the required
packages on a system running RHEL, you could enter the following
command:

yum -y install gcc flex bison openssl-devel db4-devel kernel-devel perl
make openldap-devel libattr-devel

 

****Notes   ****If you do not plan to use certificate-based security,
omit the option (openldap-devel) from the command.\
 \
 For more details about supported Linux distributions and other required
Linux packages, see [Preview System
Requirements](Preview_System_Requirements.htm).

#### Procedure

**Important  **Because clients have different requirements for the
OrangeFS build system, please read through all installation instructions
for the client(s) you plan to use BEFORE you build OrangeFS.

To build OrangeFS, follow these steps:

1.  Go to [www.orangefs.org](http://www.orangefs.org) and download the
    compressed tar file into the /tmp/src directory (or similar
    directory for temporary storage). The tar file is named as follows:

orangefs-*version*.tar.gz

where...

*version* = version number of the OrangeFS distribution release

Example: orangefs-2.9.tar.gz

 

2.  Change Directory (cd) to /tmp/src, and extract the compressed tar
    file, then change to the newly created orangefs directory:

tar -xzf orangefs-*version*.tar.gz\
 cd orangefs-*version*

Following is a sample listing of initial directories and files in the
orangefs download directory:

/tmp/src/orangefs-*version* \$ ls\
 aclocal.m4       COPYING    Makefile.in         SecuritySetup\
 AUTHORS          CREDITS    module.mk.in        src\
 autom4te.cache   doc        patches             test\
 cert-utils       examples   prepare             windows\
 ChangeLog        include    pvfs2-config.h.in\
 configure        INSTALL    README\
 configure.in     maint      README.name\_change

3.  Build a Makefile for OrangeFS that includes the installation
    location and the path of the system kernel, using the following
    command line format:

./configure --prefix=/opt/orangefs --with-kernel=kernel\_path
protocol\_options security\_mode\_option

where...

*kernel\_path* = path to kernel source

Examples: /usr/src/kernels/2.6.18-194.17.1.el5-x86\_64/

/lib/modules/\`uname -r\`/build

**Note     **In the second example, \`uname -r\` will return the kernel
version.

 

*protocol\_options* = one of the following:

  --------------------------------- ---------------------------------------
  If your network protocol is...    Include these options:
  TCP                               None, enabled by default
  IB, using Mellanox IB libraries   --with-ib=/usr --without-bmi-tcp
  IB, using OFED                    --with-openib=/usr --without-bmi-tcp
  MX                                --with-mx/user=/usr --without-bmi-tcp
  GM                                --with-gm=/usr --without-bmi-tcp
  --------------------------------- ---------------------------------------

****Note     ****If you must run OrangeFS on more than one network
protocol, please contact [Technical Support](Technical_Support.htm).

 

*security\_mode\_option* = one of the following:

  ------------------------------ --------------------------
  To use this security mode...   Include this option:
  Default                        None, enabled by default
  Key-based                      --enable-security-key
  Certificate-based              --enable-security-cert
  ------------------------------ --------------------------

 

*with\_db\_backend* = one of the following:

  ------------------------------ --------------------------
  To use this security mode...   Include this option:
  Berkeley DB                    None, enabled by default
  LMDB                           --with-db-backend=lmdb
  ------------------------------ --------------------------

**Note     **LMDB is a newer database backend option but is not yet the
default. It will work only for new installs, so you cannot upgrade from
existing Berkeley DB installations.

Example:

./configure --prefix=/opt/orangefs --with-kernel=/lib/modules/\`uname
-r\`/build --enable-security-cert 

**Important  **When using the Upstream Kernel Module, omit
--with-kernel=/lib/modules/\`uname -r\`/build from the above command.

4.  Continue with the standard Linux commands to build and run an
    executable program:

make\
 make install

5.  Compile and install the kernel module that your OrangeFS Linux
    clients will need later.

**Important  **When using the Upstream Kernel Module, do not include the
following code.

make kmod\
 make kmod\_prefix=/opt/orangefs kmod\_install

**Important  **OrangeFS is currently not compatible with SELinux,
integrated into many Linux distributions, so be sure to disable it on
all your Linux installations. If it is not disabled, you will get a
"permission denied" error when you try to run OrangeFS. \
 To disable SELinux, use the following command:\
                 echo 0 \> /seLinux/enforce\
 To prevent SELinux from loading at boot time, edit /etc/seLinux/config
and set the SELINUX value to “disabled”, for example,\
                 SELINUX=disabled\
 The command for disabling SELinux can vary, depending on your Linux
version.

 

 

 

 

 
