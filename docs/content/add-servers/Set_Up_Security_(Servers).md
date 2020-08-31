+++
title= "Set up Server Security"
weight=220
+++

|  |  |
|---|---|
| {{<figure src="../images/icon_security.png" alt="Security Icon" width="50">}} | To use key-based security mode, you must copy the private keys you generated on the Build system to each of your Server systems. In future versions of OrangeFS, security will be simplified. |


*Note*     Neither the default nor certificate-based modes of security
require any additional setup on Server systems. For these modes, see the
next topic, [Server Startup]({{<relref "Server-Startup.md">}})

Procedure
---------

### Copying Keys Manually

To add a private key to an individual Server, copy the private key file
from /opt/ofs\_keys on the Build system to the Server system:

scp –r /opt/ofs\_keys/orangefs-serverkey-hostname.pem
hostname:/opt/orangefs/etc/orangefs-serverkey.pem

where...

*hostname* = host name of the Server system

**Note     **The above command line format assumes you generated your
keys according to instructions in [Set Up
Security](Setup_Security_(Build).htm) under the [Build and
Configure](Build_and_Configure.htm) step.

### Copying Keys to Many Systems

You can use a script file (provided with OrangeFS) to copy all of your
private keys with one command **if both the following statements are
true**:

1.  You have already copied the OrangeFS installation directory to all
    of your designated servers and any additional Linux systems on which
    you plan to use an OrangeFS client interface.

2.  You generated your security keys during the [Build and
    Configure](Build_and_Configure.htm) step, using the script provided
    with OrangeFS (pvfs2-gen-keys.sh).

**Note     **For more information about client interfaces, see [Add
Clients](Installing_Other_Clients.htm).

 

If both the above statements are true, you can add private keys to all
your Linux-based OrangeFS systems as follows:

1.  Change Directory (cd) to the /opt/ofs\_keys directory on the Build
    system:

cd /opt/ofs\_keys

 

2.  If you followed the security setup instructions under the earlier
    Build and Configure step, the script file should be in the current
    directory and you can skip this step. Otherwise, copy the script
    from the OrangeFS source directory as follows:

cp /tmp/src/orangefs-version/examples/keys/pvfs2-dist-keys.sh

 

3.  With the script named pvfs2-dist-keys.sh, use the following command
    format to copy private keys to all OrangeFS Linux systems:

./pvfs2-dist-keys.sh *orangefs\_install*

where...

*orangefs\_install* = the location of the OrangeFS installation
directory

*Example:*  /opt/orangefs

 

Example of full command:

./pvfs2-dist-keys.sh /opt/orangefs

If the above example is used, the script examines the key filenames to
determine the hostname for each target server or client, then
secure-copies (scp) each key accordingly to the /opt/orangefs/etc
directory of the relevant system.  

**Note     **The script assumes that the specified OrangeFS installation
directory already exists on all of the targeted systems. The above
example uses the default location for the instructions in this
*Installation Guide*.

 

 

 

 

 
