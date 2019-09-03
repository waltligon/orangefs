+++
title= "Manual Installation"
weight=440
+++

**Important**  The Client connects to a running OrangeFS server. If you
have not yet installed the OrangeFS server components, consult the
documentation and [install the server](Add_Server(s).htm) before
installing the Windows Client.

Follow the instructions below to install the OrangeFS Windows Client:

Download the ZIP file associated with your system type (64- or 32-bit):

1.  For 64-bit systems, download
    orangefs-windows-client-*version\#*-win64.zip.
2.  For 32-bit systems, download
    orangefs-windows-client-*version\#*-win32.zip.\
     where...\
          *version\#* is the OrangeFS version, for example, 2.8.\*.

Extract the ZIP file to any directory, where the OrangeFS and Dokan
directories will be created.

Open a Command Prompt from **Start** | **All Programs** |
**Accessories** to complete the following steps.

#### Install the Dokan driver:

1.  Change directory (cd) to the Dokan\\DokanLibrary directory.

2.  Copy dokan.dll to the System32 directory: <br> copy dokan.dll c:\\windows\\system32

3.  Copy dokan.sys to the system Drivers directory: <br> copy dokan.sys c:\\windows\\system32\\drivers

4.  Install the driver using dokanctl.exe: <br> dokanctl /i d

5.  Restart your system.

#### Install the Dokan Mounter service:

1.  Change directory (cd) to the Dokan\\DokanLibrary directory.
2.  Install the service using dokanctl.exe: <br> dokanctl /i s

#### Install the OrangeFS Client service:

1.  Change directory (cd) to the OrangeFS\\Client directory.
2.  Install the service using orangefs-client.exe: <br> orangefs-client -installService

3.  Configure the OrangeFS Client by creating the orangefstab and orangefs.cfg files in OrangeFS\\Client, following the instructions in the [Configuration]({{<relref "WinClient_Admin.md">}}) section of the Windows Client documentation.
4.  Start the Dokan Mounter and OrangeFS Client services using the Services Administrative Tool (**Start** | **Control Panel** **Administrative Tools** | **Services**).

Your OrangeFS file system should appear as a Removable Drive.

For troubleshooting, open the Event Log Administrative Tool and consult
the Application Log. For additional help, consult the documentation.

 

 

 

 

 

 

 

 

 
