+++
title= "Client Administration"
weight=470
+++

Information in this topic includes:

-   [Configuration](#Configuration)
-   [Configuring the Client Security
    Mode](WinClient_Admin.htm#Configuring_the_Client_Security_Mode)
-   [User Mapping](#User_Mapping)
-   [Installing and Using Globus
    Toolkit](#Notes_on_Installing_Globus_Toolkit) (certificates only)
-   [Troubleshooting](#Troubleshooting)
-   [Source Code](#Source_Code)

Configuration
-------------

Two configuration files exist for the OrangeFS Client:

-   orangefstab
-   orangefs.cfg

 

Both text files are located in the installation directory
(C:\\OrangeFS\\Client) by default and can be edited.

The orangefstab file contains only one line entry, which is the URI
address of the OrangeFS server to be mounted for Windows Client access.

The orangefs.cfg file can contain a wide range of settings, including:

-   The drive letter on your Windows system that is associated with
    OrangeFS
-   The user mapping option your client is configured for (list,
    certificate, ldap)
-   Various additional settings for each of the user mapping options
-   Debug settings for logging and troubleshooting  

 

**Note     **Because the configuration files can be altered to change
security information, only administrative users should have access to
change them. For security information, see your Windows documentation.

 

### Working with the orangefstab File

The orangefstab file uses the same format as Linux/UNIX mtab (mounted
file system table) files. Here is a sample line entry in orangefstab:

tcp://orangefs.acme.com:3334/orangefs /mnt/orangefs pvfs2
defaults,noauto 0 0

-   Since only one file system can be mounted, only one line can be
    used.
-   The first field is a URI that specifies an OrangeFS file system
    server. The format is:

tcp://*hostname*:*port*/fs*\_name*

where...

*hostname* = OrangeFS server host name

*port* = port number

*fs\_name* = OrangeFS installation name

TCP is the only protocol supported on Windows. The default port is 3334.
The file system name can be determined from the server configuration
file (default is orangefs).

-   The second field is the internal UNIX-style mount point. This value
    should be the same for all clients (Windows or Linux/UNIX). The
    other fields should be left as-is above.

 

### Working with the orangefs.cfg File

Most of the Windows Client configuration information is contained in
orangefs.cfg, a text file that contains lines in the form:

*keyword* *option\_value*

You can specify comments using the \# character:

\# This is a comment.

 

#### Keyword: mount {.normal_indent_1}

The first essential keyword is mount. It specifies the drive letter
associated with the mounted OrangeFS server.

Example:

mount O:

This example will mount the file system on O: drive. (You must include
the colon.) If you do not use the mount keyword, the first
alphabetically available drive, starting with E:, is used by default.

 

#### Keyword: user-mode {.emphasis}

The user-mode keyword sets the user mapping mode. The Client will not
start if it is not included in the file. The option value must be list,
certificate or LDAP.

Example:

user-mode list

**Note     **The user-mode keyword is at the top level of a hierarchy of
keywords for configuring user mapping, discussed in more detail in the
next section.

 

#### Keywords: new-file-perms, new-dir-perms

The new-file-perms and new-dir-perms keywords change the initial
permissions mask of newly created files and directories. If these
keywords are not present, the default permissions mask is 755
(rwxr-xr-x).

**Note     **For more information about the permissions mask, see the
Linux/UNIX chmod man page.

The keywords are used with an octal integer value representing the
permissions mask.

Examples:

new-file-perms 644\
 new-dir-perms 700

The first example will cause new files to be created with “rw-r--r--“
permissions.

The second will create directories with “rwx------“ permissions.

**Note     **While you can set the “sticky bit” in OrangeFS, it has no
effect.

**Important  **Ensure that the file owners always have read permissions
to their own files (mask 400), and read and execute permissions to their
own directories (mask 500). Otherwise, they cannot read these files and
directories after creation.

 

#### Keywords: debug, debug-file, debug-stderr

The debug, debug-file and debug-stderr keywords log detailed debugging
information. If you specify the debug keyword by itself, client-related
messages are recorded in orangefs.log in the installation directory
(C:\\OrangeFS\\Client by default). You can change the name and location
of the log file by using the debug-file keyword.

Example:

debug-file C:\\Temp\\myfile.log

You can also use any of the debugging flags available with OrangeFS. For
a list of these flags, see the OrangeFS system documentation. The client
flag is win\_client.

Example:

debug win\_client io msgpair

In this example, you would log debugging information about the client,
I/O and message pair operations.

The debug-stderr keyword is used with no option value and prints
debugging messages to the console. This keyword is useful only if
orangefs-client.exe is running as a normal executable (not as a
service).

#### Keywords Table

Following is a list of all keywords available for use in the
orangefs.cfg file.


| Keyword | Description |
|---|---|
|mount | Sets the Windows drive letter to represent the OrangeFS file system. |
| user-mode | Sets the authentication/security mode used to map Windows user accounts with OrangeFS user accounts. Three possible option values:  <br> **- list:** This mode directly matches one Windows ID with one OrangeFS UID and primary GID.   <br>  **- certificate:** This mode maps digital certificates to OrangeFS UID/GID   <br>  **- ldap:** This mode enables Windows user ID to be looked up in an identity directory that supports LDAP. Examples: Active Directory, eDirectory, Open LDAP. |
| user | Used only when value for user-mode keyword is list. <br> <br> Specifies a user. A separate line entry with this keyword is required for each user. Each time it is used, you must enter it in a line that occurs below the user-mode keyword line. |
| ca-path | Used only when value for user-mode keyword is certificate. <br> <br> Sets path to file for CA (Certificate Authority) certificate. If you use this keyword, you must enter it in a line that occurs below the user-mode keyword line. |
| cert-dir-prefix | Used only when value for user-mode keyword is certificate. <br> <br> Sets the location of your user and proxy certificates if the user's default profile directory is not being used. If you use this keyword, you must enter it in a line that occurs below the user-mode keyword line. The option value is the alternative path. |
| ldap-host| Used only when value for user-mode keyword is ldap. <br> <br> Sets the host computer that is running ldap. If you use this keyword, you must enter it in a line that occurs below the user-mode keyword line. |
| ldap-bind-dn | Used only when value for user-mode keyword is ldap. <br> <br> Sets a user DN to bind to. If you use this keyword, you must enter it in a line that occurs below the user-mode keyword line. |
| ldap-bind-password | Used only when value for user-mode keyword is ldap. <br> <br> Sets a user password. If you use this keyword, you must enter it in a line that occurs below the user-mode keyword line.|
| ldap-search-root | Used only when value for user-mode keyword is ldap. <br> <br> Specifies the DN of the directory container object where searches should begin. If you use this keyword, you must enter it in a line that occurs below the user-mode keyword line. |
| ldap-search-class | Used only when value for user-mode keyword is ldap. <br> <br> Specifies object class that the user object must be. If you use this keyword, you must enter it in a line that occurs below the user-mode keyword line. |
| ldap-search-scope | Used only when value for user-mode keyword is ldap. <br> <br> Sets the scope of user searches. If you use this keyword, you must enter it in a line that occurs below the user-mode keyword line. |
| ldap-naming-attr | Used only when value for user-mode keyword is ldap. <br> <br> Sets the attribute on the user object that must exactly match the Windows user ID. If you use this keyword, you must enter it in a line that occurs below the user-mode keyword line. |
| ldap-uid-attr | Used only when value for user-mode keyword is ldap.<br> <br> Specifies the attributes with store the OrangeFS UID. If you use this keyword, you must enter it in a line that occurs below the user-mode keyword line. |
| ldap-gid-attr | Used only when value for user-mode keyword is ldap. <br> <br> Specifies the attributes with store the OrangeFS GID. If you use this keyword, you must enter it in a line that occurs below the user-mode keyword line. |
| new-file-perms | Specifies the permissions mask that new OrangeFS files will have. |
| new-dir-perms | Specifies the permissions mask that new OrangeFS directories will have. |
| debug | Specifies for all client-related messages to be logged in orangefs.log. |
| debug-file | Sets a custom name and location of the log file to be used for debugging (in place of orangefs.log). |
| debug-stderr | Sets all debugging messages to print to console. Works only when the executable, orangefs-client, is running (rather than the service). |


Configuring the Client Security Mode
------------------------------------

**A**n OrangeFS installation operates in one of three security modes:
default, key- and certificate-based (see [Preview
Security](Preview_Security.htm)). You must configure the Client’s
security mode to match that of the servers or it cannot access the file
system.

**Note     **The “certificate” security mode is distinct from the
“certificate” user-mapping mode.

The security mode is specified by the security-mode keyword in
orangefs.cfg (see above). Its value is one of default, key and
certificate. (The default value is default.)

### Configuring the Client for Default Security

This mode offers checking file object permissions (i.e. read, write and
execute) against the owner's identity but does not prevent user
impersonation. You may use any user-mapping mode with this security
mode, which requires no further configuration.

### Configuring the Client for Key-Based Security

In this mode, each client and server has a key pair consisting of a
public key and a private key. Public keys are stored in a server-side
file known as the keystore, while each client or server stores its
private key in a protected file. See [Set Up
Security](Setup_Security_(Build).htm) for instructions for generating
key pairs. The generated private key for the client should be
transferred to the client’s local file system.

In key mode, the key-file keyword in orangefs.cfg must be present and
must specify the absolute path to the private key file.

Example:

key-file C:\\OrangeFS\\Client\\orangefs-key.pem

**Note     **You should protect this file using Windows security so that
it cannot be accessed by non-administrative accounts.

The client’s public key must be stored in the keystore on each server as
normal.

### Configuring the Client for Certificate-Based Security

In this mode, each user has a certificate which stores identifying
information and their public key. A private key corresponds to the
public key, with the certificate and private key being stored in
separate files on the local (or user-shared) file system.

The key-file and cert-file keywords in orangefs.cfg specify absolute
paths to these files. However, because each pair of files is
user-specific, use the %USERNAME% token to specify how the path is
formed.

Example:

key-file C:\\Users\\%USERNAME%\\orangefs-cert-key.pem\
 cert-file C:\\Users\\%USERNAME%\\orangefs-cert.pem

Additionally, a certificate and private key must be obtained for the
SYSTEM user which performs basic OrangeFS operations such as retrieving
the disk space. You must place these files in the same directory as
orangefs-client.exe, C:\\OrangeFS\\Client by default. (See[Using the
orangefs-get-user-cert
App](WinClient_Admin.htm#Using_the_orangefs-get-user-cert_App) below.)

Users can generate their own certificates and private keys using the
orangefs-get-user-cert app.

Users must not be able to read other users’ private keys, so you should
protect them using Windows security. Users who do not have a certificate
receive an “access denied” message when attempting to access OrangeFS.

User-mapping is done on the server for this mode, so the user-mapping
mode must be “server”.

### Using the orangefs-get-user-cert App

An OrangeFS installation may have a dynamic pool of users who need
access to files. Having an administrator generate credentials for every
user request would be a needless waste of time. Instead, users can use
the orangefs-get-user-cert app to create a private key and retrieve a
certificate from the server.

First, users must have and know their OrangeFS user name and password;
these correspond to their identity stored in the server-side LDAP
directory. It is convenient to make them match their Windows
credentials, but not necessary.

Then they may run the orangefs-get-user-cert app from the client. The
executable is in C:\\OrangeFS\\Client. You may create a shortcut to it.

Users will first be prompted for their OrangeFS user name, with their
Windows user name given as a default. Then they’re prompted for their
password. If these are entered correctly, their credential files are
stored in the directory specified by orangefs.cfg (typically their
profile directory).

Additionally, an administrator can create a certificate for the SYSTEM
user by specifying the -s option when running the app. The OrangeFS user
name is typically root (UID 0), but can be any OrangeFS user.

Below is the full usage of the app:

orangefs-get-user-cert [-h|--help] [options...] [username]

| Option | Description |
|---|---|
| -s  <br> --system | generates files for the SYSTEM user|              
| -c *path* <br>  --certfile=*path* | full path for certificate file storage (overrides orangefs.cfg) |
| -k *path* <br> --keyfile=*path* | full path for private key file storage (overrides orangefs.cfg) |
| -x *days* <br> --expiration=*days*  | expiration time days (default set in server configuration file) |

User Mapping
------------

The Windows Client maps Windows user IDs to OrangeFS Linux/UNIX-based
UIDs for authentication. The user-mode keyword in orangefs.cfg specifies
the type of user mapping. There are three modes of user mapping,
detailed below.

### List Mode

This simple form of mapping allows you to list Windows user IDs and
their corresponding OrangeFS UIDs and primary GIDs. The list is created
in orangefs.cfg. Here is the format of each line:

user *windows\_userid* *uid*:*gid*

Example:

user ofsuser 500:100

A separate line entry with the user keyword is required for each user.
Each time you use this keyword, you must enter it in a line that occurs
below the user-mode keyword line.

File operations originating from the specified Windows user ID will be
carried out on OrangeFS as the specified UID.

### Certificate Mode

This section includes:

-   A summary of the currently supported approach to certificate mapping
    for the Windows Client, including the supported software package for
    implementing this approach
-   The three types of certificates that must be in place before
    configuring the Windows Client for certificate mapping
-   The configuration settings for certificate mapping that can be set
    in orangefs.cfg, either during installation or manually

**Important  **This topic *does not* discuss how to create certificates.
For details on the mechanics of generating certificates, see [Notes on
Installing and Using Globus
Toolkit](#Notes_on_Installing_Globus_Toolkit) later in this topic.

#### Certificates for Grid-Computing

With OrangeFS, certificates for user mapping and security are often
associated with grid computing. Therefore, the OrangeFS team chose to
support the certificate generation capabilities of Globus Toolkit (an
open source utilities package for grid computing) in its early
implementation of the Windows Client.

Specifically, the Globus Toolkit components used to generate
certificates for the Windows Client are MyProxy and SimpleCA.

If you select the certificate mode for user mapping, the certificates
must already have been generated and placed in their appropriate
locations. For more information on meeting these certificate
requirements for Windows Client, see [Notes on Installing and Using
Globus Toolkit](#Notes_on_Installing_Globus_Toolkit) below.

Future releases of the Windows Client will address alternatives to
Globus Toolkit. Until then, if you wish to implement a certificate
solution other than the one used here, please contact Technical Support.

#### Certificate Requirements

The Client uses X.509 certificates to identify users. The certificates
contain the UID and GID to be used on the OrangeFS server. Because
OrangeFS currently expects trusted clients, the certificates *do not
provide true security*. However, they will limit the actions of typical
users, such as preventing deleting files they do not own. Note that
support for untrusted clients will be added to OrangeFS in an upcoming
release.

Three types of certificates must be in place for the Windows Client:

-   CA (certificate authority)
-   Proxy
-   User

 

The following table describes each type.

  ------- --------------------- ------------------------------------ --------------------------------------------------------------------------------------------
  Type    Example Name          Default Location on Windows Client   Default Location on Globus Toolkit System

  CA      cacert.pem            C:\\OrangeFS\\Client\\CA             *home*/.globus/cacert.pem \
                                                                     ...where *home* is the home directory of the user who installed SimpleCA (typically root).

  Proxy   cert.0                C:\\Users\\*userid*                  /tmp/x509up\_u250 \
                                                                     ...for UID 250

  User    cert.1, cert.2, ...   C:\\Users\\*userid*                  *home*/.globus/cacert.pem \
                                                                     ...where *home* is the home directory of the user who installed SimpleCA (typically root).
  ------- --------------------- ------------------------------------ --------------------------------------------------------------------------------------------

#### Configuration File

Two configuration file keywords are associated with the certificate mode
for user mapping: ca-path and cert-dir-prefix.

To store the CA certificate in a non-default location on the Windows
Client, you can add a line entry to orangefs.cfg that begins with the
ca-path keyword, followed by the custom path.

Example:

*ca-path* C:\\Certificates\\OrangeFS\\CA

To store the user and proxy certificates in a non-default location on
the Windows Client, you can add a line entry to orangefs.cfg that begins
with the cert-dir-prefix keyword, followed by a prefix directory path to
be placed in front of the certificate user directory.

Example:

cert-dir-prefix C:\\Certificates\\OrangeFS

When the Client attempts to locate the proxy and user certificates for a
user, it will append the *userid* as a directory name to the
cert-dir-prefix. Using the above example, the certificates for user
bsmith would be placed in C:\\Certificates\\OrangeFS\\bsmith\\ using the
cert-dir-prefix above.

### LDAP Mode

LDAP (Lightweight Directory Access Protocol) mapping allows the Windows
user ID to be looked up in an identity directory that supports LDAP.
LDAP directory examples include Microsoft Windows Active Directory and
Novell\* eDirectory. Consult your directory documentation for
information on LDAP.

LDAP options for the Windows Client are specified in orangefs.cfg. All
keywords described in this section must occur below user-mode ldap line
entry.

#### Connecting over LDAP

First you must specify the host computer running LDAP. This is done with
the ldap-host keyword in the following format:

ldap-host ldap[s]://*hostname*:*port*

If ldaps is specified, a secure connection is used; otherwise, the
connection is plain text. The default secure port is 636, and the
default plain text port is 389, but you can alter the port as shown
above.

Example:

ldap-host ldaps://myldaphost.acme.com:1636

You can bind to the directory anonymously if it allows, or you can
specify a user and password with the ldap-bind-dn and ldap-bind-password
keywords:

ldap-bind-dn *bind\_user\_dn* (login)\
 ldap-bind-password *password*

Example:

ldap-bind-dn cn=orangefs-user,ou=special,o=acme\
 ldap-bind-password S3crt!

Because the password is stored in plain text in the configuration file,
give the binding user minimal rights to the directory. For more
information, see [LDAP Security](#LDAP_Security) below.

#### Search Options

The Windows Client will search LDAP for the Windows user ID making the
file system request. The search options specify how the directory is
searched.

First, the ldap-search-root keyword specifies the DN of the directory
container object where the search should begin.

Example:

ldap-search-root ou=cluster-users,o=acme

The ldap-search-scope keyword can be either onelevel or subtree. If
onelevel is specified, only the object specified with ldap-search-root
is searched—no descendant objects (sub-containers) are searched. If
subtree is specified, the object specified with ldap-search-root is
searched along with all descendant objects. The default is onelevel.

Example:

ldap-search-root subtree

The Client will form an LDAP search string in the following form:

(&(objectClass=*ldap-search-class*)(*ldap-naming-attr*=*windows\_userid*))

The ldap-search-class keyword specifies the required object class of the
user object. Typical values are User or inetOrgPerson.

Example:

ldap-search-class User

The ldap-naming-attr keyword indicates the attribute on the user object
that must exactly match the Windows user ID. Consult your documentation
to determine if the comparison is case-sensitive (typically it is not).
Typical values might be cn or name.

Example:

ldap-naming-attr cn

 

#### Attribute Options

The ldap-uid-attr and ldap-gid-attr keywords specify the attributes
which store the OrangeFS UID and primary GID, respectively. The Windows
Client retrieves these values for use on the file system.

Example:

ldap-uid-attr *uidNumber*\
 ldap-gid-attr *gidNumber*

#### LDAP Security

Because the LDAP binding password is stored as plain text, give the
binding user minimal rights to the LDAP directory. Alternatively,
minimal rights can be given to users who bind anonymously—no password is
stored in this case. Here are rights to consider:

-   Rights to search objects in the search root and below
-   Rights to read the object class, naming attribute, UID attribute and
    GID attribute from searchable objects
-   No write/delete/administrator rights

For performance, UID/GID credentials are cached for a time after lookup.
If you need to revoke rights, you must restart the OrangeFS Client
service.

You should also use an encrypted connection to LDAP if possible, by
specifying ldaps in the host URI.

Notes on Installing Globus Toolkit
----------------------------------

This section provides supplementary information about Globus Toolkit.
The information applies only to Windows Clients that use the certificate
mode for user mapping.

With OrangeFS, certificates for user mapping and security are often
associated with grid computing. Therefore, the OrangeFS team chose to
support the certificate generation capabilities of Globus Toolkit (an
open source utilities package for grid computing) in its early
implementation of the Windows Client.

**Note     **Future releases will accommodate alternatives to the Globus
Toolkit approach. Until then, if you wish to implement a certificate
solution other than the one described here, please contact [Technical
Support](Technical_Support.htm).  

Whether you are new to Globus Toolkit or you have already installed it
for certificate generation, the guidelines and suggestions in this
section ensure optimal certificate configuration for the Windows Client.

### Introduction

The Client can use X.509 certificates to identify users. The
certificates contain the UID and GID to be used on the OrangeFS server.
Because OrangeFS currently expects trusted clients, the certificates *do
not provide true security*. However, they will restrict the actions of
typical users, such as deleting files they do not own. Note that support
for untrusted clients will be added to OrangeFS in an upcoming release.

#### Identifying Certificate Format

The certificate that identifies the OrangeFS user is called the
identifying certificate. It is a proxy certificate, which allows
authorization on behalf of an “end entity,” in this case, a user. This
user is represented by a user certificate.

Proxy certificates contain authorization information in a data field
known as a policy. For the Client, the policy is a UTF-8 string in the
form *uid*/*gid*. For example, with OpenSSL, the proxy specification for
UID 250 and primary GID 100 would be as follows:

language=id-ppl-anyLanguage\
 pathlen=0\
 policy=text:250/100

More information on generating this certificate is provided below.

#### Certificates and Validation

The identifying certificate is useful only if it can be validated
against its signing certificate. The signing certificate might also
require validation against the certificate that signed it, and so on,
forming a certificate chain. Ultimately, the chain must end at the
trusted, self-signed certificate of a certificate authority (CA).

### Installing Globus Toolkit

Install Globus Toolkit on one of the OrangeFS servers or another Linux
system that shares the same user information (UIDs/GIDs).

Installation instructions for Globus Toolkit can be obtained at
[http://www.globus.org/toolkit/docs/latest-stable/](http://www.globus.org/toolkit/docs/latest-stable/).
The *Quickstart* instructions will provide a default configuration for
MyProxy, including a CA called SimpleCA.

Many different security options can be configured. For example, a
third-party certificate authority can be used. As long as the
identifying certificate follows the format above, the client will accept
the certificate.

### Locating the CA Certificate

If SimpleCA is being used, the default CA certificate is
*home*/.globus/cacert.pem, where *home* is the home directory of the
user who installed SimpleCA, typically root. If a third-party CA is
being used, the certificate will be located in an
implementation-dependent location. The security administrator of the
grid should be able to locate the file.

The CA certificate must be copied to the Windows Client system after the
Client is installed. For the file location, see [Client Certificate
Locations](#Client_Certificate_Locations) below.

### Using Grid-Based Certification

To use grid-based certification, the user must first have a user
certificate. To obtain this certificate, the user runs grid-cert-request
to generate a certificate request file. At that time, the user specifies
the certificate pass phrase. This file is then delivered to the CA
organization, where a human agent will review the request and return a
user certificate signed by the CA certificate. The certificate will be
stored in home/.globus/usercert.pem, where *home* is the home directory
of the user who installed SimpleCA, typically root. If the grid
installation is using SimpleCA, the certificate request can be processed
by a local administrator using the grid-ca-sign command.

The grid-proxy-init command can then be used to obtain a proxy
certificate. Create a file (cert-policy, for example) to contain the
policy text, which is formatted *uid*/*gid*. For example, the file would
contain 250/100 for a user with UID 250 and GID 100. The grid-proxy-init
command can be used to generate the proxy certificate with the example
cert-policy file, as follows:

grid-proxy-init -policy cert-policy -pl id-ppl-anyLanguage

When the user enters the certificate pass phrase, the proxy certificate
is generated.

To simplify this command, the OrangeFS installation package includes the
script Tools\\pvfs2-grid-proxy-init.sh. This will generate the policy
file and run grid-proxy-init.

The resulting proxy certificate is stored by default at
/tmp/x509up\_u*uid*. Example for UID 250: /tmp/x509up\_u250

Transfer this certificate to the Windows Client system, along with the
user certificate. For the file location, see [Client Certificate
Locations](#Client_Certificate_Locations) below. The proxy certificate
must be renamed cert.0, and the user certificate cert.1.

### Delegating Identities for Clusters

The use of identifying proxy certificates allows the identity of the
user to be separated from the actual Windows user ID making a file
system request. This ability is useful for clusters.

For example, when a user with a Windows user ID of JSmith executes a job
on a cluster node, the job scheduler uses Windows user ID ClusterUser.

The system administrator would set the certificate directory prefix to
C:\\ClusterWork. A directory called ClusterUser would be created under
ClusterWork. The job scheduler would transfer certificates to the
C:\\ClusterWork\\ClusterUser directory. When ClusterUser makes file
system requests, it will use the certificates of JSmith, so requests on
the file system will use the UID of JSmith. When a different user uses
the node, that user’s certificates will be used.

### Certificate Expiration and Renewal

For performance, the Client caches the OrangeFS user identity (UID/GID)
until the proxy certificate expires.

By default, Globus Toolkit proxy certificates expire after 12 hours. If
jobs requiring more time are expected, a means for the user to renew the
certificate should be provided.

One way to do this is to have the user to run grid-proxy-init again.
This will overwrite the current proxy. Then the new proxy certificate
can be transferred to the Client system (overwriting the current
certificate) without interrupting the current job.

### Client Certificate Locations

The certificates are stored as PEM-format files on the Windows Client
system. The identifying certificate’s name is cert.0. Because the
identifying certificate is associated with a Windows user, it is stored
in its user’s profile directory by default. On most systems this is
C:\\Users\\.

Example:  C:\\Users\\jsmith

Alternatively, you can specify a certificate prefix directory in the
client configuration file, C:\\OrangeFS\\Client\\orangefs.cfg by
default. Use the cert-dir-prefix keyword to specify this directory. The
user’s *userid* will be appended as a directory name to the prefix
directory.

Example configuration file line entry:

cert-dir-prefix M:\\OrangeFS Users

For user jsmith, the identifying certificate will be M:\\OrangeFS
Users\\jsmith\\cert.0.

The identifying certificate must be verified by its end-entity
(sometimes called a user) certificate. Place this certificate in the
same directory as the identifying certificate, with the name cert.1.
Additional intermediate certificates can be placed in the same directory
with names cert.2, cert.3, and so on.

The CA certificate is placed in the OrangeFS CA directory with the name
cacert.pem. By default this is C:\\OrangeFS\\Client\\CA\\cacert.pem.
This path can be changed in the configuration file using the ca-path
directive in the configuration file.

Example:

*ca-path* M:\\OrangeFS Certificates\\orangefs-cacert.pem

Troubleshooting
---------------

To troubleshoot problems, check the Application Event Log in the Event
Viewer utility. You can also turn on detailed debugging (see
[Working\_With\_The\_orangefs.cfg\_File](#Working_With_the_orangefs.cfg_File)).

Startup errors are logged to the Windows Event Log.

The configuration file has some strict requirements, so the Windows
Client will log an error to Event Log and exit if there is a problem.
The event message should give an exact explanation of the problem with
the configuration file. Correct the problem and restart the OrangeFS
Client service.

ensure network connectivity is available between the Client system and
the server(s) hosting OrangeFS. Check firewall settings and network
access lists.

For information about the debug and related keywords, see
[Configuration](#Configuration). You can use the generated file
orangefs.log to diagnose problems. A file named service.log is also
created in the installation directory when debugging is enabled and can
provide more detail on startup errors.

Note that many debug messages are low-level and require extensive
knowledge of OrangeFS/PVFS2 to interpret. For more information, consult
the OrangeFS and PVFS2 system documentation.

Free and commercial support is available at
[http://www.orangefs.org](http://www.orangefs.org).

Source Code
-----------

The OrangeFS team intends to provide all source code needed for building
the Client.

Currently, a source code package is available at
[http://www.orangefs.org](http://www.orangefs.org). (The Windows package
is separate from the Linux/UNIX package.) Build instructions will be
released at a later date.

 

 

 
