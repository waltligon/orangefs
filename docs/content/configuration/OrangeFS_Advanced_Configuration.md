+++
title= "Multiple File Systems"
weight=520
+++

In some circumstances, you may need to configure OrangeFS to support
multiple concurrent file systems on the same set of storage servers.
This topic provides examples of two different configuration options.

To configure OrangeFS to support multiple concurrent file systems on the
same set of storage servers, you have two options. One is to run
multiple server processes, each with its own config file. The second
option is to have a single server process surface multiple file systems.
 There are no hard coded limits on the number of file systems via either
method, so you are limited only by system resources.  

This topic is organized into the following two sections:

-   [Multiple Server Processes, Multiple File
    Systems](OrangeFS_Advanced_Configuration.htm#Multiple_Server_Processes__Multiple_File_Systems)

-   [Single Server Process, Multiple File
    Systems](OrangeFS_Advanced_Configuration.htm#Single_Server_Process__Multiple_File_Systems)

Multiple Server Processes, Multiple File Systems
------------------------------------------------

Below are two sample configuration files that will run on the same
systems and provide two different file systems.  Each file system will
have its own storage area.

### Multiple Server Processes Config File 1

Below is the first sample configuration file.

\<Defaults\>\
       UnexpectedRequests 50\
       EventLogging none\
       EnableTracing no\
       LogStamp datetime\
       BMIModules bmi\_tcp\
       FlowModules flowproto\_multiqueue\
       PerfUpdateInterval 1000\
       ServerJobBMITimeoutSecs 30\
       ServerJobFlowTimeoutSecs 30\
       ClientJobBMITimeoutSecs 300\
       ClientJobFlowTimeoutSecs 300\
       ClientRetryLimit 5\
       ClientRetryDelayMilliSecs 2000\
       PrecreateBatchSize 0,1024,1024,1024,32,1024,0\
       PrecreateLowThreshold 0,256,256,256,16,256,0\
 \
       DataStorageSpace     /ofs001/storage/3334/data\
       MetadataStorageSpace /ofs001/storage/3334/meta\
 \
       LogFile /var/log/orangefs-server-3334.log\
 \
       \<Security\>\
             TurnOffTimeouts yes\
       \</Security\>\
 \</Defaults\>\
 \
 \<Aliases\>\
       Alias ofs001 tcp://ofs001:3334\
 \</Aliases\>\
 \
 \<Filesystem\>\
       Name orangefs\
       ID 466735872\
       RootHandle 1048576\
       FileStuffing yes\
       DistrDirServersInitial 1\
       DistrDirServersMax 1\
       DistrDirSplitSize 100\
       \<MetaHandleRanges\>\
             Range ol7dot3 3-4611686018427387904\
       \</MetaHandleRanges\>\
       \<DataHandleRanges\>\
             Range ol7dot3 4611686018427387905-9223372036854775806\
       \</DataHandleRanges\>\
       \<StorageHints\>\
             TroveSyncMeta yes\
             TroveSyncData no\
             TroveMethod alt-aio\
       \</StorageHints\>\
 \</Filesystem\>

### **\
**Multiple Server Processes Config File 2**

Below is the second sample configuration file.

\<Defaults\>\
       UnexpectedRequests 50\
       EventLogging none\
       EnableTracing no\
       LogStamp datetime\
       BMIModules bmi\_tcp\
       FlowModules flowproto\_multiqueue\
       PerfUpdateInterval 1000\
       ServerJobBMITimeoutSecs 30\
       ServerJobFlowTimeoutSecs 30\
       ClientJobBMITimeoutSecs 300\
       ClientJobFlowTimeoutSecs 300\
       ClientRetryLimit 5\
       ClientRetryDelayMilliSecs 2000\
       PrecreateBatchSize 0,1024,1024,1024,32,1024,0\
       PrecreateLowThreshold 0,256,256,256,16,256,0\
 \
       DataStorageSpace     /ofs001/storage/3335/data\
       MetadataStorageSpace /ofs001/storage/3335/meta\
 \
       LogFile /var/log/orangefs-server-3335.log\
 \
       \<Security\>\
             TurnOffTimeouts yes\
       \</Security\>\
 \</Defaults\>\
 \
 \<Aliases\>\
       Alias ofs001 tcp://ofs001:3335\
 \</Aliases\>\
 \
 \<Filesystem\>\
       Name orangefs\
       ID 466735872\
       RootHandle 1048576\
       FileStuffing yes\
       DistrDirServersInitial 1\
       DistrDirServersMax 1\
       DistrDirSplitSize 100\
       \<MetaHandleRanges\>\
             Range ol7dot3 3-4611686018427387904\
       \</MetaHandleRanges\>\
       \<DataHandleRanges\>\
             Range ol7dot3 4611686018427387905-9223372036854775806\
       \</DataHandleRanges\>\
       \<StorageHints\>\
             TroveSyncMeta yes\
             TroveSyncData no\
             TroveMethod alt-aio\
       \</StorageHints\>\
 \</Filesystem\>

Single Server Process, Multiple File Systems {dir="ltr"}
--------------------------------------------

Below is an example configuration file for a situation in which one
server manages multiple file systems.  The storage area for these two
file systems is shared.

\<Defaults\>\
       UnexpectedRequests 50\
       EventLogging none\
       EnableTracing no\
       LogStamp datetime\
       BMIModules bmi\_tcp\
       FlowModules flowproto\_multiqueue\
       PerfUpdateInterval 1000\
       ServerJobBMITimeoutSecs 30\
       ServerJobFlowTimeoutSecs 30\
       ClientJobBMITimeoutSecs 300\
       ClientJobFlowTimeoutSecs 300\
       ClientRetryLimit 5\
       ClientRetryDelayMilliSecs 2000\
       PrecreateBatchSize 0,1024,1024,1024,32,1024,0\
       PrecreateLowThreshold 0,256,256,256,16,256,0\
 \
       DataStorageSpace     /ofs001/storage/data\
       MetadataStorageSpace /ofs001/storage/meta\
 \
       LogFile /var/log/orangefs-server.log\
 \
       \<Security\>\
             TurnOffTimeouts yes\
       \</Security\>\
 \</Defaults\>\
 \
 \<Aliases\>\
       Alias ofs001 tcp://ofs001:3334\
 \</Aliases\>\
 \
 \<Filesystem\>\
       Name orangefs-1\
       ID 466735872\
       RootHandle 1048576\
       FileStuffing yes\
       DistrDirServersInitial 1\
       DistrDirServersMax 1\
       DistrDirSplitSize 100\
       \<MetaHandleRanges\>\
             Range ol7dot3 3-4611686018427387904\
       \</MetaHandleRanges\>\
       \<DataHandleRanges\>\
             Range ol7dot3 4611686018427387905-9223372036854775806\
       \</DataHandleRanges\>\
       \<StorageHints\>\
             TroveSyncMeta yes\
             TroveSyncData no\
             TroveMethod alt-aio\
       \</StorageHints\>\
 \</Filesystem\>\
 \<Filesystem\>\
       Name orangefs-2\
       ID 1234567\
       RootHandle 1048576\
       FileStuffing yes\
       DistrDirServersInitial 1\
       DistrDirServersMax 1\
       DistrDirSplitSize 100\
       \<MetaHandleRanges\>\
             Range ol7dot3 3-4611686018427387904\
       \</MetaHandleRanges\>\
       \<DataHandleRanges\>\
             Range ol7dot3 4611686018427387905-9223372036854775806\
       \</DataHandleRanges\>\
       \<StorageHints\>\
             TroveSyncMeta yes\
             TroveSyncData no\
             TroveMethod alt-aio\
       \</StorageHints\>\
 \</Filesystem\>

 

 

 

 

 

 

 

 

 
