+++
title= "pvfs2tab"
weight=310
+++

  {{<figure src="../images/icon_mount.png" width="50" alt="mount">}}

Each client must know where to access OrangeFS resources. The pvfs2tab file, similar to the /etc/fstab file in Linux, provides clients with this access information. It involves creating a file at a designated path, which will function as the gateway to your OrangeFS installation.


1.  Determine the URL of the OrangeFS server you will access.

You can retrieve this information from the orangefs-server.conf file.
For example, the first URL listed in that file can be extracted with the
following command:

grep "Alias " /opt/orangefs/etc/orangefs-server.conf | awk '{ print \$3
}' | head -n 1

The format to use for server URL is *protocol*://*hostname*:*port*.

Example:  tcp://server1:3334

2.  Create a file named pvfs2tab in the system's /etc directory that
    tells the system how to access OrangeFS:

echo "tcp://server1:3334/orangefs /mnt/orangefs pvfs2 defaults,noauto 0
0" \>\>\
 /etc/pvfs2tab

**Note     **In the above example, tcp: is the network protocol,
//server1 is the server providing access to the configuration file,
and 3334 is the number of the TCP/IP port on which the OrangeFS servers
communicate, which was determined in step 1; /mnt/orangefs is the path
you use to access these files. You can think of /mnt/orangefs as the
root directory of the OrangeFS file system.

3.  You must also assign read-access to the new file:

chmod a+r /etc/pvfs2tab

4.  If you want to use an alternative file path instead of the standard
    location of /etc/pvfs2tab, you can set the PVFS2TAB\_FILE
    environment variable to the desired path.

 

 

 

 

 

 
