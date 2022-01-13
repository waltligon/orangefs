+++
title= "OrangeFS Config File"
weight=120
+++

The OrangeFS installation directory on the Build system will need an OrangeFS configuration file. You will enter basic information for this file in a program called pvfs2-genconfig. Once the configuration file has been created, you might need to make additional modifications (regarding security, for example) for the initial deployment.
                         
**Note** After standard installation, consult the [Administration Guide]({{<relref "configuration/admin_ofs_configuration_file">}}) for details on the options and values available for fine tuning the configuration file.


##### To create the OrangeFS configuration file, follow these steps:

###### 1.  Using pvfs2-genconfig, the configuration file will be automatically

generated as follows:  
/opt/orangefs/bin/pvfs2-genconfig /opt/orangefs/etc/orangefs-server.conf. 

The program presents a series of prompts to enter the required settings for your OrangeFS configuration file.

##### 2.  Answer all the prompts to generate the configuration file in the etc
directory. Following is a list of possible entries for these prompts:

Option/Setting    
Default/Example Value  
Description   
Protocol  
tcp   

**Protocol** choices are tcp, gm, mx, ib and portals. Default is tcp. For multi-homed configurations, separate multiple protocols with commas, for example ib, tcp

- tcp - TCP/IP port number that each OrangeFS server will listen on. Default is 3334.  

- gm - GM port number (in the range of 0 to 7) that each OrangeFS server will listen on. Default is 6.  

- mx - MX board number (in the range of 0 to 4) that each server will listen on. Default is 0.  
	pvfs2-genconfig assumes that all servers will use the same board number.  

	MX endpoint (in the range of 0 to 7) that each server will listen on. Default is 3.  
	pvfs2-genconfig assumes that all servers will use the same endpoint number.  
- ib - 
	TCP/IP port that each server will listen on for IB communications. Default is 3335.  
	pvfs2-genconfig assumes that all servers will use the same port number.  

- portals - Portal index that each server will listen on for portals communications. Default is 5.  
pvfs2-genconfig assumes that all servers will use the same portal index.  

**Data Directory**  
	/opt/orangefs/storage/data   
	Full path + directory name where each OrangeFS server will store its
data.   

**Metadata Directory**  
	/opt/orangefs/storage/meta. 
	Full path + directory name where each OrangeFS server will store its
metadata.  

**Log Directory**.  
	/var/log/orangefs-server.log. 
	Full path + file name where each server will write its log messages.  

**Server Host Names**    
	Default: localhost   
	Example: ofs{1-4}   
	Hostname of each OrangeFS data server (server on which data directory is
located). This should be the value returned by the hostname command.
Syntax is node1, node2, ... or node{\#-\#,\#,\#}.  

**Data/Metadata Allowed**   
	Default: yes    
Enter yes to keep metadata directory on same server as data directory.  
If you enter no, you are prompted to enter additional hostnames.   

**Security Options**    
	Options that display for security are based on the security mode you
chose when you built OrangeFS. The three security modes are **default,
key-based or certificate-based.**    

 - Default
	If you select this security mode, you will not be prompted with any more
security options.  

 - **Key**  
	**Server key file location**   
	/opt/orangefs/etc/orangefs-serverkey.pem  
	Full path + file name where each server will store its public server
key   
	**Keystore location**   
	/opt/orangefs/etc/orangefs-keystore  
	Full path + file name where each server will store its keystore.

 - **Certificate**  
	**CA certificate file**  
	/opt/orangefs/etc/orangefs-ca-cert.pem  
	Full path + file name where each server will store a copy of the CA
certificate.  
	**CA private key file**  
	/opt/orangefs/etc/orangefs-ca-cert-key.pem  
	Full path + file name where each server will store its private key.  
	**User certificate root DN**  
	C=US, O=OrangeFS  
	The distinguished name (DN) for any existing user certificates in your LDAP setup.    
	**User certificate expiration**  
	365    
	Enter the number of days a user certificate is in effect (before
expiration)  
	**LDAP host list**  
	ldap://localhost  
	Enter the LDAP host or list of hosts. Syntax is ldap[s]://host[:port],...  
	**LDAP bind user DN**  
	cn=admin,dc=acme,dc=com    
	Enter the LDAP bind user's DN. By default, will bind anonymously.  
	**LDAP bind password**  
	*password*  *or*  passwd:/opt/orangefs/etc/ldappwd.txt  
	LDAP bind password or passwd:file\_path to specify text file with
password.  
	**LDAP search mode**  
	CN    
	Enter either CN or DN for the LDAP search mode   
	**LDAP search root**  
	ou=users,dc=acme,dc=com    
	Enter the LDAP search root object's DN      
	**LDAP search class**  
	inetOrgPerson    
	Enter the name of the LDAP search class  <br>
	**LDAP search attribute**  
	cn  
	Enter the LDAP search attribute.  
	**LDAP search scope**  
	subtree 
	Enter either onelevel or subtree for the LDAP search scope    
	**LDAP UID attribute**   
	*uidNumber*  
	Enter the LDAP UID attribute.  
	**LDAP GID attribute**   
	*gidNumber*   
	Enter the LDAP GID attribute.  
	**Verify Server List**    
	n  
	Asks (y/n) if you want to redisplay the server hostnames you entered.  

**Note** Standard installation, as configured above, places file
system storage directories inside the OrangeFS installation directory
under opt for portability. These directories can be located elsewhere
for system optimization and larger space allocations. For detailed
information on all options in the OrangeFS configuration file, see the
[Administration Guide]({{<relref "configuration/admin_ofs_configuration_file">}}).

When you are finished running pvfs2-genconfig, the OrangeFS
configuration file is added to /opt/orangefs/etc.

The configuration file is a simple text file that can be opened and
modified manually. While pvfs2-genconfig will query you about the most
important options, default values are assigned to many additional
options. You can consider changes for most of these defaults later after
installation, when you can reference the [Administration
Guide]({{<relref "configuration/admin_ofs_configuration_file">}}) for performance-tuning and
optimization.

 

 

 

 

 
