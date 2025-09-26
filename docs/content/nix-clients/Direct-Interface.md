+++
title= "Linux Direct Interface"
weight=330
+++

The Direct Interface allows you to access OrangeFS in a cluster similar
to Linux (POSIX-based); however, the Direct Interface (also known by
usrint, the system folder in which it is stored) bypasses the Linux
Kernel for a more direct and better performing path to OrangeFS. It
provides high performance access for programs that are not written for
MPI.

{{<notice info>}}The OrangeFS Direct Interface using Global Configuration
will work only on systems configured with shared C libraries.{{</notice>}}

The Direct Interface is included with the OrangeFS standard
installation, accessed  by copying appropriate files to a client
location and activating it with configuration statements.

This topic is organized into two sections:

-   [Understanding the Interface Levels]({{<relref "#understanding-the-interface-levels">}})
-   [Configuring the Direct Interface]({{<relref "#configuring-the-direct-interface">}})

<!-- TODO: add System Calls page?
**Note     **To learn about System Calls for the Direct Interface, see
[System Calls](System_Calls.htm) in the Administration Guide.
-->

Understanding the Interface Levels
----------------------------------

The Direct Interface offers three levels of access, so you must
configure your access based on the level that works best for your needs.
<!-- TODO: find and add this image
The following illustration shows the three interface levels.

![](ofs_usrint_parts.png)
-->

- [Level 1: System Call Library]({{<relref "#level-1-system-call-library">}})
- [Level 2: POSIX Library]({{<relref "#level-2-posix-library">}})
- [Level 3: C Library]({{<relref "#level-3-c-library">}})

#### Level 1: System Call Library

The first and lowest level is an API with OrangeFS-specific functions that can
be substituted for each of the basic POSIX defined I/O related system calls.
Essentially, each POSIX system call is replicated in the API. What makes this
API different is that each function ONLY works with files in the OrangeFS file
systems.

#### Level 2: POSIX Library

The next layer is a POSIX system call interposition library. Each of the same
POSIX system calls represented in the lower layer are provided in this API,
this time with the same interface syntax as Linux POSIX. Rather than calling
the Linux kernel directly, each call is checked to see if it refers to an
OrangeFS file, and if so the call is made to the corresponding function in the
lower level API. Thus a call to open() will call pvfs\_open() if the path
refers to an OrangeFS file; otherwise it will call the Linux open system call.
This API is more convenient, though slightly less efficient, than the lower level one.

#### Level 3: C Library

Finally, many programmers prefer to use the C library interface rather than the
system call interface to file I/O, in part because it provides I/O buffering
and a richer set of interface options. Any C calls are implemented using the
POSIX calls, and so their implementation can, in theory, be linked from the C
library, and use the OrangeFS POSIX interposition API.

Virtually all modern Linux systems use shared libraries for the C library.
Shared libraries tend to link all of the various functions at various levels
into a single shared object that is loaded dynamically. Thus, if you call
fopen() using the standard shared C library, there is no means to get that
function to call the OrangeFS pvfs\_open() function. For this reason, OrangeFS
provides its own implementation of these functions in an OrangeFS C Library
interposition API. These functions are identical to those in the standard C
library implementation, except that they call the OrangeFS functions, and, in
some cases, can be optimized for specific OrangeFS features.

Configuring the Direct Interface
--------------------------------

This section explains two methods for configuring the Direct Interface.

[Program Configuration]({{<relref "#program-configuration">}}):  Use this
method to specify an individual program to run through the OrangeFS Direct
Interface.  
[Global Configuration]({{<relref "#global-configuration">}}):  Use this method
to specify that all programs will run through the OrangeFS Direct Interface.


### Program Configuration

Programs, and higher level libraries, written to any of the three
library levels included in the Direct Interface should link to the
appropriate OrangeFS replacement library (liborangefsposix or
liborangefs) to directly access the OrangeFS file system. The command
for this configuration also determines whether to use a shared or static
version of the library.

To link a program with the replacement library, include the following
command when compiling the program:

<!-- backticks instead of "code" shortcode in order to use <> without having
to use html entities -->
```
gcc -o <program> <program_source> -L<orangefs_lib_path> <rep_lib>
```

where...

*program* = the name of your program, including the path

*program\_source* = the name of your program source code, including the
path

*orangefs\_lib\_path* = path to lib directory in the OrangeFS
installation directory

*rep\_lib* = one of the following options:

| If your program is written to: | Enter this option: | To use this replacement library: |
| --- | --- | --- |
| C Library or POSIX Library | -lorangefsposix | liborangefsposix |
| OrangeFS System Call Library | -lorangefs | liborangefs |

Example command line:

```
gcc -o /programs/foo /programs/foo.c -L/opt/orangefs/lib -lorangefsposix
```
 

### Global Configuration

Programs not specifically recompiled to use OrangeFS can still be
redirected to do so by preloading the shared version of the appropriate
OrangeFS replacement library (libofs and/or libpvfs2). You must
configure the source to build the shared library before compiling
OrangeFS.

Assuming the shared libraries are installed, set the following
environment variables:

```
export OFS_LIB_PATH=<orangefs_lib_path>
export LD_LIBRARY_PATH=$OFS_LIB_PATH:$LD_LIBRARY_PATH
export LD_PRELOAD=$OFS_LIB_PATH/<rep_shared_library>
```

where...

*orangefs\_lib\_path* = path to lib directory (in the OrangeFS
installation directory)

Example: /opt/orangefs/lib

*rep\_shared\_lib* = one of the following replacement library files:

| To redirect programs written to: | Use these replacement library files: |
| --- | --- |
| C Library or POSIX Library | libofs.so and libpvfs2.so |
| OrangeFS System Call Library | libpvfs2.so |

Example: LD\_PRELOAD=\$OFS\_LIB\_PATH/libpvfs2.so

{{<notice info>}}The global configuration method does not work if you use the
static version of libc.{{</notice>}}

 Ensure that your system's /etc/ld.so.preload includes libdl, libssl,
libcrypto and libpthreads preloaded through /etc/ld.so.preload. Most
Linux systems will already include this.\
 \
 If this configuration method is used in the shell, every program
(including such commands as ls, vi and cp) will redirect through the
OrangeFS libraries. You can set these variables in a script to affect
only the desired commands.\
 \
 If all users on a system want the shared libraries preloaded, the
system administrator can edit the file /etc/ld.so.preload and list the
libraries there.

 

 

 

 

 

 
