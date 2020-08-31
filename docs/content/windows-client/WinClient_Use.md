+++
title= "Using the Client"
weight=460
+++

Information in this topic includes:

-   [Interfacing with OrangeFS](#Interfacing_with_OrangeFS)
-   [Running the Client](#Running_the_Client)
-   [Understanding Security](#Understanding_Security)
-   [Getting/Generating New User-Mapping
    Certificates](#Getting__or_Generating__New_Certificates)

Interfacing with OrangeFS
-------------------------

When the Windows Client is running on your computer, the OrangeFS file
system appears as a removable drive at the drive letter (E:-Z:). This
drive letter, specified during installation, is a setting in the
configuration file that can be changed. For more information, see
[Client Administration]({{<relref "WinClient_Admin.md">}}).

You can interact with files and directories in the file system like
local files. For example, they can be viewed in Windows Explorer, listed
in the Command Prompt or accessed using program API functions, such as
fopen.

**Note     **Currently the Client can mount only one OrangeFS file
system at a time.

Running the Client
------------------

You must start two Windows Services to run the OrangeFS Windows Client:

-   DokanMounter
-   OrangeFS Client

You can access these services in the Windows Services utility. To open
the Services utility, navigate to the **Control Panel** and click
**Administrative Tools** | **Services**. You should see the DokanMounter
and OrangeFS Client services included in the console listing.

To start (or stop) a service, right-click the service and select the
desired action.

You should start the DokanMounter service first. This service is tied to
the Dokan Library, which is the third-party software included with your
installation. DokanMounter enables the Windows Client to mount the file
system transparently.

The two services are configured to start automatically any time the
system is restarted. To change this setting, right-click the service and
select **Properties**.

**Note     **If you need to stop the Windows Client service, you do not
normally have to stop the DokanMounter service.

Understanding Security
----------------------

First you must set the Client to operate using the File System security
mode used by the servers. Do this by setting the “security-mode” option
in the configuration file to “default”, “key” or “certificate”, with
“default” being used if no option is specified. (For more information,
see [Client Administration](WinClient_Admin.htm).)

You can configure the file as read-only on Windows to remove owner write
permissions.

**Note     **The default permissions mask can be changed with the
new-file-perms and new-dir-perms configuration file keywords. Form more
information, see [Client Administration]({{<relref "WinClient_Admin.md">}}).

Level of security will also depend on the user mapping configuration of
your Windows Client. The three types of user mapping are

  ------------- ----------------------------------------------------------------------------------------------------------------------------------------------------------
  List          Directly matches one Windows ID with one OrangeFS UID and primary GID.
  Certificate   Maps user digital certificate to OrangeFS UID/GID. Our recommended configuration is for grid computing which requires CA, proxy and user certificates.
  LDAP          Maps user(s) on an LDAP tree, such as Active Directory or eDirectory, to OrangeFS UIDs/GIDs.
  Server        Used only when the security mode is “certificate,” this mode features client-side certificates for each user and server-side identity mapping with LDAP.
  ------------- ----------------------------------------------------------------------------------------------------------------------------------------------------------

For more information, see [Client Administration](WinClient_Admin.htm).

Getting (or Generating) New User-Mapping Certificates
-----------------------------------------------------

**Note     **This task only applies if your Windows Client is using
certificates mode for user mapping.

If your Windows Client is configured for certificate mapping, this will
likely involve three types of certificates (CA, proxy, user). Usually,
your administrator creates and installs these certificates. However,
since all certificates have expiration dates, you might need new ones
regenerated from time to time while using the Windows Client.

Depending on your setup, you might need to request new certificates from
your administrator, or the administrator might provide you with
instructions for doing it yourself.

Of the three types of certificates mentioned earlier, the proxy
certificate must generally be renewed more often than the other two.
Depending on your administrative policies, the time before a proxy
certificate expires can average anywhere from 6 hours to two weeks.

 

 

 

 

 
