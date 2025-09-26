+++
title= "OrangeFS Configuration File"
weight=510
+++

The OrangeFS configuration file is copied to all servers as a single
reference point for operation and performance. This is the file in which
you specify settings and preferences for the file system. During
installation, you use a program called pvfs2-genconfig to automatically
generate the OrangeFS configuration file. The program presents a series
of prompts, enabling you to enter basic required settings.

While pvfs2-genconfig is designed to query you about the most important
options, there are many additional options with default values that are
bypassed during installation. After installation, you can revisit the
configuration file to make changes and additions from a broad selection
of options for:

-   Server reconfiguration

-   Performance-tuning

-   Storage optimization

-   Troubleshooting

 

**Note     **After installation, any time you change the configuration
file, you will need to recopy it to all servers in your OrangeFS
installation and also restart each server.

 

**Note     **This list of options is generated automatically from
program comments that have not been edited for spelling and grammar.

What's Inside
-------------

The configuration file is a simple text file that can be opened and
manually modified.

It is organized into a number of option categories called contexts. Each
context is bracketed by tags and includes a list of one or more
option-value pairs, as shown in this example:

\<*ContextName*\>      \
      *Option1Name Option1Value*\
      *Option2Name Option2Value* \
 \</*ContextName*\>

 

When a server is started, the options associated with its server-alias
in the configuration file are executed.

An option cannot span more than one line, and only one option can be
specified on each line. The *OptionValue* should be formatted based on
the option's type:

-   Integer - must be an integer value

-   String - must be a string without breaks (newlines)

-   List - a set of strings separated by commas

 

Options must be defined within a specified context or set of contexts.
Sub-contexts must be defined within their specified parent contexts.

For example, the *Range* option is specified in either the
*DataHandleRanges* or *MetaHandleRanges* contexts. Both of those
contexts are specified to be defined in the *FileSystem* context.

Options and contexts that appear in the top-level (not defined within
another context) are considered to be defined in a special *Global*
context. Many options are only specified to appear within the Default
context, which is a context that allows a default value to be specified
for certain options. The options detailed below specify their type, the
context where they appear, a default value, and description. The default
value is used if the option is not specified. Options without default
values must be defined.

Option and Context Descriptions
-------------------------------

The remainder of this topic is a reference for all options in the
configuration file, grouped by the contexts in which they are allowed to
be used.

OrangeFS Configuration File
===========================

The OrangeFS configuration file is copied to all servers as a single
reference point for operation and performance. This is the file in which
you specify settings and preferences for the file system. During
installation, you use a program called pvfs2-genconfig to automatically
generate the OrangeFS configuration file. The program presents a series
of prompts, enabling you to enter basic required settings.

While pvfs2-genconfig is designed to query you about the most important
options, there are many additional options with default values that are
bypassed during installation. After installation, you can revisit the
configuration file to make changes and additions from a broad selection
of options for:

-   Server reconfiguration

-   Performance-tuning

-   Storage optimization

-   Troubleshooting

 

**Note     **After installation, any time you change the configuration
file, you will need to recopy it to all servers in your OrangeFS
installation and also restart each server.

 

**Note     **This list of options is generated automatically from
program comments that have not been edited for spelling and grammar.

What's Inside
-------------

The configuration file is a simple text file that can be opened and
manually modified.

It is organized into a number of option categories called contexts. Each
context is bracketed by tags and includes a list of one or more
option-value pairs, as shown in this example:

\<*ContextName*\>      \
      *Option1Name Option1Value*\
      *Option2Name Option2Value* \
 \</*ContextName*\>

 

When a server is started, the options associated with its server-alias
in the configuration file are executed.

An option cannot span more than one line, and only one option can be
specified on each line. The *OptionValue* should be formatted based on
the option's type:

-   Integer - must be an integer value

-   String - must be a string without breaks (newlines)

-   List - a set of strings separated by commas

 

Options must be defined within a specified context or set of contexts.
Sub-contexts must be defined within their specified parent contexts.

For example, the *Range* option is specified in either the
*DataHandleRanges* or *MetaHandleRanges* contexts. Both of those
contexts are specified to be defined in the *FileSystem* context.

Options and contexts that appear in the top-level (not defined within
another context) are considered to be defined in a special *Global*
context. Many options are only specified to appear within the Default
context, which is a context that allows a default value to be specified
for certain options. The options detailed below specify their type, the
context where they appear, a default value, and description. The default
value is used if the option is not specified. Options without default
values must be defined.

Option and Context Descriptions
-------------------------------

The remainder of this topic is a reference for all options in the
configuration file, grouped by the contexts in which they are allowed to
be used.

### Option Descriptions

This is the list of possible Options that can be used in the config
files in this version of OrangeFS.

<!-- TODO: add back links to the contexts (and other options) within
the tables? (use "anchor" shortcode to create anchor links) -->

| Option: | **TrustedPorts** |
|---|---|
| Type: | String |
| Contexts: | Security |
| Default Value: | None |
| Description: | Specifies the range of ports in the form of a range of 2 integers from which the connections are going to be accepted and serviced. The format of the TrustedPorts option is: <br><br> TrustedPorts{*StartPort*}-{*EndPort*} <br><br>  As an example: <br><br>  TrustedPorts 0-65535 |

| Option: | **TrustedNetwork** |
|---|---|
| Type: | List |
| Contexts: | Security |
| Default Value: | None |
| Description: | Specifies the IP network and netmask in the form of 2 BMI addresses from which the connections are going to be accepted and serviced. The format of the TrustedNetwork option is: <br> <br> TrustedNetwork <br> <br> {*bmi-network-address@bmi-network-mask*}-{*EndPort*} <br> <br>As an example: <br> <br> TrustedNetwork tcp://192.168.4.0@24 |

| Option: | **KeyStore** |
|---|---|
| Type: | String |
| Contexts: | Defaults, ServerOptions, Security |
| Default Value: | None |
| Description: | A path to a keystore file, which stores server and client public keys for key-based security. Note: May be in the Defaults section for compatibility. For newly-generated configuration files it should appear in the Security section. |

| Option: | **ServerKey** |
|---|---|
| Type: | String |
| Contexts: | Defaults, ServerOptions, Security |
| Default Value: | None |
| Description: | Path to the server private key file, in PEM format. Must correspond to CA certificate in certificate mode. <br><br> Note: May be in the Defaults section for backwards-compatibility. For newly-generated configuration files it should appear in the Security section. |

| Option: | **CredentialTimeoutSecs** |
|---|---|
| Type: | Integer |
| Contexts: | Security |
| Default Value: | 3600 |
| Description: | Credential timeout in seconds |

| Option: | **CapabilityTimeoutSecs** |
|---|---|
| Type: | Integer |
| Contexts: | Security |
| Default Value: | 600 |
| Description: | Capability timeout in seconds |


| Option: | **TurnOffTimeouts** |
|---|---|
| Type: | String |
| Contexts: | Security |
| Default Value: | yes |
| Description: | Prevent the server from issuing an error whenever a capability or credential expires. In this case, the client provides the only mechanism determining when a capability or credential needs to be egenerated. This option is only valid within the Defaults context; either the entire system is using timeouts or it is not. |

| Option: | **CredentialCacheTimeoutSecs** |
|---|---|
| Type: | Integer |
| Contexts: | Security |
| Default Value: | 3600 |
| Description: | Server-side Credential cache timeout in seconds |


| Option: | **CapabilityCacheTimeoutSecs** |
|---|---|
| Type: | Integer |
| Contexts: | Security |
| Default Value: | 600 |
| Description: | Server-side Capability cache timeout in seconds |

| Option: | **CertificateCacheTimeoutSecs** |
|---|---|
| Type: | Integer |
| Contexts: | Security |
| Default Value: | 3600 |
| Description: | Server-side Certificate cache timeout in seconds |

| Option: | **CAFile** |
|---|---|
| Type: | String |
| Contexts: | Defaults, ServerOptions, Security |
| Default Value: | None |
| Description: | Path to CA certificate file in PEM format. Note: May be in the Defaults section for backwards-compatibility. For newly-generated configuration files it should appear in the Security section. |

| Option: | **UserCertDN** |
|---|---|
| Type: | String |
| Contexts: | Defaults <br> ServerOptions <br> Security |
| Default Value: | \\C=US, O=OrangeFS\\ |
| Description: | DN used for root of generated user certificate subject DN Note: May be in the Defaults section for backwards-compatibility. For newly-generated configuration files it should appear in the Security section. |
 
| Option: | **UserCertExp** |
|---|---|
| Type: | Integer |
| Contexts: | Defaults <br> ServerOptions <br> Security |
| Default Value: | 365 |
| Description: | Expiration of generated user certificate in days Note: May be in the Defaults section for backwards-compatibility. For newly-generated configuration files it should appear in the Security section. |
 
| Option: | **Hosts** |
|---|---|
| Type: | String |
| Contexts: | LDAP |
| Default Value: | ldaps://localhost |
| Description: | List of LDAP hosts in URI format, <br> e.g. ldaps://ldap.acme.com:999 |
 
| Option: | **BindDN** |
|---|---|
| Type: | String |
| Contexts: | LDAP |
| Default Value: | None |
| Description: | DN of LDAP user to use when binding to LDAP directory. |
 
| Option: | **BindPassword** |
|---|---|
| Type: | String |
| Contexts: | LDAP |
| Default Value: | None |
| Description: | Password of LDAP user to use when binding to LDAP directory. May also be in form file:{*path*} which will load password from restricted file. |
 
| Option: | **SearchMode** |
|---|---|
| Type: | String |
| Contexts: | LDAP |
| Default Value: | CN |
| Description: | May be CN or DN. Controls how the certificate subject DN is used to search LDAP for a user. |
 
| Option: | **SearchRoot** |
|---|---|
| Type: | String |
| Contexts: | LDAP |
| Default Value: | None |
| Description: | DN of top-level LDAP search container. Only used in CN mode. |
 
| Option: | **SearchClass** |
|---|---|
| Type: | String |
| Contexts: | LDAP |
| Default Value: | inetOrgPerson |
| Description: | Object class of user objects to search in LDAP. |
 
| Option: | **SearchAttr** |
|---|---|
| Type: | String |
| Contexts: | LDAP |
| Default Value: | CN |
| Description: | Attribute name to match certificate CN. Only used in CN mode. |
 
| Option: | **SearchScope** |
|---|---|
| Type: | String |
| Contexts: | LDAP |
| Default Value: | subtree |
| Description: | May be onelevel to search only SearchRoot container, or subtree to search SearchRoot container and all child containers. |
 
| Option: | **UIDAttr** |
|---|---|
| Type: | String |
| Contexts: | LDAP |
| Default Value: | uidNumber |
| Description: | Attribute name in which UID value is stored. |

| Option: | **GIDAttr** |
|---|---|
| Type: | String |
| Contexts: | LDAP |
| Default Value: | gidNumber |
| Description: | Attribute name in which GID value is stored. |

| Option: | **SearchTimeoutSecs** |
|---|---|
| Type: | Integer |
| Contexts: | LDAP |
| Default Value: | 15 |
| Description: | LDAP server timeout for searches (in seconds) |
 
| Option: | **Alias** |
|---|---|
| Type: | List |
| Contexts: | Aliases |
| Default Value: | None |
| Description: | Specifies an alias in the form of a non-whitespace string that can be used to reference a BMI server address (a HostID). This allows us to reference individual servers by an alias instead of their full HostID. The format of the Alias <br><br>option is:<br> Alias {*alias string*} {*bmi address*} <br><br> As an example: <br><br> Alias mynode1 <br> tcp://hostname1.clustername1.domainname:12345 |

| Option: | **Server** |
|---|---|
| Type: | String |
| Contexts: | ServerOptions |
| Default Value: | None |
| Description: | Defines the server alias for the server specific options that are to be set within the ServerOptions context. |
 
| Option: | **Range** |
|---|---|
| Type: | List |
| Contexts: | MetaHandleRanges <br> DataHandleRanges |
| Default Value: | None |
| Description: | As logical files are created in OrangeFS, the data files and meta files that represent them are given file system unique handle values. The user can specify a range of values (or set of ranges) to be allocated to data files and meta files for a particular server, using the Range option in the DataHandleRanges and MetaHandleRanges contexts. Note that in most cases, its easier to let the pvfs2-genconfig script determine the best ranges to specify. <br><br> This option specifies a range of handle values that can be used for a particular OrangeFS server in a particular context (meta handles or data handles). The DataHandleRanges and MetaHandleRanges contexts should contain one or more Range options.  The format is: <br><br> [Range {*alias*} {*min value1*}-{*max value1*}[, {*min value2*}-{*max value2*},...] <br><br> Where {*alias*} is one of the alias strings already specified in the Aliases context. <br><br> {*min value*} and {*max value*} are positive integer values that specify the range of possible handles that can be given out for that particular host. {*max value*} must be less than 18446744073709551615 (UINT64\_MAX). <br><br> As shown in the specified format, multiple ranges can be specified for the same alias. The format requires that max value of a given range is less than the min value of the next one, i.e. {*max value1*}\<{*min value2*} <br><br> Example of a Range option for data handles: <br><br> Range mynode1 2147483651-4294967297 |

| Option: | **RootHandle** |
|---|---|
| Type: | String |
| Contexts: | FileSystem |
| Default Value: | None |
| Description: | Specifies the handle value for the root of the file system. This is a required option in the FileSystem context. The format is: <br><br> RootHandle {*handle value*}  <br><br> Where {*handle value*} is a positive integer no greater than 18446744073709551615 (UIN64\_MAX). In general its best to let the pvfs-genconfig script specify a RootHandle value for the file system. |
 
| Option: | **Name** |
|---|---|
| Type: | String |
| Contexts: | FileSystem <br> Distribution |
| Default Value: | None |
| Description: | This option specifies the name of the particular file system or distribution that its defined in. It is a required option in FileSystem and Distribution contexts. |
 

 
| Option: | **ID** |
|---|---|
| Type: | Integer |
| Contexts: | FileSystem |
| Default Value: | None |
| Description: | An OrangeFS server may manage more than one file system, and so a unique identifier is used to represent each one. This option specifies such an ID (sometimes called a 'collection id') for the file system it is defined in. <br> <br> The ID value can be any positive integer, no greater than 2147483647 (INT32\_MAX). It is a required option in the FileSystem context. |
 

 
| Option: | **TroveMaxConcurrentIO** |
|---|---| 
| Type: | Integer |
| Contexts: | Defaults <br> ServerOptions |
| Default Value: | 16 |
| Description: | Maximum number of AIO operations that Trove will allow to run concurrently |
 

 
| Option: | **LogFile** |
|---|---| 
| Type: | String |
| Contexts: | Defaults <br> ServerOptions |
| Default Value: | /tmp/pvfs2-server.log |
| Description: | The gossip interface in OrangeFS allows users to specify different levels of logging for the OrangeFS server. The output of these different log levels is written to a file, which is specified in this option. The value of the option must be the path pointing to a file with valid write permissions. The LogFile option can be specified for all the OrangeFS servers in the Defaults context or for a particular server in the Global context. |
 
| Option: | **LogType** |
|---|---| 
| Type: | String |
| Contexts: | Defaults <br> ServerOptions |
| Default Value: | file |
| Description: | The LogType option can be used to control the destination of log messages from OrangeFS server. The default value is file, which causes all log messages to be written to the file specified by the LogFile parameter. Another option is syslog, which causes all log messages to be written to syslog. |
 
| Option: | **EventLogging** |
|---|---|  
| Type: | List |
| Contexts: | Defaults <br> ServerOptions |
| Default Value: | none |
| Description: | The gossip interface in OrangeFS allows users to specify different levels of logging for the OrangeFS server. This option sets that level for either all servers (by being defined in the Defaults context) or for a particular server by defining it in the Global context. Possible values for event logging are: |

Logging Options listed in the Table Below:

| | |
|---|---|
| *Name* | *Log output for:* |
| acache | Debug the attribute cache. Only useful on the client. |
| access | Show server file (metadata) accesses (both modify and read-only). |
| access_detail | Show more detailed server file accesses |
| access_hostnames | Display the hostnames instead of IP addrs in debug output |
| all | Everything |
| bstream | Debug the bstream code 
| cancel | Debug the cancel operation |
| client | Log client sysint info. This is only useful for the client. |
| clientcore | Debug the client core app       |
| clientcore_timing | Debug the client timing state machines (job timeout, etc.)    |
| coalesce| Debug the metadata sync coalescing code|
| dbpfattrcache | Debug the server-side dbpf attribute cache |
| directio | Debug trove in direct io mode   |
| distribution | Log/Debug distribution calls    |
| endecode | network encoding |
| flow | Log flow calls|
| flowproto | Log the flow protocol events including flowproto_multiqueue |
| fsck | Debug the fsck tool |
| getattr | Debug the server getattr state machine.  |
| geteattr | Debug the client and server get ext attributes SM. |
| io  | Debug the io operation (reads and writes) for both the client and server                 || job | Log job info |
| keyval | Debug the metadata dbpf keyval functions  |             
| listattr | vectored getattr server state  machine   |
| listeattr | Debug the listeattr operation   |
| lookup | Debug the client lookup state machine. |
| mgmt | Debug direct io thread management    |
| mirror | Debug mirroring process |
| mkdir | Debug the mkdir operation (server only) |
| msgpair | Debug the msgpair state machine |
| ncache | Debug the client name cache. Only useful on the client. | 
| network | Log network debug info. |
| none | No debug output |
| open_cache | Debug the server's open file descriptor cache | 
| permissions | Debug permissions checking on the server | 
| racache | Debug read-ahead cache events. Only useful on the client. |
| readdir | Debug the readdir operation (client and server) | 
| remove | Debug the client remove state macine. | 
| reqsched | Log request scheduler events    |
| request | Debug PINT_process_request calls. (EXTREMELY verbose!) |
| seccache | Capability Cache 
| security | Debug robust security code |
| server | Log server info, including new operations.|
| setattr | Debug the server setattr state machine.|
| seteattr | Debug the client and server set ext attributes SM.|
| sm | Debug the state machine management code |
| storage | Log trove debugging info. Same as 'trove'. |
| trove | Log trove debugging info. Same as 'storage'.  |
| trove_op | Log trove operations. |
| user_dev| Show the client device events |
| usrint | Client User Interface |
| varstrip| Debug the varstrip distribution |
| verbose | Everything except the periodic events. Useful for debugging    |
| win\_client | Windows client |

The value of the EventLogging option can be a comma-separated list of the above values. Individual values can also be negated with a '-'. Examples of possible values are: 

- EventLogging flow,msgpair,io 
- EventLogging -storage 
- EventLogging -flow,-flowproto 
 
<br>

|Option:|**EnableTracing**|
|---|---|
|Type:|String|
|Contexts:|Defaults, ServerOptions|
|Default Value:|no|
|Description:|Enable code related to the use of TAU (Tuning and Analysis Utilities)|

|Option:|**UnexpectedRequests**|
|---|---|
|Type:|Integer|
|Contexts:|Defaults, ServerOptions|
|Default Value:|50|
|Description:|At startup each OrangeFS server allocates space for a set number of incoming requests to prevent the allocation delay at the beginning of each unexpected request. This parameter specifies the number of requests for which to allocate space. A default value is set in the Defaults context which will be be used for all servers. However, the default value can also be overwritten by setting a separate value in the ServerOptions context.|

|Option:|**StorageSpace**|
|---|---|
|Type:|String|
|Contexts:|Defaults, ServerOptions|
|Default Value:|None|
|Description:|DEPRECATED. Use DataStorageSpace and MetadataStorageSpace instead.|


|Option:|**DataStorageSpace**|
|---|---|
|Type:|String|
|Contexts:|Defaults, ServerOptions|
|Default Value:|None|
|Description:|Specifies the local path for the OrangeFS server to use as storage space for data files. This option specifies the default path for all servers and will appear in the Defaults context. NOTE: This can be overridden in the ServerOptions context on a per-server basis. Example: DataStorageSpace /opt/orangefs/storage/data|

|Option:|**MetadataStorageSpace**|
|---|---|
|Type:|String|
|Contexts:|Defaults, ServerOptions|
|Default Value:|None|
|Description:|Specifies the local path for the OrangeFS server to use as storage space for metadata files. This option specifies the default path for all servers and will appear in the Defaults context. NOTE: This can be overridden in the ServerOptions context on a per-server basis. Example: MetadataStorageSpace /opt/orangefs/storage/meta|

|Option:|**TCPBufferSend**|
|---|---|
|Type:|Integer|
|Contexts:|Defaults|
|Default Value:|0|
|Description:|Current implementations of TCP on most systems use a window size that is too small for almost all uses of OrangeFS. We recommend administators consider tuning the Linux kernel maximum send and receive buffer sizes via the /proc settings. The [PSC tcp tuning section for linux](http://www.psc.edu/networking/projects/tcptune/#Linux) has good information on how to do this. The *TCPBufferSend* and *TCPBufferReceive* options allow setting the tcp window sizes for the OrangeFS clients and servers, if using the system wide settings is unacceptable. The values should be large enough to hold the full bandwidth delay product (BDP) of the network. Note that setting these values disables tcp autotuning. See the [PSC networking options](http://www.psc.edu/networking/projects/tcptune/#options) for details.|

|Option:|**TCPBufferReceive**|
|---|---|
|Type:|Integer|
|Contexts:|Defaults|
|Default Value:|0|
|Description:|See the TCPBufferSend option.|

|Option:|**TCPBindSpecific**|
|---|---|
|Type:|String|
|Contexts:|Defaults, ServerOptions|
|Default Value:|no|
|Description:|If enabled, specifies that the server should bind its port only on the specified address (rather than INADDR\_ANY).|

|Option:|**ServerJobBMITimeoutSecs**|
|---|---|
|Type:|Integer|
|Contexts:|Defaults, ServerOptions|
|Default Value:|300|
|Description:|Specifies the timeout value in seconds for BMI jobs on the server.|

|Option:|**ServerJobFlowTimeoutSecs**|
|---|---|
|Type:|Integer|
|Contexts:|Defaults, ServerOptions|
|Default Value:|300|
|Description:|Specifies the timeout value in seconds for TROVE jobs on the server.|

|Option:|**ClientJobBMITimeoutSecs**|
|---|---|
|Type:|Integer|
|Contexts:|Defaults|
|Default Value:|300|
|Description:|Specifies the timeout value in seconds for BMI jobs on the client.|

|Option:|**ClientJobFlowTimeoutSecs**|
|---|---|
|Type:|Integer|
|Contexts:|Defaults|
|Default Value:|300|
|Description:|Specifies the timeout value in seconds for FLOW jobs on the client.|

|Option:|**ClientRetryLimit**|
|---|---|
|Type:|Integer|
|Contexts:|Defaults|
|Default Value:|5|
|Description:|Specifies the number of retry attempts for operations (when possible).|

|Option:|**ClientRetryDelayMilliSecs**|
|---|---|
|Type:|Integer|
|Contexts:|Defaults|
|Default Value:|2000|
|Description:|Specifies the delay in milliseconds to wait between retries.|

|Option:|**PrecreateBatchSize**|
|---|---|
|Type:|List|
|Contexts:|Defaults, ServerOptions|
|Default Value:|0, 1024, 1024, 1024, 32, 1024, 0|
|Description:|Specifies the number of handles to be preceated at a time from each server using the batch create request. One value is specified for each type of DS handle. Order is important. It matches the order in which the types are defined in the PVFS\_ds\_type enum, which lives in include/pvfs2-types.h. If that enum changes, it must be changed here to match. Currently, this parameter follows the order: PVFS\_TYPE\_NONE PVFS\_TYPE\_METAFILE PVFS\_TYPE\_DATAFILE PVFS\_TYPE\_DIRECTORY PVFS\_TYPE\_SYMLINK PVFS\_TYPE\_DIRDATA PVFS\_TYPE\_INTERNAL|

|Option:|**PrecreateLowThreshold**|
|---|---|
|Type:|List|
|Contexts:|Defaults, ServerOptions|
|Default Value:|0, 256, 256, 256, 16, 256, 0|
|Description:|Precreate pools will be "topped off" if they fall below this value. One value is specified for each DS handle type. This parameter operates the same as the PrecreateBatchSize in that each count corresponds to one DS handle type. The order of types is identical to the PrecreateBatchSize defined above.|

|Option:|**FileStuffing**|
|---|---|
|Type:|String|
|Contexts:|FileSystem|
|Default Value:|yes|
|Description:|Specifies if file stuffing should be enabled or not. File stuffing allows the data for a small file to be stored on the same server as the metadata.|

|Option:|**PerfUpdateHistory**|
|---|---|
|Type:|Integer|
|Contexts:|Defaults|
|Default Value:|10|
|Description:|This specifies the number of samples that performance monitor should keep Can be set in either Default or ServerOptions contexts.|

|Option:|**PerfUpdateInterval**|
|---|---|
|Type:|Integer|
|Contexts:|Defaults|
|Default Value:|1000|
|Description:|This specifies the frequency (in milliseconds) that performance monitor should be updated Can be set in either Default or ServerOptions contexts.|

|Option:|**BMIModules**|
|---|---|
|Type:|List|
|Contexts:|Defaults|
|Default Value:|None|
|Description:|List the BMI modules to load when the server is started. At present, only tcp, infiniband, and myrinet are valid BMI modules. The format of the list is a comma separated list of one of: bmi\_tcp bmi\_ib bmi\_gm For example: BMIModules bmi\_tcp,bmi\_ib Note that only the bmi modules compiled into OrangeFS should be specified in this list. The BMIModules option can be specified in either the Defaults or ServerOptions contexts.|

|Option:|**FlowModules**|
|---|---|
|Type:|List|
|Contexts:|Defaults|
|Default Value:|flowproto\_multiqueue,|
|Description:|List the flow modules to load when the server is started. The modules available for loading currently are: flowproto\_multiqueue - A flow module that handles all the possible flows, bmi-\>trove, trove-\>bmi, mem-\>bmi, bmi-\>mem. At present, this is the default and only available flow for production use. flowproto\_bmi\_cache - A flow module that enables the use of the NCAC (network-centric adaptive cache) in the OrangeFS server. Since the NCAC is currently disable and unsupported, this module exists as a proof of concept only. flowproto\_dump\_offsets - Used for debugging, this module allows the developer to see what/when flows are being posted, without making any actual BMI or TROVE requests. This should only be used if you know what you're doing.|

|Option:|**LogStamp**|
|---|---|
|Type:|String|
|Contexts:|Defaults, ServerOptions|
|Default Value:|usec|
|Description:|Specifies the format of the date/timestamp that events will have in the event log. Possible values are: usec: [%H:%M:%S.%U] datetime: [%m/%d/%Y %H:%M:%S] thread: [%H:%M:%S.%U (%lu)] none The format of the option is one of the above values. For example, LogStamp datetime|

|Option:|**FlowBufferSizeBytes**|
|---|---|
|Type:|Integer|
|Contexts:|FileSystem|
|Default Value:|262144|
|Description:|buffer size to use for bulk data transfers|

|Option:|**FlowBuffersPerFlow**|
|---|---|
|Type:|Integer|
|Contexts:|FileSystem|
|Default Value:|8|
|Description:|number of buffers to use for bulk data transfers|

|Option:|**RootSquash**|
|---|---|
|Type:|List|
|Contexts:|ExportOptions|
|Default Value:| |
|Description:|RootSquash option specifies whether the exported file system needs to squash accesses by root. This is an optional parameter that needs to be specified as part of the ExportOptions context and is a list of BMI URL specification of client addresses for which RootSquash has to be enforced. RootSquash tcp://192.168.2.0@24 tcp://10.0.0.\* tcp://192.168.\* ...|

|Option:|**RootSquashExceptions**|
|---|---|
|Type:|List|
|Contexts:|ExportOptions|
|Default Value:| |
|Description:|RootSquashExceptions option specifies exceoptions to the RootSquash list. This is an optional parameter that needs to be specified as part of the ExportOptions context and is a list of BMI URL specification of client addresses for which RootSquash has to be enforced. RootSquash tcp://192.168.2.0@24 tcp://10.0.0.\* tcp://192.168.\* ...|

|Option:|**ReadOnly**|
|---|---|
|Type:|List|
|Contexts:|ExportOptions|
|Default Value:| |
|Description:|ReadOnly option specifies whether the exported file system needs to disallow write accesses from clients or anything that modifies the state of the file system. This is an optional parameter that needs to be specified as part of the ExportOptions context and is a list of BMI URL specification of client addresses for which ReadOnly has to be enforced. An example: ReadOnly tcp://192.168.2.0@24 tcp://10.0.0.\* tcp://192.168.\* ...|

|Option:|**AllSquash**|
|---|---|
|Type:|List|
|Contexts:|ExportOptions|
|Default Value:| |
|Description:|AllSquash option specifies whether the exported file system needs to squash all accesses to the file system to a specified uid/gid. This is an optional parameter that needs to be specified as part of the ExportOptions context and is a list of BMI URL specification of client addresses for which AllSquash has to be enforced. An example: AllSquash tcp://192.168.2.0@24 tcp://10.0.0.\* tcp://192.168.\* ...|

|Option:|**AnonUID**|
|---|---|
|Type:|String|
|Contexts:|ExportOptions|
|Default Value:|65534|
|Description:|AnonUID tells the servers to translate the requesting client's uid to the specified one whenever AllSquash is specified. If this is not specified and AllSquash is specified then the uid used will be that of nobody. An example: AnonUID 3454|

|Option:|**AnonGID**|
|---|---|
|Type:|String|
|Contexts:|ExportOptions|
|Default Value:|65534|
|Description:|AnonGID tells the servers to translate the requesting client's gid to the specified one whenever AllSquash is specified. If this is not specified and AllSquash is specified then the gid used will be that of nobody. An example: AnonGID 3454|

|Option:|**HandleRecycleTimeoutSecs**|
|---|---|
|Type:|Integer|
|Contexts:|StorageHints|
|Default Value:|360|
|Description:|The TROVE storage layer has a management component that deals with allocating handle values for new metafiles and datafiles. The underlying trove module can be given a hint to tell it how long to wait before reusing handle values that have become freed up (only deleting files will free up a handle). The HandleRecycleTimeoutSecs option specifies the number of seconds to wait for each file system. This is an optional parameter that can be specified in the StorageHints context.|

|Option:|**AttrCacheKeywords**|
|---|---|
|Type:|List|
|Contexts:|StorageHints|
|Default Value:|DATAFILE\_HANDLES\_KEYSTR, METAFILE\_DIST\_KEYSTR, DIRECTORY\_ENTRY\_KEYSTR, SYMLINK\_TARGET\_KEYSTR|
|Description:|The TROVE layer (server side storage layer) has an attribute caching component that caches stored attributes. This is used to improve the performance of metadata accesses. The AttrCacheKeywords option is a list of the object types that should get cached in the attribute cache. The possible values for this option are: dh - (datafile handles) This will cache the array of datafile handles for each logical file in this file system md - (metafile distribution) This will cache (for each logical file) the file distribution information used to create/manage the datafiles. de - (directory entries) This will cache the handles of the directory entries in this file system st - (symlink target) This will cache the target path for the symbolic links in this file system The format of this option is a comma-separated list of one or more of the above values. For example: AttrCacheKeywords dh,md,de,st|

|Option:|**AttrCacheSize**|
|---|---|
|Type:|Integer|
|Contexts:|StorageHints|
|Default Value:|511|
|Description:|The attribute cache in the TROVE layer mentioned in the documentation for the AttrCacheKeywords option is managed as a hashtable. The AttrCacheSize adjusts the number of buckets that this hashtable contains. This value can be adjusted for better performance. A good hashtable size should always be a prime number.|

|Option:|**AttrCacheMaxNumElems**|
|---|---|
|Type:|Integer|
|Contexts:|StorageHints|
|Default Value:|1024|
|Description:|This option specifies the max cache size of the attribute cache in the TROVE layer mentioned in the documentation for the AttrCacheKeywords option. This value can be adjusted for better performance.|

|Option:|**TroveSyncMeta**|
|---|---|
|Type:|String|
|Contexts:|StorageHints|
|Default Value:|yes|
|Description:|The TroveSyncMeta option allows users to turn off metadata synchronization with every metadata write. This can greatly improve performance. In general, this value should probably be set to yes; otherwise, metadata transaction could be lost in the event of server failover.|

|Option:|**TroveSyncData**|
|---|---|
|Type:|String|
|Contexts:|StorageHints|
|Default Value:|yes|
|Description:|The TroveSyncData option allows users to turn off datafile synchronization with every write operation. This can greatly improve performance, but may cause lost data in the event of server failover.|

|Option:|**DBCacheSizeBytes**|
|---|---|
|Type:|Integer|
|Contexts:|StorageHints|
|Default Value:|0|
|Description:|Berkeley DB: The DBCacheSizeBytes option allows users to set the size of the shared memory buffer pool (i.e., cache) for Berkeley DB. The size is specified in bytes. See BDB documentation for set\_cachesize() for more info.|

|Option:|**DBCacheType**|
|---|---|
|Type:|String|
|Contexts:|StorageHints|
|Default Value:|sys|
|Description:|Berkeley DB: cache type for berkeley db environment. sys and mmap are valid values for this option|

|Option:|**DBMaxSize**|
|---|---|
|Type:|String|
|Contexts:|Defaults, StorageHints, ServerOptions|
|Default Value:|536870912|
|Description:|LMDB: when specified in the Defaults context, DBMaxSize specifies the size of the storage\_attributes and collections databases. When specified in the StorageHints context, DBMaxsize specifies the size of the collection\_attributes, dataspace\_attributes, and keyval databases. DBMaxSize can also be specified in the ServerOptions context to override the Defaults value on a per server basis. Default is 512MB if not specified.|

|Option:|**Param**|
|---|---|
|Type:|String|
|Contexts:|Distribution|
|Default Value:|None|
|Description:|This option specifies a parameter name to be passed to the distribution to be used. This option should be immediately followed by a Value option.|

|Option:|**Value**|
|---|---|
|Type:|Integer|
|Contexts:|Distribution|
|Default Value:|None|
|Description:|This option specifies the value of the parameter whose name was specified in the Param option.|

|Option:|**DefaultNumDFiles**|
|---|---|
|Type:|Integer|
|Contexts:|FileSystem|
|Default Value:|0|
|Description:|This option specifies the default number of datafiles to use when a new file is created. The value is passed to the distribution and it determines whether to use that value or not.|

|Option:|**ImmediateCompletion**|
|---|---|
|Type:|String|
|Contexts:|StorageHints|
|Default Value:|no|
|Description:| |

|Option:|**CoalescingHighWatermark**|
|---|---|
|Type:|String|
|Contexts:|StorageHints|
|Default Value:|8|
|Description:| |

|Option:|**CoalescingLowWatermark**|
|---|---|
|Type:|Integer|
|Contexts:|StorageHints|
|Default Value:|1|
|Description:| |

|Option:|**TroveMethod**|
|---|---|
|Type:|String|
|Contexts:|Defaults, StorageHints|
|Default Value:|alt-aio|
|Description:|This option specifies the method used for trove. The method specifies how both metadata and data are stored and managed by the OrangeFS servers. Currently the alt-aio method is the default. Possible methods are: alt-aio This uses a thread-based implementation of Asynchronous IO. directio This uses a direct I/O implementation to perform I/O operations to datafiles. This method may give significant performance improvement if OrangeFS servers are running over shared storage, especially for large I/O accesses. For local storage, including RAID setups, the alt-aio method is recommended. null-aio This method is an implementation that does no disk I/O at all and is only useful for development or debugging purposes. It can be used to test the performance of the network without doing I/O to disk. dbpf Uses the system's Linux AIO implementation. No longer recommended in production environments. Note that this option can be specified in either the "Defaults" context of fs.conf, or in a file system specific "StorageHints" context, but the semantics of TroveMethod in the "Defaults" context is different from other options. The TroveMethod in the "Defaults" context only specifies which method is used at server initialization. It does not specify the default TroveMethod for all the file systems the server supports. To set the TroveMethod for a file system, the TroveMethod must be placed in the "StorageHints" context for that file system.|

|Option:|**SecretKey**|
|---|---|
|Type:|String|
|Contexts:|FileSystem|
|Default Value:|None|
|Description:|Specifies the file system's key for use in HMAC-based digests of client operations.|

|Option:|**SmallFileSize**|
|---|---|
|Type:|Integer|
|Contexts:|FileSystem|
|Default Value:|None|
|Description:|Specifies the size of the small file transition point|

|Option:|**DirectIOThreadNum**|
|---|---|
|Type:|Integer|
|Contexts:|StorageHints|
|Default Value:|30|
|Description:|Specifies the number of threads that should be started to service Direct I/O operations.|

|Option:|**DirectIOOpsPerQueue**|
|---|---|
|Type:|Integer|
|Contexts:|StorageHints|
|Default Value:|10|
|Description:|Specifies the number of operations to service at once in Direct I/O mode.|

|Option:|**DirectIOTimeout**|
|---|---|
|Type:|Integer|
|Contexts:|StorageHints|
|Default Value:|1000|
|Description:|Specifies the timeout in Direct I/O to wait before checking the next queue.|

|Option:|**TreeWidth**|
|---|---|
|Type:|Integer|
|Contexts:|FileSystem|
|Default Value:|2|
|Description:|Specifies the number of partitions to use for tree communication.|

|Option:|**TreeThreshold**|
|---|---|
|Type:|Integer|
|Contexts:|FileSystem|
|Default Value:|2|
|Description:|Specifies the minimum number of servers to contact before tree communication kicks in.|

|Option:|**DistrDirServersInitial**|
|---|---|
|Type:|Integer|
|Contexts:|FileSystem|
|Default Value:|1|
|Description:|Specifies the default for initial number of servers to hold directory entries. Note that this number cannot exceed 65535 (max value of a 16-bit unsigned integer).|

|Option:|**DistrDirServersMax**|
|---|---|
|Type:|Integer|
|Contexts:|FileSystem|
|Default Value:|4|
|Description:|Specifies the default for maximum number of servers to hold directory entries. Note that this number cannot exceed 65535 (max value of a 16-bit unsigned integer).|

|Option:|**DistrDirSplitSize**|
|---|---|
|Type:|Integer|
|Contexts:|FileSystem|
|Default Value:|10000|
|Description:|Specifies the default for number of directory entries on a server before splitting.|

### Context Descriptions

This is the list of possible Contexts that can be used in the configuration file in this version of OrangeFS.

|Context:|**Defaults**|
|---|---|
|Parent Context:|Global|
|Description:|Options specified within the Defaults context are used as default values over all the OrangeFS server specific config files.|

|Context:|**StorageHints**|
|---|---|
|Parent Context:|FileSystem|
|Description:|This groups options specific to a file system and related to the behavior of the storage system. Mostly these options are passed directly to the TROVE storage module which may or may not support them. The DBPF module (currently the only TROVE module available) supports all of them.|

|Context:|**Global**|
|---|---|
|Parent Context:|None|
|Description:|Global Context|

|Context:|**Security**|
|---|---|
|Parent Context:|None|
|Description:|settings related to key- or certificate-based security options. These options are ignored if security mode is not compiled in.|

|Context:|**DataHandleRanges**|
|---|---|
|Parent Context:|FileSystem|
|Description:|This context groups together the Range options that define valid values for the data handles on a per-host basis for this file system. A DataHandleRanges context is required to be present in a FileSystem context.|

|Context:|**ServerOptions**|
|---|---|
|Parent Context:|Global|
|Description:|This groups the Server specific options. The ServerOptions context should be defined after the Alias mappings have been defined. The reason is that the ServerOptions context is defined in terms of the aliases defined in that context. Default options applicable to all servers can be overridden on a per-server basis in the ServerOptions context. To illustrate: Suppose the Option name is X, its default value is Y, and one wishes to override the option for a server to Y'. \<Defaults\> .. X Y .. \</Defaults\> \<ServerOptions\> Server {*server alias*} .. X Y' .. \</ServerOptions\> The ServerOptions context REQUIRES the Server option specify the server alias, which sets the remaining options specified in the context for that server.|

|Context:|**LDAP**|
|---|---|
|Parent Context:|Security|
|Description:|Open tag for LDAP options, used in certificate mode.|

|Context:|**MetaHandleRanges**|
|---|---|
|Parent Context:|FileSystem|
|Description:|This context groups together the Range options that define valid values for meta handles on a per-host basis for this file system. The MetaHandleRanges context is required to be present in a FileSystem context.|

|Context:|**ExportOptions**|
|---|---|
|Parent Context:|FileSystem|
|Description:|Specifies the beginning of an ExportOptions context. This groups options specific to a file system and related to the behavior of how it gets exported to various clients. Most of these options will affect things like uid translation.|

|Context:|**Distribution**|
|---|---|
|Parent Context:|FileSystem|
|Description:|Provides a context for defining the file system's default distribution to use and the parameters to be set for that distribution. Valid options within the Distribution context are Name, Param, and Value. This context is an optional context within the FileSystem context. If not specified, the file system defaults to the simple-stripe distribution.|

|Context:|**Aliases**|
|---|---|
|Parent Context:|Global|
|Description:|This groups the Alias mapping options. The Aliases context should be defined before any FileSystem contexts are defined, as options in the FileSystem context usually need to reference the aliases defined in this context.|

|Context:|**FileSystem**|
|---|---|
|Parent Context:|Global|
|Description:|This groups options specific to a file system. An OrangeFS server may manage more than one file system, so a config file may have more than one FileSystem context, each defining the parameters of a different file system.|
 