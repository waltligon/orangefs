+++
title= "Server Startup"
weight=230
+++

|  |  |
|---|---|
| {{<figure src="../images/icon_run.png" alt="Security Icon" width="120">}} | You will use the OrangeFS server daemon (pvfs2-server) on each server to initialize the storage directories and start the OrangeFS server process. |


Procedure
---------

Running the server involves two tasks:

-   Initializing the working directories on each server for storage
    space
-   Starting the server process

The first task must be performed once on each server. Thereafter, you
can start and stop the server process with a single command. Both tasks
are accomplished with a command line statement that includes the
OrangeFS server daemon (pvfs2-server), located in the OrangeFS
installation directory under sbin.

### Initialize the Storage Directories

To initialize the storage directories, run the following command on each
server:

/opt/orangefs/sbin/pvfs2-server -f -a [hostname]
/opt/orangefs/etc/orangefs-server.conf

The -f option indicates that the file system storage directories should
be initialized. This command creates the storage directories using the
locations provided in the orangefs-server.conf file.  The storage
directories created will be /opt/orangefs/storage/{data,meta} with
additional subdirectories under both storage locations.

**Notes   **Because each server has different data based on the handle
ranges provided with the orangefs-server.conf file, do not copy one set
of databases to each server. You must create them separately on each
server.\
 \
 The storage space on each OrangeFS server must be initialized only
once. \
 \
 You can change the locations for storage directories by manually adding
the \<ServerOptions\> section in OrangeFS configuration file. With this
method, you can specify unique directory locations for each server. For
detailed information on all options in the OrangeFS configuration file,
see [Advanced Configuration > OrangeFS Configuration File]({{<relref "configuration/admin_ofs_configuration_file">}}).

### Start the Server Process

To start the server process, enter the following command:

/opt/orangefs/sbin/pvfs2-server -a **hostname**
/opt/orangefs/etc/orangefs-server.conf

 

###### Starting the Server Process Automatically

To avoid repeating this command each time you reboot an OrangeFS server,
you can place the statement in the appropriate system file(s) for
automatic execution. <!-- TODO: add this page? -> For more information, see [Automating System Startup](Automating_System_Startup_.htm). -->

 

###### Stopping the Server Process

To stop the server process, enter the following command:

killall pvfs2-server

 

 

 

 

 

 
