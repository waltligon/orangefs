+++
title= "Multiple Server Processes"
weight=530
+++

To increase backend throughput when you have separate local volumes, you
may elect to run multiple server processes on each server.

The topic is organized into the following three sections:

-   [Multiple Server Processes, Single File
    System]({{<relref "#multiple-server-processes-single-file-system">}})
-   [Multiple Servers, Single File System, Some Data-only
    Servers]({{<relref "#multiple-servers-single-file-system-some-data-only-servers">}})
-   [Multiple Server Processes, Single File System, Multiple
    Machines]({{<relref "#multiple-server-processes-single-file-system-multiple-machines">}})

Multiple Server Processes, Single File System
---------------------------------------------

You can increase parallel throughput with multiple server processes.
This option is efficient only when you run one server process per disc,
because running multiple server processes per disc will cause access
delays which counteract any throughput advantage.

Below is a sample configuration for multiple server processes with a
single file system.

\<Defaults\>\
     UnexpectedRequests 128\
      EventLogging none\
      EnableTracing no\
      LogStamp datetime\
      BMIModules bmi\_tcp\
      FlowModules flowproto\_multiqueue\
      PerfUpdateInterval 10000\
      ServerJobBMITimeoutSecs 120\
      ServerJobFlowTimeoutSecs 120\
      ClientJobBMITimeoutSecs 120\
      ClientJobFlowTimeoutSecs 120\
      ClientRetryLimit 5\
      ClientRetryDelayMilliSecs 5000\
      PrecreateBatchSize 0,1024,1024,1024,32,1024,0\
      PrecreateLowThreshold 0,256,256,256,16,256,0\
    TroveMaxConcurrentIO 16\
 \</Defaults\>\
 \
 \<Aliases\>\
      Alias ofs001 tcp://ofs001:3334\
      Alias ofs002 tcp://ofs001:3335\
      Alias ofs003 tcp://ofs001:3336\
      Alias ofs004 tcp://ofs001:3337\
 \</Aliases\>\
 \
 \<ServerOptions\>\
      Server ofs001\
      DataStorageSpace /ofs001/3334/data\
      MetadataStorageSpace /ofs001/3334/meta\
      LogFile /var/log/orangefs-server-3334.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
      Server ofs002\
      DataStorageSpace /ofs001/3335/data\
      MetadataStorageSpace /ofs001/3335/meta\
      LogFile /var/log/orangefs-server-3335.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
      Server ofs003\
      DataStorageSpace /ofs001/3336/data\
      MetadataStorageSpace /ofs001/3336/meta\
      LogFile /var/log/orangefs-server-3336.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
      Server ofs004\
      DataStorageSpace /ofs001/3337/data\
      MetadataStorageSpace /ofs001/3337/meta\
      LogFile /var/log/orangefs-server-3337.log\
 \</ServerOptions\>\
 \
 \<Filesystem\>\
      Name orangefs\
      ID 696608094\
      RootHandle 1048576\
      FileStuffing yes\
      DistrDirServersInitial 1\
      DistrDirServersMax 1\
      DistrDirSplitSize 100\
      TreeThreshold 16\
      \<StorageHints\>\
           TroveSyncMeta yes\
           TroveSyncData no\
           TroveMethod alt-aio\
           DBCacheSizeBytes 2147483648\
      \</StorageHints\>\
    \<Distribution\>\
         Name simple\_stripe\
         Param strip\_size\
         Value 262144\
   \</Distribution\>\
     \<MetaHandleRanges\>\
         Range ofs001 3-144115188075855875\
         Range ofs002 144115188075855876-288230376151711748\
         Range ofs003 288230376151711749-432345564227567621\
         Range ofs004 432345564227567622-576460752303423494\
     \</MetaHandleRanges\>\
     \<DataHandleRanges\>\
         Range ofs001 2305843009213693971-2449958197289549843\
         Range ofs002 2449958197289549844-2594073385365405716\
         Range ofs003 2594073385365405717-2738188573441261589\
         Range ofs004 2738188573441261590-2882303761517117462\
     \</DataHandleRanges\>\
 \</Filesystem\>

Multiple Servers, Single File System, Some Data-only Servers
------------------------------------------------------------

Below is an example configuration file for a situation in which seven
servers are combined meta and data servers, and the remaining nine
servers are data servers only. The \<MetaHandleRanges\> and
\<DataHandleRanges\> sections in the code below determine which servers
serve in which capacity.

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
       DataStorageSpace     /opt/orangefs/storage/data\
       MetadataStorageSpace /opt/orangefs/storage/meta\
       LogFile              /opt/orangefs/orangefs-server.log\
 \
       \<Security\>\
             TurnOffTimeouts yes\
       \</Security\>\
 \</Defaults\>\
 \
 \<Aliases\>\
       Alias server01 tcp://ofs001:3334\
       Alias server02 tcp://ofs002:3334\
       Alias server03 tcp://ofs003:3334\
       Alias server04 tcp://ofs004:3334\
 \
       Alias server05 tcp://ofs005:3334\
       Alias server06 tcp://ofs006:3334\
       Alias server07 tcp://ofs007:3334\
       Alias server08 tcp://ofs008:3334\
 \
       Alias server09 tcp://ofs009:3334\
       Alias server10 tcp://ofs010:3334\
       Alias server11 tcp://ofs011:3334\
       Alias server12 tcp://ofs012:3334\
 \
       Alias server13 tcp://ofs013:3334\
       Alias server14 tcp://ofs014:3334\
       Alias server15 tcp://ofs015:3334\
       Alias server16 tcp://ofs016:3334\
 \</Aliases\>\
 \
 \<Filesystem\>\
       Name orangefs\
       ID 991638827\
       RootHandle 1048576\
       FileStuffing yes\
       DistrDirServersInitial 1\
       DistrDirServersMax 1\
       DistrDirSplitSize 100\
       \<MetaHandleRanges\>\
             Range server01 3-288230376151711745\
             Range server02 288230376151711746-576460752303423488\
             Range server03 576460752303423489-864691128455135231\
             Range server04 864691128455135232-1152921504606846974\
             Range server05 1152921504606846975-1441151880758558717\
             Range server06 1441151880758558718-1729382256910270460\
             Range server07 1729382256910270461-2017612633061982203\
       \</MetaHandleRanges\>\
       \<DataHandleRanges\>\
             Range server01 4611686018427387891-4899916394579099633\
             Range server02 4899916394579099634-5188146770730811376\
             Range server03 5188146770730811377-5476377146882523119\
             Range server04 5476377146882523120-5764607523034234862\
             Range server05 5764607523034234863-6052837899185946605\
             Range server06 6052837899185946606-6341068275337658348\
             Range server07 6341068275337658349-6629298651489370091\
             Range server08 6629298651489370092-6917529027641081834\
             Range server09 6917529027641081835-7205759403792793577\
             Range server10 7205759403792793578-7493989779944505320\
             Range server11 7493989779944505321-7782220156096217063\
             Range server12 7782220156096217064-8070450532247928806\
             Range server13 8070450532247928807-8358680908399640549\
             Range server14 8358680908399640550-8646911284551352292\
             Range server15 8646911284551352293-8935141660703064035\
             Range server16 8935141660703064036-9223372036854775778\
       \</DataHandleRanges\>\
       \<StorageHints\>\
             TroveSyncMeta yes\
             TroveSyncData no\
             TroveMethod alt-aio\
       \</StorageHints\>\
 \</Filesystem\>

Multiple Server Processes, Single File System, Multiple Machines 
----------------------------------------------------------------

Below is an example configuration file for a situation with four server
processes per machine with four machines serving a single file system.

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
       \<Security\>\
             TurnOffTimeouts yes\
       \</Security\>\
 \</Defaults\>\
 \
 \<Aliases\>\
       Alias server01 tcp://ofs001:3334\
       Alias server02 tcp://ofs001:3335\
       Alias server03 tcp://ofs001:3336\
       Alias server04 tcp://ofs001:3337\
 \
       Alias server05 tcp://ofs002:3334\
       Alias server06 tcp://ofs002:3335\
       Alias server07 tcp://ofs002:3336\
       Alias server08 tcp://ofs002:3337\
 \
       Alias server09 tcp://ofs003:3334\
       Alias server10 tcp://ofs003:3335\
       Alias server11 tcp://ofs003:3336\
       Alias server12 tcp://ofs003:3337\
 \
       Alias server13 tcp://ofs004:3334\
       Alias server14 tcp://ofs004:3335\
       Alias server15 tcp://ofs004:3336\
       Alias server16 tcp://ofs004:3337\
 \</Aliases\>\
 \
 \<ServerOptions\>\
       Server server01 \
       DataStorageSpace     /opt/orangefs/storage/server01/data\
       MetadataStorageSpace /opt/orangefs/storage/server01/meta\
       LogFile              /opt/orangefs/orangefs-server01.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server02 \
       DataStorageSpace     /opt/orangefs/storage/server02/data\
       MetadataStorageSpace /opt/orangefs/storage/server02/meta\
       LogFile              /opt/orangefs/orangefs-server02.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server03 \
       DataStorageSpace     /opt/orangefs/storage/server03/data\
       MetadataStorageSpace /opt/orangefs/storage/server03/meta\
       LogFile              /opt/orangefs/orangefs-server03.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server04 \
       DataStorageSpace     /opt/orangefs/storage/server04/data\
       MetadataStorageSpace /opt/orangefs/storage/server04/meta\
       LogFile              /opt/orangefs/orangefs-server04.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server05 \
       DataStorageSpace     /opt/orangefs/storage/server05/data\
       MetadataStorageSpace /opt/orangefs/storage/server05/meta\
       LogFile              /opt/orangefs/orangefs-server05.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server06 \
       DataStorageSpace     /opt/orangefs/storage/server06/data\
       MetadataStorageSpace /opt/orangefs/storage/server06/meta\
       LogFile              /opt/orangefs/orangefs-server06.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server07 \
       DataStorageSpace     /opt/orangefs/storage/server07/data\
       MetadataStorageSpace /opt/orangefs/storage/server07/meta\
       LogFile              /opt/orangefs/orangefs-server07.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server08 \
       DataStorageSpace     /opt/orangefs/storage/server08/data\
       MetadataStorageSpace /opt/orangefs/storage/server08/meta\
       LogFile              /opt/orangefs/orangefs-server08.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server09 \
       DataStorageSpace     /opt/orangefs/storage/server09/data\
       MetadataStorageSpace /opt/orangefs/storage/server09/meta\
       LogFile              /opt/orangefs/orangefs-server09.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server10 \
       DataStorageSpace     /opt/orangefs/storage/server10/data\
       MetadataStorageSpace /opt/orangefs/storage/server10/meta\
       LogFile              /opt/orangefs/orangefs-server10.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server11 \
       DataStorageSpace     /opt/orangefs/storage/server11/data\
       MetadataStorageSpace /opt/orangefs/storage/server11/meta\
       LogFile              /opt/orangefs/orangefs-server11.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server12 \
       DataStorageSpace     /opt/orangefs/storage/server12/data\
       MetadataStorageSpace /opt/orangefs/storage/server12/meta\
       LogFile              /opt/orangefs/orangefs-server12.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server13 \
       DataStorageSpace     /opt/orangefs/storage/server13/data\
       MetadataStorageSpace /opt/orangefs/storage/server13/meta\
       LogFile              /opt/orangefs/orangefs-server13.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server14 \
       DataStorageSpace     /opt/orangefs/storage/server14/data\
       MetadataStorageSpace /opt/orangefs/storage/server14/meta\
       LogFile              /opt/orangefs/orangefs-server14.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server15 \
       DataStorageSpace     /opt/orangefs/storage/server15/data\
       MetadataStorageSpace /opt/orangefs/storage/server15/meta\
       LogFile              /opt/orangefs/orangefs-server15.log\
 \</ServerOptions\>\
 \<ServerOptions\>\
       Server server16 \
       DataStorageSpace     /opt/orangefs/storage/server16/data\
       MetadataStorageSpace /opt/orangefs/storage/server16/meta\
       LogFile              /opt/orangefs/orangefs-server16.log\
 \</ServerOptions\>\
 \
 \<Filesystem\>\
       Name orangefs\
       ID 991638827\
       RootHandle 1048576\
       FileStuffing yes\
       DistrDirServersInitial 1\
       DistrDirServersMax 1\
       DistrDirSplitSize 100\
       \<MetaHandleRanges\>\
             Range server01 3-288230376151711745\
             Range server02 288230376151711746-576460752303423488\
             Range server03 576460752303423489-864691128455135231\
             Range server04 864691128455135232-1152921504606846974\
             Range server05 1152921504606846975-1441151880758558717\
             Range server06 1441151880758558718-1729382256910270460\
             Range server07 1729382256910270461-2017612633061982203\
             Range server08 2017612633061982204-2305843009213693946\
             Range server09 2305843009213693947-2594073385365405689\
             Range server10 2594073385365405690-2882303761517117432\
             Range server11 2882303761517117433-3170534137668829175\
             Range server12 3170534137668829176-3458764513820540918\
             Range server13 3458764513820540919-3746994889972252661\
             Range server14 3746994889972252662-4035225266123964404\
             Range server15 4035225266123964405-4323455642275676147\
             Range server16 4323455642275676148-4611686018427387890\
       \</MetaHandleRanges\>\
       \<DataHandleRanges\>\
             Range server01 4611686018427387891-4899916394579099633\
             Range server02 4899916394579099634-5188146770730811376\
             Range server03 5188146770730811377-5476377146882523119\
             Range server04 5476377146882523120-5764607523034234862\
             Range server05 5764607523034234863-6052837899185946605\
             Range server06 6052837899185946606-6341068275337658348\
             Range server07 6341068275337658349-6629298651489370091\
             Range server08 6629298651489370092-6917529027641081834\
             Range server09 6917529027641081835-7205759403792793577\
             Range server10 7205759403792793578-7493989779944505320\
             Range server11 7493989779944505321-7782220156096217063\
             Range server12 7782220156096217064-8070450532247928806\
             Range server13 8070450532247928807-8358680908399640549\
             Range server14 8358680908399640550-8646911284551352292\
             Range server15 8646911284551352293-8935141660703064035\
             Range server16 8935141660703064036-9223372036854775778\
       \</DataHandleRanges\>\
       \<StorageHints\>\
             TroveSyncMeta yes\
             TroveSyncData no\
             TroveMethod alt-aio\
       \</StorageHints\>\
 \</Filesystem\>  

 

 

 

 

 

 

 

 

 

 
