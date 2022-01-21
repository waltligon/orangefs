+++
title= "OrangeFS Config File"
weight=120
+++

The OrangeFS installation directory on the Build system will need an OrangeFS
configuration file. You will enter basic information for this file in a program
called pvfs2-genconfig. Once the configuration file has been created, you might
need to make additional modifications (regarding security, for example) for the
initial deployment.
                         
{{%notice note%}}
After standard installation, consult
[Advanced Configuration > OrangeFS Configuration File]({{<relref "configuration/admin_ofs_configuration_file">}})
for details on the options and values available for fine tuning the
configuration file.
{{%/notice%}}

## Creating the OrangeFS Configuration File

Automatically generate the configuration file using the `pvfs2-genconfig` tool:

{{<code>}}/opt/orangefs/bin/pvfs2-genconfig /opt/orangefs/etc/orangefs-server.conf{{</code>}}

The program presents a series of prompts to enter the required settings for
your OrangeFS configuration file. Answer all the prompts to generate the
configuration file in the etc directory of your OrangeFS installation.
Following is a list of possible entries for these prompts:

#### Protocol

Network protocol(s) the filesystem will use for communication. The choices are
tcp, gm, mx, ib, and portals. Default is tcp. For multi-homed configurations,
separate multiple protocols with commas (e.g. ib,tcp).

Each protocol has its own option(s) you will also need to specify. Those
options are described below.

##### TCP

- **Port Number**

  TCP/IP port number that each OrangeFS server will listen on.  
  Default: 3334

##### GM

- **Port Number**

  GM port number (in the range of 0 to 7) that each OrangeFS server will listen
  on.  
  Default: 6

##### MX

- **Board Number**

  MX board number (in the range of 0 to 4) that each server will listen on.  
  Default: 0

- **Port Number**

  MX endpoint (in the range of 0 to 7) that each server will listen on.  
  Default: 3

##### IB

- **Port Number**

  TCP/IP port that each server will listen on for IB communications.  
  Default: 3335

##### Portals

- **Portal Index**

  Portal index that each server will listen on for portals communications.  
  Default: 5

{{%notice note%}}
The pvfs2-genconfig script asumes that all servers will use the same port
number, board number, or portal index.
{{%/notice%}}

#### Data Directory

Full path + directory name where each OrangeFS server will store its data.  
Default: `<path_to_orangefs_installation>/storage/data`  
Example: `/opt/orangefs/storage/data`

#### Metadata Directory

Full path + directory name where each OrangeFS server will store its metadata.  
Default: `<path_to_orangefs_installation>/storage/meta`  
Example: `/opt/orangefs/storage/meta`

#### Log File

Full path + file name where each server will write its log messages.  
Default: `/var/log/orangefs-server.log`

#### Data Server Hostnames

Hostname of each OrangeFS data server (server on which data directory is
located). This should be the value returned by the hostname command.
Syntax is node1, node2, ... or node{\#-\#,\#,\#}.  
Default: `localhost`  
Example: `ofs{1-4}`

#### Shared Data/Metadata Servers

Yes or no whether to use the list of servers entered above for metadata
servers as well. If you enter no, you are prompted to enter a list of
hostnames for metadata servers. The same rules apply as above.  
Default: `yes`

#### Security Options

Options that display for security are based on the security mode you
chose when you built OrangeFS. The three security modes are `default`,
`key-based`, or `certificate-based.`

##### Default

If you selected this security mode, you will not be prompted with any more
security options.

##### Key-Based

- **Server key file location**

  Full path + file name where each server will store its public server key.  
  Default: `<path_to_orangefs_installation>/etc/orangefs-serverkey.pem`  
  Example: `/opt/orangefs/etc/orangefs-serverkey.pem`

- **Keystore location**

  Full path + file name where each server will store its keystore.  
  Default: `<path_to_orangefs_installation>/etc/orangefs-keystore`  
  Example: `/opt/orangefs/etc/orangefs-keystore`

##### Certificate-Based

- **CA certificate file**

  Full path + file name where each server will store a copy of the CA
  certificate.  
  Default: `<path_to_orangefs_installation>/etc/orangefs-ca-cert.pem`  
  Example: `/opt/orangefs/etc/orangefs-ca-cert.pem`

- **CA private key file**

  Full path + file name where each server will store its private key.  
  Default: `<path_to_orangefs_installation>/etc/orangefs-ca-cert-key.pem`  
  Example: `/opt/orangefs/etc/orangefs-ca-cert-key.pem`

- **User certificate root DN**

  The distinguished name (DN) for any existing user certificates in your LDAP
  setup.  
  Default: `"C=US, O=OrangeFS"`

- **User certificate expiration**

  The number of days a user certificate is in effect (before expiration).  
  Default: `365`

- **LDAP host list**

  The LDAP host or list of hosts.  
  Syntax: `ldap[s]://host[:port],...`  
  Default: `ldap://localhost`

- **LDAP bind user DN**

  The LDAP bind user's DN. By default, will bind anonymously.  
  Example: `cn=admin,dc=acme,dc=com`

- **LDAP bind password**

  Either the LDAP bind password or `file:<path>`, where `<path>` is the path
  to a text file containing the password.  
  Example: `file:/opt/orangefs/etc/ldappwd.txt`

- **LDAP search mode**

  Either CN or DN for the LDAP search mode.  
  Default: CN

- **LDAP search root**

  The LDAP search root object's DN.  
  Example: `ou=users,dc=acme,dc=com`

- **LDAP search class**

  The name of the LDAP search class.  
  Default: `inetOrgPerson`

- **LDAP search attribute**

  The LDAP search attribute.  
  Default: `cn`

- **LDAP search scope**

  Either "onelevel" or "subtree" for the LDAP search scope.  
  Default: `subtree`

- **LDAP UID attribute**

  The LDAP UID attribute.  
  Default: `uidNumber`

- **LDAP GID attribute**

  The LDAP GID attribute.  
  Default: `gidNumber`

#### Verify Server List

Asks (y/n) if you want to redisplay the server hostnames you entered.  
Default: `n`

{{%notice note%}}
Standard installation, as configured above, places file system storage
directories inside the OrangeFS installation directory under opt for
portability. These directories can be located elsewhere for system optimization
and larger space allocations. For detailed information on all options in the
OrangeFS configuration file, see
[Advanced Configuration > OrangeFS Configuration File]({{<relref "configuration/admin_ofs_configuration_file">}}).
{{%/notice%}}

### Results

When you are finished running pvfs2-genconfig, the OrangeFS configuration file
is saved to the file specified on the command line; otherwise, if no filename
was specified, it is saved to
`<path_to_orangefs_installation>/etc/orangefs.conf` (e.g.
`/opt/orangefs/etc/orangefs.conf`).

The configuration file is a simple text file that can be opened and
modified manually. While pvfs2-genconfig will query you about the most
important options, default values are assigned to many additional options. You
can consider changes for most of these defaults later after installation, when
you can reference
[Advanced Configuration > OrangeFS Configuration File]({{<relref "configuration/admin_ofs_configuration_file">}})
for performance-tuning and optimization.

 

 

 

 

 
