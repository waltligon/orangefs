
+++
title= "Preparing for Windows Installation"
weight=410
+++

*Important* Please read this section before beginning installation of the Windows Client.

### Client Requirements

Operating systems:

-   Windows Vista
-   Windows 7
-   Windows 8
-   Windows Server 2008 or Windows Server 2008 R2 (all editions; Server Core installation not currently supported)
-   Windows Server 2012 (all editions; Server Core installation not currently supported)

 

Hardware:

-   30MB disk space
-   Other requirements dependent on usage; minimum requirements very low

 

Other:

-   You must assign a drive letter during installation.
-   It is best to run the installer as an administrative user (Administrator, for example).

 

### OrangeFS Requirements

To connect to the OrangeFS server during installation, you must specify
its URI. You must know the host name and port number (default is 3334).
The format for this entry is provided later in the instructions.

*Important*  The OrangeFS installation accessed by the Windows Client
must be configured for TCP network protocol.

### File System Security Mode

An OrangeFS file system operates in one of three security modes:
default, key-based or certificate-based. All servers and clients must
operate in the chosen mode. The “security-mode” option of the Windows
Client configuration file (see [Client Administration]({{<relref "WinClient_Admin">}})) should be set to select the correct mode.
<!-- TODO: does this page exist?
See [Preview Security](Preview_Security.htm) for more information.
-->

### Authentication Configuration

During installation you have four mode options for user mapping:

-   list
-   certificate
-   ldap

 

The following table summarizes these options:



|User Mode | Option |  Description Best For... | Installation Input | More to do after installation? |
|---|---|---|---|---|
| list | Directly matches one Windows ID with one OrangeFS UID and primary GID. | Simple, smaller installations, trial runs, etc. | Enter one Windows user ID and the OrangeFS (Linux/UNIX-based) UID and primary GID for mapping. | All but first user must be entered manually in the orangefs.cfg file after installation. |
|certificate | Maps user digital certificate to OrangeFS UID/GID. Our recommended setup is for grid computing, which requires CA, proxy and user certificates. <br> <br> **Important  **You must install and configure your certificates before installation. A recommended method for doing this is included in these instructions. | Scientific, large cluster, research, etc. | Specify the Windows prefix directory of your user and proxy certificates. This might be either the user's profile directory or a custom directory, which you must enter: c:\\users\\  or  *cert-dir-prefix*   |  If your certificates were properly installed and configured before installation, nothing else should be required. |
| ldap | Maps user(s) on an LDAP tree to OrangeFS UIDs/GIDs. | Windows with Active Directory or eDirectory. | LDAP inputs, acount to sign in, etc. | If all inputs are entered, nothing else should be required. 
| server | This mode is only used with certificate security mode (see above). Identity information is stored for each user in a client-side certificate. Then the server, rather than the client, maps this information to an OrangeFS identity using LDAP. | Installations that require per-user security, particularly those that use LDAP for user information. | Non (configured post-installation) | Run orangefs-get-user-cert for each user (see [Using the orangefs-get-user-cert App]({{<relref "WinClient_Admin#using-the-orangefs-get-user-cert-app">}})) |

For more information on user mapping, see [Client Administration]({{<relref "WinClient_Admin">}}).

 

 

 

 

 

 

 

 

 
