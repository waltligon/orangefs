+++
title= "FUSE Client"
weight=360
+++

Filesystem in Userspace (FUSE) is a loadable kernel module for UNIX-like
computer operating systems that lets non-privileged users create their
own file systems without editing kernel code. File system code is run in
user space while the FUSE module provides only a bridge to the actual
kernel interfaces.

The OrangeFS client interface for FUSE enables access to an OrangeFS
file system from a Mac.

*Note* While FUSE can run on both Linux and Mac. Linux users will achieve better results with the up-stream Linux Kernel module and
Direct Interface.

Setting up a FUSE client involves four main steps on your Mac system:

-   [Install FUSE](#Install_FUSE)

-   [Install OrangeFS](#Install_OrangeFS)

-   [Mount an OrangeFS Server](#Mount_an_OrangeFS_Server)

-   [Set up Security](FUSE_Client.htm#Set_Up_Security)

Install FUSE
------------

The recommended FUSE distribution for the Mac is Fuse4x.

Fuse4x can be downloaded using Apple's port mechanism or from the Web at
[http://fuse4x.github.io/](http://fuse4x.github.io/).

To get fuse4x using the port command:

port install fuse4x

**Note     ** At the time of this Installation Guide’s initial release,
Fuse4x had been recently tested on OrangeFS using the Darwin Kernel
Version 11.4.2.

Fuse4x documentation on its website will guide you through the
installation process.

Install OrangeFS
----------------

OrangeFS must be downloaded and built in the OS X environment on your
Mac system.

**Important  **Copying the OrangeFS installation directory from your
Linux Build system to a Mac system will not work. You must build it
separately.

### Prerequisites

Prior to installing OrangeFS, you must install gcc, flex, bison, make,
and openssl-devel as described in [Additional Linux Software for the
Build
System](Preview_System_Requirements.htm#Additional_Linux_Software_For_The_Build_System).

### Procedure

To build OrangeFS on your Mac system, follow these steps:

1.  Go to [www.orangefs.org](http://www.orangefs.org/download/). If your
    OrangeFS filesystem is using the latest released version, select

orangefs-*\<version\>*

where...

*\<version\>* = version number of the OrangeFS distribution release

Example: orangefs-2.9. \
 \
 If your OrangeFS filesystem is using an older version, select the [New
releases](http://www.orangefs.org/downloads/old-releases/) in the
Previous Releases section on the OrangeFS downloads page.  select your
release then select the source link to find the tar ball.

**Notes   **The OrangeFS client and servers MUST be using the same
version.\
 \
 For systems using releases older than 2.8.2-1, the tar balls can be
found by selecting the [Previous
releases](ftp://ftp.parl.clemson.edu/pub/pvfs2/old) link; however, these
releases have not been recently tested.  Use at your own discretion.\
 \
 If using Safari to download, the tar ball will be automatically
unzipped, producing an orangefs-*\<version\>*.tar file.  In Firefox, you
will download a zipped tar ball, orangefs-*\<version\>*.tar.gz.

2.  Change directory (cd) to

/Users/*username*/Downloads,

where...

*username* is the Mac username of the person creating this interface.

Locate one of the following tar balls:

orangefs-\<*version*\>.tar or orangefs-\<*version*\>.tar.gz

and extract the OrangeFS source files using one of the following
commands:

Unzipped:

tar -xf orangefs-\<*version*\>.tar

Zipped:

tar -xzf orangefs-\<*version*\>.tar.gz

Then, change your working directory (cd)  to orangefs-\<*version*\>.

-xzf orangefs-\<*version*\>.tar.gz\
 cd orangefs-*\<version\>*

3.  Build a Makefile for OrangeFS that includes the installation
    location and four other options as follows:

./configure --prefix=/opt/orangefs --disable-server --disable-usrint
--disable-opt --enable-fuse

4.  Continue with the standard Linux commands to build and run an
    executable program:

make\
 make install

This will create the OrangeFS installation directory in /opt/orangefs.
Within that directory, the binary you need to run FUSE, pvfs2fuse, will
be located in the bin directory.

Mount an OrangeFS Filesystem
----------------------------

Assuming you have network access to the OrangeFS filesystem, you must
first create a mount point on your Mac.

**Note     **Confirm access to any of the servers using the ping
command.

To mount an OrangeFS Server:

1.  Create a directory as the mount point.  This directory can be
    anywhere on your Mac where you have create permissions:

mkdir /mnt/orangefs

2.  The FUSE client requires an OrangeFS filesystem specification
    defined as

*URL*/\<*filesystem name\>*

where...

*URL*= any ONE of the OrangeFS servers that manages your filesystem,
found with the filesystem name in the OrangeFS server conf file

Example: orangefs-server.conf.

The URL value for each server in the filesystem is listed in the
\<Aliases\> section, while the filesystem name is listed in the
\<Filesystem\> section.

\<Aliases\>\
     clemson1 tcp://server1:3334\
     tiger1 tcp://server2:3334\
 \</Aliases\>\
 \
 \<Filesystem\>\
   Name \<*filesystem name*\>\
 ...\
 \</Filesystem\>

The filesystem spec in this case is one of two choices:

tcp://server1:3334/\<*filesystem name*\>\
 tcp://server2:3334/\<*filesystem name*\>

3.  Now you are ready to mount an OrangeFS filesystem, as follows:

/opt/orangefs/bin/pvfs2fuse /mnt/orangefs -o
fs\_spec=tcp://server1:3334/\<*filesystem name*\>

**Notes   **In the above example, tcp://server1:3334 is the URL of only
one of the OrangeFS servers managing the given filesystem, determined in
Step 2; /mnt/orangefs is the mount point created in Step 1.\
 \
 If 'root' issues the pvfs2fuse command, then all users of your Mac can
access the filesystem.  However, if *\<username\>* issues the command,
only *\<username\>* has access.

Once the mount is successful, you can access your OrangeFS installation
using common commands like ls and cp.  For example:

ls /mnt/orangefs

Set up Security
---------------

### Using Default Security

By default, OrangeFS uses User and Group IDs to enforce file
permissions.  Files created using your Mac will be stored with your Mac
UID and primary GID, so only you have access to your files.  However, if
you created files using one of the other OrangeFS clients, you might not
have access to your files from the Mac, unless your Mac UID (or GID)
happens to match the UID (or GID) in these other environments.

To alleviate this problem, you or your Mac administrator can create User
IDs having the same UIDs and GIDs that match across platforms.  We
suggest that you do NOT change an existing user's UID or primary GID.
Instead, we recommend you create a new ID having the appropriate UID/GID
values.  

Below is an example (Darwin Kernel Version 11.4.2) that creates a Mac
user called "orangefs", with a specific UID and GID from the command
line:

\$ sudo dscl . create /Users/orangefs uid 500\
 \$ sudo dscl . create /Users/orangefs gid 5005\
 \$ sudo dscl . create /Users/orangefs shell /bin/bash\
 \$ sudo dscl . create /Users/orangefs home /Users/orangefs\
 \$ sudo dscl . create /Users/orangefs realname "orangefs"\
 \$ sudo dscl . create /Groups/orangefs gid 5005\
 \$ sudo dscl . create /Groups/orangefs passwd \\\*

To create an "orangefs" group and set its GID to 5005:

\$ sudo dscl . create /Groups/orangefs gid 5005\
 \$ sudo dscl . create /Groups/orangefs passwd '\*'

To add a user to this group:

\$ sudo dscl . merge /Groups/orangefs users \<*username*\>

### Using Key Security

If your OrangeFS file system is using key security, then the OrangeFS
FUSE client must be built with key security enabled.  Add
--enable-security-key to the "configure" command:

./configure --prefix=/opt/orangefs --disable-server --disable-usrint
--disable-opt --enable-fuse --enable-security-key

Your OrangeFS system administrator will typically create a
public/private key pair for your Mac and will give you the private key
to store on your machine.  By storing the private key as

\<prefix\>/etc/pvfs2-clientkey.pem

or, as in the above configure command,

/opt/orangefs/etc/pvfs2-clientkey.pem

pvfs2fuse will automatically find and use this key.  If you store the
key with a different name or in a different location, you must first
define the PVFS2KEY\_FILE environment variable before issuing the
pvfs2fuse command:

\$ export PVFS2KEY\_FILE=\<path-to-key\>/\<*key filename*\>

Your OrangeFS system administrator must create the public/private key
pair using the hostname of your Mac.  To determine the hostname, first
ensure that you can ping at least one of the OrangeFS server machines.
  Then, issue the hostname command to get the value needed by the system
administrator to create the correct public/private key pair.

**Notes   **The system administrator must add the public key to the
OrangeFS keystore, copy the keystore to each server machine, then
restart the servers before you will have access to the file system.  See
[Setting up Key-Based Security
Mode](Setting_Up_Key-Based_Security_Mode.htm) for more information.\
 \
 If your Mac has a non-static IP address in your environment, you will
have to regenerate a new public/private key pair each time the address
changes.\
 \
 The default security using UID and GID for file permissions is used in
addition to the public/private key pair.  See [Using Default
Security](FUSE_Client.htm#Using_Default_Security) above.

 

 

 

 

 
