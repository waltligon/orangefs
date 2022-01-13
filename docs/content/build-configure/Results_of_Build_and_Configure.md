+++
title= "Results"
weight=150
+++

At the end of the Build and Configure step, the build system will
include:

-   An installation directory (/opt/orangefs)
-   A configuration file (/opt/orangefs/etc/orangefs-server.conf) to be
    used by all servers associated with this installation.

If you chose key-based security mode, the build system will also
include:

-   A temporary directory where all keys and the keystore file were
    generated.
-   A copy of the keystore file in the installation directory
    (opt/orangefs/etc/keystore).

If you chose certificate-based security mode, the build system will also
include a CA certificate and key (orangefs-ca-cert.pem and
orangefs-ca-cert-key.pem in /opt/orangefs/etc).

Installation Directory
----------------------

Following is a top-level list of the orangefs installation directory:

/opt/orangefs \$ ls\
 bin   include   lib   sbin   share   etc   log

<!-- TODO: add this page?
For file listings of all directories and subdirectories, see
[Directory/File Listing](Installed_File_Listing.htm).
-->

Configuration File
------------------

Following is a sample OrangeFS configuration file:

/opt/orangefs/etc \$ cat orangefs-server.conf\
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
                PrecreateBatchSize 0,32,512,32,32,32,0\
                PrecreateLowThreshold 0,16,256,16,16,16,0\
 \
                DataStorageSpace /opt/orangefs/storage/data\
                MetadataStorageSpace /opt/orangefs/storage/meta\
 \
                LogFile /var/log/orangefs-server.log\
 \</Defaults\>\
 \
 \<Aliases\>\
                Alias tweeks tcp://tweeks:3334\
 \</Aliases\>\
 \
 \<Filesystem\>\
                Name orangefs\
                ID 1600781381\
                RootHandle 1048576\
                FileStuffing yes\
                DistrDirServersInitial 1\
                DistrDirServersMax 1\
                DistrDirSplitSize 100\
                \<MetaHandleRanges\>\
                               Range tweeks 3-4611686018427387904\
                \</MetaHandleRanges\>\
                \<DataHandleRanges\>\
                               Range tweeks
4611686018427387905-9223372036854775806\
                \</DataHandleRanges\>\
                \<StorageHints\>\
                               TroveSyncMeta yes\
                               TroveSyncData no\
                               TroveMethod alt-aio\
                \</StorageHints\>\
 \</Filesystem\>

If you enabled key- or certificate-based security, a \<Security\>
context will also be in the configuration file.

Here is an example \<Security\> context for key-based security:

\<Defaults\>\
      . . .\
      \<Security\>\
            ServerKey /opt/orangefs/etc/orangefs-serverkey.pem\
            Keystore /opt/orangefs/etc/keystore\
      \</Security\>\
      . . .\
 \</Defaults\>\
 . . .

Here is an example \<Security\> context for certificate-based security:

\<Defaults\>\
     . . .\
     \<Security\>\
           CAFile /opt/orangefs/etc/orangefs-ca-cert.pem\
           ServerKey /opt/orangefs/etc/orangefs-ca-cert-key.pem\
           \<LDAP\>\
                Hosts ldap://ldap01.acme.com\
                BindDN cn=ofsadmin,dc=acme,dc=com\
                BindPassword file:/opt/orangefs/etc/ldappw.txt\
                SearchRoot ou=OrangeFS-Users,dc=acme,dc=com\
                SearchMode CN\
                SearchClass inetOrgPerson\
                SearchAttr CN\
                SearchScope subtree\
                UIDAttr uidNumber\
                GIDAttr gidNumber\
                SearchTimeout 10\
           \</LDAP\>\
     \</Security\>\
     . . .\
 \</Defaults\>\
 . . .

The \<Security\> context can also be specified in a \<ServerOptions\>
context for different settings on each server.

For more information, see [OrangeFS Configuration
File]({{<relref "configuration/admin_ofs_configuration_file">}}).

 

 

 

 

 
