+++
title= "Advanced Security Build"
weight=130
+++

|  |  |
|---|---|
| {{<figure src="../images/icon_security.png" alt="Security Icon" width="50">}} | After you build the OrangeFS installation directory, you must continue setup and configuration if you select either the key-based or certificate-based mode of security. Much of this work can be done once on the build system, then copied to your servers and clients. In future versions of OrangeFS, security will be simplified. |

In the [Procedure]({{<relref "Build_OrangeFS#procedure">}}) section of the previous topic, you specified a security mode when running ./configure (see step 3 in Build OrangeFS).

Depending on the mode you chose (default, key-based or certificate-based), refer to the appropriate sections in this topic for additional security setup for the Build system:  

-   [Default Mode]({{<relref "#default-mode">}})
-   [Key-Based Mode]({{<relref "#key-based-mode">}})
-   [Certificate-Based Mode]({{<relref "#certificate-based-mode">}})

### Default Mode

If multi-user security is not a priority, you might have selected the default mode for optimal performance and faster installation. This mode does not require any additional setup

### Key-Based Mode

If you selected the key-based mode, you must create your security keys and a keystore file in a temporary directory on the Build system. You must then copy the keystore file to the OrangeFS installation directory.  

**Notes** To complete this procedure, you must know the host names of your OrangeFS servers and clients.   

This procedure assumes the use of an automated script provided with your OrangeFS files.  

#### Procedure

The following steps set up the Build system for key-based security. They assume the OrangeFS source is in /tmp/src/orangefs-version.  

##### 1.  Create a temporary directory on the Build system  
located outside the /tmp/src/orangefs-*version* source directory:  

cd /opt  
mkdir ofs_keys  

**Note** Later, after you have distributed key pairs to your OrangeFS servers and clients, you should either delete or limit access to this directory. If you keep this directory for future changes, secure it appropriately using best practices.

##### 2.  Change Directory (cd) to the new directory and copy two script files
from /tmp/src/orangefs-*version*/examples/keys:

cd ofs_keys
cp /tmp/src/orangefs-*version*/examples/keys/\*.sh .

**Note** You will use only one of these scripts now. You will use the second one when you add OrangeFS servers later.

 

##### 3.  With the script named pvfs2-gen-keys.sh, use the following command  
line format to generate private keys for servers and clients, as well as the keystore:  

./pvfs2-gen-keys.sh [-a] [-s *servers*] [-c *clients*]  

where...  

*servers* = server hostname(s), each separated by a space  

*Example:*  orangefs01 orangefs02 orangefs03  

*clients* = client hostname(s), each separated by a space  

*Example:*  orangefs01 client01 client02  

**Note** As this example suggests, an OrangeFS server can also be a client.  

Example of full command:  

./pvfs2-gen-keys.sh -s orangefs01 orangefs02 -c orangefs01 orangefs02  

The executed script will generate:  

The keystore, named orangefs-keystore by default, is a text file that contains the public keys for each server and client.  


| Key File Type |  File Name Format | Example |
|---|---|---|
| Server | orangefs-serverkey-*hostname*.pem | orangefs-serverkey-orangefs01.pem |
| Client | pvfs2-clientkey-*hostname*.pem | pvfs2-clientkey-client01.pem |

**Note** The -a option shown in the command line format does not apply during initial installation. Include this option only if you want the public keys to be appended to an existing keystore (named keystore by default).  

##### 4.  Copy the keystore to the etc directory in your OrangeFS installation directory:

cp keystore /opt/orangefs/etc

**Note** This is the default location for the keystore on all OrangeFS servers. If you specify a different location in the above copy command, you must reflect that change later when you create the OrangeFS configuration file.  

#### Generating Keys for Many Systems

The command line format used in step 3 above can be modified for large numbers of servers and clients, using shell expansion. For example, the following command generates server keys for orangefs-server01 to orangefs-server04 and client keys for orangefs-client01 to orangefs-client40:  

./pvfs2-gen-keys.sh -s orangefs-server0{1..4} -c orangefs-client0{1..9} orangefs-client{10..40}  

See your shell documentation for more information.  

Certificate-Based Mode
----------------------

If you selected the certificate-based mode of security, you must add a CA certificate to the OrangeFS directory on the Build system.  

If you already have one you want to use, simply copy the certificate file, along with its private key file, to /opt/orangefs/etc. Each of the files should be in PEM format (see OpenSSL documentation).  

If you need to create a CA certificate, the OrangeFS installation files include some tools to simplify the process. You must have a working knowledge of OpenSSL to tailor your certificate settings beyond the basic procedure that follows.  

#### Procedure

OpenSSL references a configuration file when it creates certificates, including CA certificates.  

**Note** This file is specifically tied to OpenSSL; it is different from the OrangeFS configuration file.

The default location for this file on the Build system is /etc/ssl/openssl.cnf, but the following procedure uses an alternative
configuration file named orangefs.cnf. That file is located in /opt/orangefs/examples/certs, and it includes basic "quick start" settings that you can modify as needed.  

**Note** For complete information on the OpenSSL configuration file format, see the config(5ssl) Linux man page.  

To create a CA certificate (using the example configuration file):  

##### 1.  Change Directory (cd) to the directory where the example configuration file is located:

cd /tmp/src/orangefs-*version*/examples/certs  


##### 2.  If necessary, customize the settings in the configuration file
(orangefs.cnf) to reflect the security settings and policies of your organization.  

##### 3.  Enter the following command:  

openssl req -config orangefs.cnf -new -x509 -outform PEM -out  
orangefs-ca-cert.pem -keyout orangefs-ca-cert-key.pem -nodes -days 1825  

**Notes** You can use different file names. You can also select a different expiration; the above example expires in 5 years (1825 days) The documentation for this command is in the req(1) Linux man page. You are prompted for configuration values after entering this command.  

##### 4.  Enter the elements of the CA certificate subject.  

The configuration file will prompt you for country, state, locality, organization, organizational unit and common name. You and your security administrator might want to discuss the values any existing certificates use and follow a similar format.  

When you submit the entries, the CA certificate and private key you specified (orangefs-ca-cert.pem and orangefs-ca-cert-key.pm in the example above) will be generated in the current directory.  

##### 5.  Move the CA certificate and private key files to the etc subdirectory:  

mv \*.pem /opt/orangefs/etc. 

#### Using the Script File

The examples/certs directory in your OrangeFS source directory also includes a script (pvfs2-cert-ca.sh) to streamline the above procedure. Its command line format includes a single optional parameter for any characters you want to add to the certificate file names.  

For example, to achieve the same results as in the above procedure, you would enter:  

./pvfs2-cert-ca.sh orangefs  

#### Restricting Access

Be sure to use chmod to restrict access to the CA key.

 

 

 

 

 

 
