+++
title= "What to Expect"
weight=420
+++

When you complete the Enterprise installation process, if you have
provided all the necessary inputs, the last panel in the installer
offers the option to start the client. If you select this option, your
system mounts the OrangeFS server you specified, and the Windows Client
starts. If you are not ready to start the client, you can do it manually
later.

The installation program creates two new directories on your Windows
system:


| New Directory | Description |
|---|---|
| C:\\OrangeFS\\Client | OrangeFS client software, including the client executable (orangefs-client.exe), the orangefs-get-user-cert.exe app, and two configuration files (orangefs.cfg, orangefstab). |
| C:\\Program Files\\Dokan (enterprise installation)\ or C:\\Dokan (manual installation)  | The Dokan Library, an open source set of files used by the OrangeFS client to mount an OrangeFS file system as a virtual drive. |

The installation program adds a few settings to your Windows Registry.  These settings are automatically removed if you uninstall the client.

 

 

 

 

 

 

 
