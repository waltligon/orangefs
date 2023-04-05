## OrangeFS Client 2.10.0 - Documentation




### Windows Client Requirements




#### OS Versions






* Windows 10
* Windows 11
* Windows Server 2019 (all Editions)
* Windows Server 2022 (all Editions)




#### Hardware Requirements






* X64 Processor (ARM not supported)
* 4GB RAM
* 100 MB disk space (minimum 10GB recommended)




#### Access Requirements






* Administrator access required for:
        * Installation
        * Starting and stopping the Client
        * Configuring the Client




### License Information




#### Third-Party Software Libraries






* [Dokany 1.5.1](https://github.com/dokan-dev/dokany)
* [Microsoft Visual C++ CRT](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170) (C Run-time Library)
* [OpenSSL](https://openssl.org)




### Installation (Executable Installer)




#### Installation Procedure






1. Download orangefs-client-win64-2.10.0.exe from [https://orangefs.org](https://orangefs.org) to any directory.
2. Run the installer, clicking Yes if asked if you want to allow the installer to make changes to your system.
3. Third-Party Software (if applicable): click Yes / Continue to install the Microsoft Visual C++ Runtime Libraries and dokany.  \
_Note_: a reboot may be required–after logging into the account used above, the installer will resume automatically.
4. Click Next to begin the installation.
5. Select a destination folder for the Client.
6. Click Install to perform the installation.
7. Enter the configuration information as noted.
        1. Windows Userid: It’s a good idea to specify the Windows administrative user (default userid: Administrator). More accounts can be added later.
        2. UID / GID: specifying 0 for both fields sets up the specified user on Windows to operate as root on Linux.
8. Click Finish to finalize the installation. You may start the OrangeFS Client immediately.




#### Verification






1. Open File Explorer and select “This PC”.
2. Verify that the Drive with the letter (e.g. “E:”) selected for OrangeFS is present. If not, refresh (F5) the view several times to see if it appears. If it fails to appear, see the Troubleshooting section below.
3. Navigate into the Drive and experiment with creating, view and deleting files.




### Installation (WSL: Windows Subsystem for Linux)


A development and test environment for the OrangeFS Windows Client can be implemented on a Windows system using Windows Subsystem for Linux (WSL). WSL runs one or more virtualized Linux systems on a Windows PC. Below are general steps for implementing such an environment:






1. Install WSL from the Microsoft Store, selecting your desired distribution (e.g. Ubuntu). (It can also be installed from Apps & Features, but installing from the Store allows the WSL to be updated more easily.)
2. Download the OrangeFS source code tarball.
3. Run WSL from the Start Menu to open a terminal window.
4. Set up a stable IP address by adding these lines to `/etc/wsl.conf`, creating that file if necessary: \
`[network] \
generateResolvConf = true \
`Then restart WSL. The address can be viewed on Linux using the `ip addr` command.
5. Build the OrangeFS server:
        1. cd to the directory containing the source tarball. Windows drives are available under `/mnt`, e.g. `/mnt/c` for C: drive.
        2. Extract, build and install the OrangeFS Server, following the Linux instructions. Note that you don’t need to build the kernel module.
6. Open a firewall port between Windows and WSL using this PowerShell command: \
`New-NetFirewallRule -DisplayName "WSL" -Direction Inbound -InterfaceAlias "vEthernet (WSL)" -Action Allow`
7. Install the Windows Client to the same system, using the IP address from step 4. OrangeFS files will be visible via the drive (letter) specified in the orangefs.cfg file.




### Using the OrangeFS Windows Client


When the Client is active and connected to an OrangeFS file system, the file system’s root directory will be mounted as a removable Drive on the Windows system. Drives on Windows are denoted by a capital letter followed by a colon–”`C:`” represents the primary hard drive. Directories are separated by a backslash, though in many programming languages, a forward slash can be used interchangeably with the backslash. Therefore, `C:\` represents the root directory of the primary hard drive,` C:\Windows` is the Windows directory, and so on.


The Client can be configured to mount as a specific Drive letter, or simply to use the first available Drive letter. (See Administration - Configuration below).


Once mounted, the OrangeFS file system can be used just as if its files were on any other removable drive. The file system can be navigated using File Explorer, the GUI application included with Windows; files can be interacted with using a command line interface such as Windows PowerShell; and the files can be opened with custom applications using standard functions like `fopen()` in C.


To enforce file system permissions, the Client maps Windows user names to UID/(primary) GID on the Linux system. The mappings are specified in `orangefs.cfg` (see below). If a user on Windows attempts to access a directory or file they don’t have permission to access, an “Access Denied” message is displayed.




### Administration




#### Starting and Stopping the Client


The Client runs as a Windows Service—a background process with no GUI component. To start and stop the Client, open the Services Administrative Tool from the Start Menu and locate “OrangeFS Client” in the list. Select that entry and click Start or Stop as desired. The service is set to start automatically upon system startup by default–this behavior can be changed in the service’s Properties window.




#### Configuring the Client


As mentioned under Installation above, the Client directory (default C:\OrangeFS\Client) contains two text files used for configuration: `orangefstab` and `orangefs.cfg`.


_Note_: the default Windows text editor, Notepad, will usually append `.txt` to a text file’s file name. So, if the `orangefstab` file name is specified, the result may be `orangefstab.txt`. This can be fixed by changing the name of `orangefstab.txt` to `orangefstab` in File Explorer.


Any changes to the configuration files requires a restart of the Client service (see above).




##### orangefstab format


The orangefstab file uses the same format as Linux/UNIX mtab (mounted file system table) files. Here is a sample line entry in orangefstab:




```
        tcp://orangefs.acme.com:3334/orangefs /mnt/orangefs pvfs2 defaults,noauto 0 0


```






* Since only one file system can be mounted, only one line can be used.
* The first field is a URI that specifies an OrangeFS file system server. The format is: \
<code>tcp://<em>hostname</em>:<em>port</em>/fs_name \
</code>Where… \
<em>hostname</em> = OrangeFS server host name \
<em>port</em> = port number \
<em>fs_name</em> = OrangeFS file system name


TCP is the only protocol supported on Windows. The default port is 3334. The file system name can be determined from the server configuration file (default is orangefs).


The second field is the internal Linux-style mount point. This value should be the same for all clients (Windows or Linux).


The other fields should be left as-is above.




##### orangefs.cfg format


Most of the Windows Client configuration information is contained in orangefs.cfg, a text file that contains lines in the form:




```
        keyword "option_value"
```




with the double quotes only needed if spaces are present in the option value.


You can specify comments using the # character:




```
        # This is a comment.
```






###### Keyword: mount


The first essential keyword is `mount`. It specifies the drive letter associated with the mounted OrangeFS server.


Example:




```
        mount O:
```




This example will mount the file system on O: drive. (You must include the colon.) If you do not use the mount keyword, the first available drive alphabetically, starting with E:, is used by default.




###### Keywords: user


Mapping Windows users to Linux users, represented by their UID and primary GID, is done with the `user` keyword:




```
        user Administrator 0:0
```




where “Administrator” is the Windows user name, and the next two numbers represent the UID and GID of the corresponding Linux user, respectively. Note that a person’s name may be used as the administrative user on the home Windows operating systems–use quotes if spaces are present:




```
        user "John Doe" 0:0
```




**Important**: due to security library changes, the certificate and LDAP user-mapping modes have been removed from the version 2.10.0 release.




###### Keywords: new-file-perms, new-dir-perms


The `new-file-perms` and `new-dir-perms` keywords change the initial permissions mask of newly created files and directories. If these keywords are not present, the default permissions mask is 755 (rwxr-xr-x).


_Note_: For more information about the permissions mask, see the Linux `chmod` man page.


The keywords are used with an octal integer value representing the permissions mask.


Examples:




```
        new-file-perms 644
        new-dir-perms 700
```




The first example will cause new files to be created with “rw-r–r–“ permissions.


The second will create directories with “rwx——“ permissions.


_Note_: While you can set the “sticky bit” in OrangeFS, it has no effect.


**Important**: Ensure that the file owners always have read permissions to their own files (mask 400), and read and execute permissions to their own directories (mask 500). Otherwise, they cannot read these files and directories after creation.




###### Keywords: debug, debug-file, debug-stderr


The `debug`,` debug-file` and `debug-stderr` keywords log detailed debugging information. If you specify the debug keyword by itself, client-related messages are recorded in `orangefs.log` in the installation directory (`C:\OrangeFS\Client `by default). You can change the name and location of the log file by using the debug-file keyword.


Example:




```
        debug-file C:\Temp\myfile.log
```




You can also use any of the debugging flags available with OrangeFS. For a list of these flags, see the OrangeFS system documentation. The client flag is win_client.


Example:




```
        debug win_client io msgpair
```




In this example, you would log debugging information about client, I/O and message pair operations.


The `debug-stderr` keyword is used with no option value and prints debugging messages to the console. This keyword is useful only if orangefs-client.exe is running as a normal executable (not as a service).




#### Troubleshooting




##### Log Information


Client logs are generated in three ways:






1. The Windows Application and System Logs, which are viewed using Event Viewer from Windows Administrative Tools (on the Start Menu).
2. When run as a service, the Client writes messages about service operation to a text file in the OrangeFS Client directory called service.log.
3. If configured (see below), detailed debug log information will be stored in a file specified in orangefs.cfg. \
_Note_: It may not be possible to open the log file while the Client is running due to file-locking. To view its contents, copy the file and open the copy.




##### Common Problems


The Client uses two configuration files, `orangefstab` and `orangefs.cfg`. `orangefstab` is a single line that primarily specifies the network location of an OrangeFS server. `orangefs.cfg` is a multi-line file with multiple options that override defaults._ \
Note_: any changes to `orangefs.cfg` or `orangefstab` will require the OrangeFS Client service to be restarted in Services Administrative Tools.






* **The OrangeFS drive does not appear in Explorer**: \
View logs to get more information. \
Check orangefs.cfg to determine whether user information is correct.
* **The OrangeFS drive appears, but files can’t be viewed**: \
Check that the OrangeFS server is running. \
Check that the hostname or IP address and port specified in the `orangefstab` file are correct. \
Check network connectivity between the server system and client system, considering firewall and other security settings.
* **Clicking on the OrangeFS drive results in “Access Denied”**: \
Check the configuration file to determine whether user information is correct. \
Check the rights of the specified server user on the server system. (The server user is specified by UID and GID in `orangefs.cfg`.)