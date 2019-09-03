+++
title= "Configure LDAP"
weight=130
+++

When an OrangeFS server receives a certificate from a client, it
performs identity mapping with the certificate. The certificate contains
a subject distinguished name (DN) to identify it, while the server needs
a numerical user ID (UID) and primary group ID (GID). In order to do the
mapping, an LDAP directory is used. The subject DN is transformed in a
configurable way to locate a user object in the LDAP directory; the
object contains the UID and GID.

OrangeFS is designed to use OpenLDAP client libraries, which are
available for most distributions. The OrangeFS server can communicate
with an OpenLDAP server or a standard LDAP server from another
organization.

For more information on LDAP
see[ http://openldap.org](http://openldap.org/).

Planning for LDAP Identity Mapping
----------------------------------

First, identify which users will be allowed to use OrangeFS. These users
will require user certificates and must have a user object in the LDAP
directory. Information on creating users in LDAP is provided below.

You might be able to leverage an existing LDAP directory. Use the
information below to evaluate how existing LDAP user objects can be
utilized.

The next step is to identify a string to be uniquely associated with
each user. The most obvious is the login name, the first field
of /etc/passwd, with which users log in. However, if you have existing
LDAP users, use their naming attribute values (often the “CN” or “UID”
attribute).  The description field of /etc/passwd could also be used.
Any string will work as long as it is unique to each user. This value is
the “user name”.

Determine the naming attribute for user objects in your LDAP directory.
When creating a new LDAP directory, Common Name (CN) is a good choice.

Next, determine where in LDAP the users will be, or are, stored. LDAP
directories are hierarchical trees, where objects are identified by
distinguished names (DNs). A DN consists of segments in the
form **“****attribute**=**value****”**, separated by commas. The DN
“ou=Users,dc=acme,dc=com” indicates an organizational unit (OU) named
Users under the acme domain context (DC), which in turn is in the com
DC. Objects containing other objects are called containers; some typical
container classes are domain contexts (DC), organizations (O) and
organizational units (OU). Often the DNS name of an organization is used
to form the domain contexts at the root of the directory, for example
acme.com becomes “dc=acme,dc=com”.

Determine the DN of the container that contains all the users to enable
for OrangeFS. In some cases the users are in multiple containers; if so,
select the container at the “highest” point that contains all
subcontainers with users. For example if users are in both
“ou=Engineering,ou=Users...” and “ou=Sales,ou=Users...”, make a note of
“ou=Users” as the container. Also note whether the users are in one
container or multiple containers.

Finally, you must know where the UID and GID values are stored in LDAP.
Objects in LDAP have named attributes, which can have one or more
values. The default attributes that store the UID and GID are uidNumber
and gidNumber. If you are using the OpenLDAP server, use the schema
file nis.schema to enable these attributes. (See the OpenLDAP
documentation for more information.)

The list below summarizes information needed to configure OrangeFS for
LDAP identity mapping.

1.  Which users to enable for OrangeFS
2.  A user name to uniquely identify each user. For existing LDAP
    installations, this should correspond to the naming attribute of the
    existing user objects (often “CN” or “UID”).
3.  The naming attribute used for user objects in LDAP, often Common
    Name (CN) or UID.
4.  The DN of the LDAP container where user objects are stored. Users
    can be stored in one container or multiple containers.
5.  The names of the UID- and GID-storing attributes,
    usually uidNumber and gidNumber.

Planning for LDAP Binding
-------------------------

"Binding" means connecting and authenticating to an LDAP server. You
must have the following information to bind to your LDAP server:

-   URI(s) for the LDAP server(s). These URIs are in form
    “ldap[s]://hostname[:port]”. Using “ldap” specifies a plaintext
    connection, and “ldaps” specifies a secure (usually SSL) connection.
    The default port is 389 for plaintext, and 636 for secure. You can
    have multiple LDAP servers for the same directory—specify any of
    these.
-   DN of a binding user. The user must have sufficient rights to search
    for users in their specified container, and to read their uidNumber
    and gidNumber attributes. This value is optional, as anonymous binds
    are possible. The administrator must ensure that anonymous binds do
    not have excess rights.
-   Password of the binding user. This value can be stored in a
    protected file for additional security. This value is optional, as
    users are not required to have passwords.

Because the password is not encrypted, a user should be created for
OrangeFS usage with only the rights described above.

Server Configuration File Settings
----------------------------------

The LDAP settings are specified in the OrangeFS configuration file,
which is identical for each server. The \<LDAP\> tag within the
\<Security\> tag contains the settings:

\<Defaults\>\
     . . .\
      \<Security\>\
           . . .\
           \<LDAP\>\
                [Hosts {list of LDAP URIs}]\
                [BindDN {DN}]\
                [BindPassword {password} or {file:path}]\
                [SearchMode “CN” or “DN”]\
                [SearchRoot {DN}]\
                [SearchClass {Class name}]\
                [SearchAttr {Attrname}]\
                [SearchScope “onelevel” or “subtree”]\
                [UIDAttr {Attrname}]\
                [GIDAttr {Attrname}]\
                [SearchTimeout {timeout (secs)}]\
           \</LDAP\>\
      \</Security\>\
      . . .\
 \</Defaults\>

The settings are defined below.

  ------------------------------------------------------------------------------------------------------------------------------------------------------------ ----------------------------------------------------------------------------------------------
  ###### Setting {style="font-size: 10pt; font-family: Verdana, Arial, Helvetica, sans-serif;                                                                  ###### Default {style="font-size: 10pt; font-family: Verdana, Arial, Helvetica, sans-serif; 
                                               font-weight: bold; color: rgb(20, 50, 125);                                                                                                                  font-weight: bold; color: rgb(20, 50, 125); 
                                               margin-top: 0pt; margin-bottom: 0pt;"}                                                                                                                       margin-top: 0pt; margin-bottom: 0pt;"}

  Hosts: a list of LDAP URIs separated by spaces, for example “ldaps://myhost.org”.                                                                            “ldaps://localhost”.

  BindDN: an LDAP DN specifying the user that will connect to LDAP                                                                                             will bind anonymously

  BindPassword: the password for the binding user, or the string “file:” followed by a path to a file from which to read the password.                          no password

  SearchMode: “CN” or “DN”. See below for more information.                                                                                                    “CN”

  SearchRoot: the DN of the container with the user objects.                                                                                                   the root of the directory
                                                                                                                                                               
  **Note     **You must specify this value if you are using an OpenLDAP server.                                                                                

  SearchClass: the object class of the user objects.                                                                                                           “inetOrgPerson”

  SearchAttr: the naming attribute to match against the certificate CN.                                                                                        “CN”

  SearchScope: “onelevel” or “subtree”. Whether to search only the SearchRoot container (“onelevel”) or that container and all child containers (“subtree”).    “subtree”

  UIDAttr: the name of the UID-storing attribute.                                                                                                              “uidNumber”

  GIDAttr: the name of the GID-storing attribute.                                                                                                              “gidNumber”

  SearchTimeout: timeout in seconds for LDAP searches.                                                                                                          “15”
  ------------------------------------------------------------------------------------------------------------------------------------------------------------ ----------------------------------------------------------------------------------------------

You should have noted these values during “[Planning for LDAP
Binding](http://www.omnibond.com/orangefs/docs/v_2_9/admin_Certificate-Based_Security.htm#Planning_for_LDAP_Identity_Mapping)”
described above.

Searching LDAP for Identities
-----------------------------

The OrangeFS server searches LDAP for the user object based on the user
certificate’s subject DN.

If the SearchMode is “CN”, the CN (common name) of the certificate
subject is used. It must match an object meeting these criteria:

1.  It is in or under the SearchRoot container (depending
    on SearchMode).

2.  It has an object class equal to the SearchClass

3.  It has its SearchAttr attribute matching the certificate CN. The
    search filter used is:

(&(objectClass={SearchClass})({SearchAttr}={Certificate CN}))

The UID and GID will be retrieved from the UIDAttr
and GIDAttr attributes of the object. This UID and GID will be used for
subsequent file system operations. If this search fails, an error will
be printed to the server log and “operation not permitted” returned to
the client.

If the SearchMode is “DN”, the certificate subject DN must match the
LDAP user object DN exactly (case-insensitive). In this
mode, SearchRoot, SearchClass, SearchAttr and SearchScopeare not used.

OrangeFS will retry the connection if it can’t contact the LDAP server.
It will try different servers on the URI list.

### LDAP and System Identities

You can specify that an LDAP user object have a different UID/GID from
its corresponding system user. For example, the system user “jsmith” can
have UID/GID 500/100, but the LDAP user corresponding to “jsmith” might
have UID/GID 550/500. However, OrangeFS utilities will still show the
system login name associated with the OrangeFS UID/GID. In our example,
OrangeFS utilities display files as owned by system UID 550 rather than
“jsmith”. If you are using nsswitch (Name Service Switch) with LDAP you
will not have this conflict. Otherwise, it is not recommended that the
identities have mismatching UID/GIDs.

Creating a New LDAP Directory
-----------------------------

The examples/certs directory included in the distribution contains
scripts and files that can be used to create a new OpenLDAP directory.

The script pvfs2-ldap-create-dir.sh will create a new OpenLDAP directory
and add some basic objects. Usage of the script is:

./pvfs2-ldap-create-dir.sh [-p {prefix}] [-a {admin dn}] [-s {suffix
dn}] [-w {admin password}]

-   prefix: base directory for OpenLDAP installation, default /usr/local
-   admin dn: DN of LDAP administrator; should end with suffix DN,
    default cn=admin,{suffix dn}
-   suffix dn: base (topmost) DN of directory; default based
    on hostname, for example hostname acme.com would give dc=acme,dc=com
-   admin password: LDAP administrator password, default “ldappwd”.

The script will create the new LDAP directory and add two organizational
units, named "Users" and "Groups." A user object for the system root
account will be created with a random password. See “[Adding Users to
LDAP](Configuring_LDAP_For_Identity_Mapping.htm#Adding_Users_to_LDAP)”
below for information on changing the password.

**Important  **The directory created is not secure. User passwords are
stored in plaintext, and SSL/TLS security is not enabled.\
 The directory should only be used for testing, or as a starting point
for a secure directory. Consult the OpenLDAP documentation for
information on securing the directory.

These statements in the OrangeFS configuration file will configure this
directory.

\<Defaults\>\
     . . .\
      \<Security\>\
           . . .\
           \<LDAP\>\
                Hosts ldap://{hostname}\
                BindDN {admin dn}\
                BindPassword {admin password}\
                SearchRoot ou=Users,{suffix dn}\
                SearchScope onelevel\
           \</LDAP\>\
      \</Security\>\
      . . .\
 \</Defaults\>

Substitute the values in braces for the values used when creating the
LDAP directory. All unspecified values are equal to the defaults.

Adding Users to LDAP
--------------------

The ldapadd utility is used to add objects, including users, to an LDAP
directory. LDAP utilities use LDIF files to describe objects. Consult
the LDIF RFC
([http://www.ietf.org/rfc/rfc2849.txt](http://www.ietf.org/rfc/rfc2849.txt))
for more information on the LDIF file format.

In examples/certs, the script pvfs2-ldap-add-user.sh will create a user
based on the information for that user in /etc/passwd:

./pvfs2-ldap-add-user.sh [-D {admin dn}] [-w {admin pw}] {logon name}
{container dn}

The script will create a user with the CN equal to the logon name,
located in the specified container.
The uidNumber, gidNumber, displayName, homeDirectory and login shell
attributes will be set to correspond to the system account fields
(displayName corresponds to description). A random password will be
created.

To change a user password, the ldapmodify utility is used. A wrapper
script is provided in examples/certs:

./pvfs2-ldap-set-pass.sh [-D {admin dn}] [-w {admin pw}] {user dn}
{password}

For example:

./pvfs2-ldap-set-pass.sh -D cn=admin,dc=acme,dc=com -w ldappwd
cn=jsmith,ou=users,dc=acme,dc=com ‘sEcr3t!’

The script will store the password in LDAP in an encrypted format, using
the slappasswd utility.

 

 

 
