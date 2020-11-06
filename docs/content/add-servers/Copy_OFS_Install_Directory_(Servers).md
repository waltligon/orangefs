+++
title= "Copy OrangeFS Server Installation Directory"
weight=210
+++

|  |  |
|---|---|
| {{<figure src="../images/icon_addserver.png" alt="Security Icon" width="120">}} | Begin your deployment by copying the OrangeFS installation directory from the Build system to each Server system designated in your OrangeFS solution. These are the servers already identified in the OrangeFS configuration file. |

System Requirements
-------------------

Any system that functions as an OrangeFS server requires a supported
distribution of Linux.

**Note     **For more information on supported distributions, see
[Preview System Requirements](Preview_System_Requirements.htm).

Procedure
---------

To add the required software to an OrangeFS server, copy the
/opt/orangefs directory from the Build system:

scp –r /opt/orangefs hostname:/opt

where...

*hostname* = host name of the Server system

Important  the default OrangeFS configuration is not compatible with the default SELinux configuration, for streamlined installation you can disable SELinux.  This SELinux configuration will cause a "permission denied" error when you try to run OrangeFS. 

To disable SELinux, use the following command:\
                 echo 0 \> /seLinux/enforce\
 To prevent SELinux from loading at boot time, edit /etc/seLinux/config
and set the SELINUX value to “disabled”, for example,\
                 SELINUX=disabled\
 The command for disabling SELinux can vary, depending on your Linux
version.

 

 

 

 

 

 
