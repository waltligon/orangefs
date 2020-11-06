+++
title= "OrangeFS Clients"
weight=300
+++

Client access to OrangeFS is flexible, with support for a variety of
operating environments and interfaces.

Depending on the Client you wish to select, your Client system might
run Linux, Windows, MacOS X or even Apache (web-based), as shown here:

| Client Interface | Client System (Operating Environment) |
|---|---|
| Kernel Module | Linux |
| Direct Interface | Linux (Kernel bypass) |
| FUSE | MacOS X and Linux |
| ROMIO (MPI-IO) | Linux |
| Windows Client | Windows |
| Apache WebDAV / S3 | Multiplatform Web Access |


Generally, the requirements for these client solutions can be addressed
separately, as their instructions assume the file system servers are
already installed and running.

This topic further summarizes client options in two parts:

-   Client Matrix
-   Client Architecture Diagram

Client Matrix
--------------

| Client | Description | Typical Uses | Advantages |
|---|---|---|---|
| [Linux Kernel Module](Add_Linux_Client(s).htm)  | Enables access to OrangeFS through the native Linux operating environment.              | For Linux users who wish to access OrangeFS as a mounted file system, using standard tools like ls, cp and rm. | Supports standard out-of-the-box Linux kernels.  |
| [Linux Direct Interface](Direct_Interface.htm)  | Provides program libraries that allow developers to call standard functions (open, close, read, write, etc.) that communicate with OrangeFS servers directly, bypassing the Linux kernel.   | For advanced users who can benefit from higher access speeds or targeted programming access to OrangeFS. | Bypasses the Linux Kernel for improved performance. Interoperability between application programs.  Provides three levels of access in varying performance versus ease-of-use combinations. |
| [ROMIO (MPI-IO)](ROMIO_Interface.htm)          | ROMIO is an implementation of the MPI-IO protocol that includes support for OrangeFS.   |   Access to OrangeFS in programs and operations optimized for parallel computing.                         | Any MPI Library implementation that works with ROMIO (such as MPICH and OpenMPI) can also work with OrangeFS. | |
| [FUSE](FUSE_Client.htm) targeted for MacFuse   | Allows access to OrangeFS file systems through FUSE (Filesystem in Userspace), which provides its own kernel module/driver to mount a file system.                                            | For FUSE interface users who want to use OrangeFS for their file system storage.                         | Adds performance advantages of OrangeFS to a MacFUSE front end. |
| [Windows Client](WinClient_Intro.htm)          | Enables access to OrangeFS through Microsoft Windows environment.                       |    Native, transparent access to OrangeFS from Windows.   |  Parallel protocol perforance directly from Windows. |
| [Hadoop Client](Hadoop_JNI_Client.htm)         | Enables MapReduce, the processing engine for Hadoop, to replace its standard file system (HDFS) with OrangeFS. | For Hadoop-based, data-intensive, distributed applications. | Can improve MapReduce performance and provides more ways to leverage data with the OrangeFS feature set. |
| [WebDAV Apache Module](Web_Pack_Intro.htm)     | Allows any WebDAV client access to OrangeFS via an Apache server.  | Access OrangeFS data via HTTP. | Native WebDAV access to OrangeFS. |
| [S3 Apache Module](Web_Pack_Intro.htm)         | Allows any S3 client access to OrangeFS via an Apache server. | For using the S3 file access protocol. | Numerous client tools already exist. |

Client Architecture Diagram
---------------------------

The following diagram depicts the primary OrangeFS components that
enable each client interface to connect to the file system.

{{<figure src="./images/ofs_client_interfaces_2_9.png" alt="OrangeFS Clients Diagram">}}


 

 

 

 

 

 

 

 
