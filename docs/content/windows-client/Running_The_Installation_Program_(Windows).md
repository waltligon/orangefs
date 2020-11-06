+++
title= "Installing the Software"
weight=430
+++

To install the OrangeFS Windows Client, you need the self-extracting
installation program. Two versions are available, depending on your
system’s processor type (32-bit or 64-bit).

Download and run orangefs-client-*version*-win32.exe or
orangefs-client-*version*-win64.exe

where...

*version* = version number of the executable

Example: orangefs-client-2.8.5.3-win64.exe

*Notes* OS. It is best to run the installer as an administrative user
(Administrator, for example).

After running the executable, follow these steps:

- When the installation program's **Welcome** dialog displays, click
    Next. <br> <br> The next dialog prompts you for an installation location. <br> <br>
- Use the default or select a different location and click Next.
- Click Install to install the client. <br> <br> The next dialog prompts you for a file system URI, a mount point and user mapping mode. <br> <br>
- Complete the dialog as follows:

| | |
|---|---|  
| File System URI  | Enter the DNS name/IP address and port number of an OrangeFS file system server in a URI format: tcp://[hostname]:[port]/[FS\_name] <br> <br> Example: tcp://server1.com:3334/orangefs <br> <br> The default port number is 3334. |
| Mount Point | Click the drop-down button to select a drive letter (E: to Z:) for the file system. Select Auto to use the first available drive letter (starting with E:). |
| User Mapping | select **List**, **Certificate**, or **LDAP**. This corresponds with the mode of user mapping (described in the next step). If you are not sure, select List, as the settings can be changed later.  <br> <br> **Important** The certificates must first be generated and placed in appropriate locations to support the Windows client before you can select the certificate mode. If you still need to complete this process, close the installation program and restart it when you have completed the certificate generation. |
Click Next to continue.

- Depending on the mode of user mapping you chose in step 5, a dialog
    with one of the following titles prompts you for more information:

| | |
|---|---|  
| List Map Add User | If you selected list mode, enter one Windows user ID and the OrangeFS (Linux/UNIX-based) UID and primary GID for mapping. You will manually add additional users to the configuration file after the installation. |
| Certificate User Mapping | If you selected certificate mode, enter the Windows prefix directory of your user and proxy certificates. The default is the user profile directory (C:\\Users). You can also enter another location, for the prefix directory. |
| Setup Type | If you selected ldap mode, a dialog provides three choices for your LDAP implementation (**Microsoft Active Directory**, **Novell eDirectory** or **Custom**). Select one and click **Next**. <br> <br> In the next dialog, enter the LDAP values required by OrangeFS. <br> <br>  **Note**     Depending on your LDAP selection, some of the text fields that display in the dialog might already have entries. |
For complete details on user mapping see [ClientAdministration](({{<relref "WinClient_Admin.md">}})).

- Click Next to continue.

The final dialog displays with an option to start the OrangeFS service
when you exit the installation program.\

- Do one of the following:

  - Select the check box for **Start the OrangeFS services** if you want
the Windows Client to mount the OrangeFS file system.

  - Leave the check box deselected if your configuration is not complete.
You can manually start the service later.

- Click Finish to complete the installation.

 

 

 

 

 

 

 

 

 

 

 
