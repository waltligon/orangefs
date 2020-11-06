+++
title= "Linux Client Advanced Security Modes"
weight=350
+++

|  |  |
|---|---|
| {{<figure src="../images/icon_security.png" alt="Security Icon" width="50">}} | After you copy the OrangeFS installation directory, you must perform additional setup and configuration for the advanced key-based and certificate-based security modes. |

For key-based security, most of this work can be done once on the build system, then copied later to your servers and clients.
                           
This topic includes sections for setting up two types of security:
                           
-   [Key-Based Security](Set_Up_Security_(Linux_Clients).htm#Key-Based_Security)
-   [Certificate-Based Security](Set_Up_Security_(Linux_Clients).htm#Certificate-Based_Security)
                           

Key-Based Security
------------------

Each client has its own key pair, consisting of a private key and a
public key that are cryptographically related. The private key is kept
secret while the public key can be distributed. A file used by the
servers known as the keystore contains public keys for all servers and
clients in the OrangeFS system.  When a client sends a request to the
server, it submits a credential object which is signed by its private
key. The server verifies the signature using the known public key of the
client.

**Note     **All OrangeFS clients and servers must be built for the same
security mode (key-based in this case) to interoperate.

### Generating Client Private Keys

Like servers, all client systems must have a key pair. Because you need
to build the keystore file for the servers, you should create the client
private keys on a single server—typically the one you used to create the
server private keys. You can then distribute them to the clients.

The openssl command used is the same as for the server, although for
performance reasons the size of the key (in bits) is less:

openssl genrsa -out pvfs2-clientkey.pem 1024

The size of the client private key, 1024 bits, is usually half the size
of the server keys (default 2048).

This file is typically stored in the etc directory under the OrangeFS
installation directory, /opt/orangefs/etc by default.

Keys for multiple clients can be generated in a temporary directory and
distributed to the client systems in a similar fashion as the server
keys.

### Configure Client for Key-Based Security

On OrangeFS client systems, the private key should be readable only by
root:

chmod 600 /opt/orangefs/etc/pvfs2-clientkey.pem

The default private key location is pvfs2-clientkey.pem in the etc
directory under the OrangeFS installation directory, for example
/opt/orangefs/etc/pvfs2-clientkey.pem. You can override this location by
using the --keypath parameter when running pvfs2-client. Example:

/opt/orangefs/sbin/pvfs2-client --keypath \\\
 /usr/local/orangefs/etc/pvfs2-clientkey.pem

### Copy Files to Client

The script pvfs2-dist-keys.sh distributes private keys and the keystore
to multiple systems using scp. The keys should have been generated with
the pvfs2-gen-keys.sh script as described in the *[Administration
Guide](_Automated_Key_and_Keystore_Generation_Scripts1.htm#Generating_Key_Pairs_and_a_Keystore).*

**Note     **The script requires one argument: the installation
directory of OrangeFS which must be the same on all systems. This
directory must exist prior to executing the script.

An example using the default location:

./pvfs2-dist-keys.sh /opt/orangefs

The script examines the key filenames to determine the hostname of the
target server or client. For example, the server file
orangefs-serverkey-orangefs-server01.pem will cause the script to
execute this command, given /opt/orangefs as the installation directory:

scp
orangefs-serverkey-orangefs-server01.pem    orangefs-server01:/opt/orangefs/etc/orangefs-serverkey.pem

Generate a client private key as instructed in the *[Administration
Guide](Setting_Up_Key-Based_Security_Mode.htm#Generating_Client_Private_Keys)*
and append its public key to the keystore. Distribute the private key to
the client system and the keystore to all servers.

To remove a client, edit the keystore file and remove the hostname
identifier (for example “C:client01”) and the public key that follows.
Distribute this updated keystore file to all servers.

Currently in key-based security when a client is added to (or removed
from) your OrangeFS installation, all servers must be stopped and
restarted. The keystore is read only at server startup, so you would
generally add clients during a maintenance period. Certificate-Based
Security can be used if a more dynamic system is needed.

Certificate-Based Security
--------------------------

**Note     **All OrangeFS clients and servers must be built for the same
security mode (certificates, in this case) to interoperate.

Prior to configuring your client(s) for certificate-based security, you
must configure your servers and create a CA certificate. See [Building
OrangeFS for Certificate-Based
Security](Setting_Up_Certificate-Based_Security_Mode.htm#Building_OrangeFS_for_Certificate-Based_Security__Admin_Guide_)
for steps to take before configuring clients.

Then, install OpenSSL client libraries to the client system if
necessary. (Consult your OS distribution documentation for more
information.)

### User Certificate Application

A client application, pvfs2-get-user-cert, is installed to allow users
to request and receive a user certificate with no intervention from the
administrator.

You must configure the client system to connect to a running OrangeFS
server; the file pvfs2tab, located in /opt/orangefs/etc by default,
contains the necessary configuration information. (See [pvfs2tab
File](pvfs2tab_File.htm) for more information on pvfs2tab.)

The requesting user must have an identity (user account) in the LDAP
directory before requesting a certificate. See [Configuring LDAP for
Identity
Mapping](http://www.omnibond.com/orangefs/docs/v_2_9/admin_Certificate-Based_Security.htm#Configuring_LDAP_for_Identity_Mapping)
for more information.

The usage of pvfs2-get-user-cert is:

pvfs2-get-user-cert [*user name*]

If the optional user name is not supplied, the user name of the
currently-active user account will be used. The user will be prompted
for their LDAP directory password. Once this is entered correctly, the
user certificate and private key are stored
as \~/.pvfs2-cert.pem and \~/.pvfs2-cert-key.pem, respectively.

### Obtaining a User Certificate Manually

If you do not want users to use the pvfs2-get-user-cert application,
they can create a certificate request, which an administrator can use to
generate a certificate.

#### Creating a User Certificate Request

A certificate request is a file indicating what values should be in the
requested certificate. A user can generate the request and submit the
file to the administrator for signing by the CA certificate. In a
production environment, it is not secure for users to sign their own
certificates.

To generate a certificate request, execute this command:

openssl req -newkey rsa:1024 -config pvfs2-user.cnf -keyout
pvfs2-cert-key.pem -nodes -out pvfs2-cert-req.pem

**Note     **pvfs2-user.cnf is in the examples/certs directory.

You can use different file names. The user will be prompted to enter
subject values, which should follow some organization-defined naming
scheme.

**Note     **The common name of the certificate subject will be used for
UID/GID-mapping later, so take note of it.

The user can then submit (for example via email) the certificate request
(but not the private key) to the administrator for signing.

A script named pvfs2-cert-req.sh is in examples/certs for this step. It
takes a name as an optional parameter (default “pvfs2”):

./pvfs2-cert-req.sh pvfs2

#### Signing a User Certificate Request

The administrator will sign the certificate request with the CA private
key. Execute this command:

openssl x509 -req -in pvfs2-cert-req.pem -CA orangefs-ca-cert.pem -CAkey
orangefs-ca-cert-key.pem -days 365 -out pvfs2-cert.pem

The file names should correspond with file names used in prior steps.
Return the resulting certificate file (pvfs2-cert.pem above) to the
user.

A script named pvfs2-sign-cert.sh is in examples/certs. It takes
the cert name and the CA name as optional parameters (defaults “pvfs2”
and “orangefs” respectively):

./pvfs2-cert-sign.sh pvfs2 orangefs

The files pvfs2-cert.pem and pvfs2-cert-key.pem can then be sent to the
user (for example via email).

#### Storing the User Certificate

The user can now store the certificate and private key files. The
default file names used by OrangeFS are \~/.pvfs2-cert.pem for the
certificate file and \~/.pvfs2-cert-key.pem for the key file. Note the
“.” preceding both names, which marks them hidden. The private key and
certificate should have permissions revoked for other users:

mv pvfs2-cert.pem\~/.pvfs2-cert.pem\
 mv pvfs2-cert-key.pem\~/.pvfs2-cert-key.pem\
 chmod 600\~/.pvfs2-cert\*.pem

These locations can be overridden with
the PVFS2CERT\_FILE and PVFS2KEY\_FILE environment variables. These
variables are used when accessing OrangeFS through a client application
(sysint--for example pvfs2-ls) or library (usrint); they are not used if
OrangeFS is mounted through the kernel module.

 

 

 

 

 
