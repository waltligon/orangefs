+++
title= "Uninstalling the Windows client"
weight=450
+++

Uninstalling the Enterprise Installation
----------------------------------------

To uninstall the Windows client, follow these steps:

1.  From the Windows Start Menu, select Control Panel, then Programs and
    Features.
2.  Locate and select the OrangeFS Client item, and click the Uninstall
    button above.
3.  Follow the uninstaller steps to remove the Client.
4.  Remove configuration files under C:\\OrangeFS\\Client (by default)
    and the C:\\OrangeFS\\Client directories.

Uninstalling a Manual Installation
----------------------------------

Follow the instructions below to uninstall the OrangeFS Windows Client:

Stop the Dokan Mounter and OrangeFS Client services.

Remove the OrangeFS Client service:

-   Change directory (cd) to the OrangeFS\\Client directory.
-   Remove the service using orangefs-client.exe:\
     orangefs-client -removeService

Remove the Dokan Mounter service:   

-   Change directory (cd) to the Dokan\\DokanLibrary directory.
-   Remove the service using dokanctl.exe:\
     dokanctl /r s

Remove the Dokan driver:

-   Change directory (cd) to the Dokan\\DokanLibrary directory.
-   Remove the driver using dokanctl.exe:\
     dokanctl /r d
-   Restart your system

Remove Dokan system files:

-   Remove dokan.dll:\
     del c:\\windows\\system32\\dokan.dll
-   Remove dokan.sys:\
     del c:\\windows\\system32\\drivers\\dokan.sys

Remove application files:

-   Remove the Dokan directory:\
     rd Dokan /s
-   Remove the OrangeFS directory:\
     rd OrangeFS /s

 

 

 

 

 

 

 

 

 

 
