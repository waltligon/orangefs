+++
title= "Linux ROMIO MPI Interface"
weight=340
+++

ROMIO is a particular implementation of the MPI-IO protocol, the open
standard for data transfer to and from MPI.

MPI, also an open standard, was created for researchers who needed a
*message-passing interface* optimized for high performance parallel
computing.

Different working implementations for MPI, also called MPI libraries,
exist. Two popular MPI libraries are MPICH from Argonne National
Laboratory and Open MPI from a consortium of users including Oak Ridge
National Laboratory.

Different implementations of MPI-IO also exist. The ROMIO implementation
includes support for OrangeFS. Therefore, any MPI library implementation
that works with ROMIO, *such as MPICH and Open MPI*, can also work with
OrangeFS.

Setting up a ROMIO client involves one step, with options for
configuring Open MPI or MPICH:

-   [Configuring for Linux](ROMIO_Interface.htm#Configuring_for_Linux)

-   [Install OrangeFS 2.9 with MPICH
    3.0.4](ROMIO_Interface.htm#Install_OrangeFS_2.9_with_MPICH_3.0.4)

-   [Install OrangeFS 2.9 with Open MPI
    1.6.5](ROMIO_Interface.htm#Install_OrangeFS_2.9_with_Open_MPI_1.6.5)

Configuring for Linux
---------------------

Both MPICH and Open MPI are packaged with ROMIO. Configuring either of
these MPI implementations to access OrangeFS involves two areas:

-   Adding OrangeFS installation files

-   Linking programs to OrangeFS

#### Adding OrangeFS Installation Files

To add the OrangeFS installation files to the MPI client system, Change
Directory (cd) to /opt on the client and copy the /opt/orangefs
directory from the build system:

scp -r hostname:/opt/orangefs /opt

where...

*hostname* = host name of the build system

#### Linking Programs to OrangeFS

When you run your Open MPI or MPICH applications, link to OrangeFS by
including the -lpvfs2 option.

For example, to run a program called mytest, you would follow these
steps:

1.  Compile and link the program to the pvfs2 library in your OrangeFS
    installation:

cc mytest.c -o mytest -L /opt/orangefs/lib -L
/*mpich2\_install\_dir*/lib -I /opt/orangefs/include -I
/*mpich2\_install\_dir*/include  -lpvfs2 -lmpich

where...

*mpich2\_install\_dir* = the name and path of your MPICH installation
directory

Other Operating Environments
----------------------------

ROMIO can also run on Windows and Mac. Those platforms are less
efficient for the high performance parallel computing that most ROMIO
users seek in OrangeFS, so the above instructions focus on Linux client
implementations only.

-   To connect to OrangeFS from a Windows environment, consider using
    the [Windows Client](WinClient_Intro.htm) developed specifically for
    OrangeFS.
-   To connect to OrangeFS from a Mac environment, consider using
    [FUSE](FUSE_Client.htm).

Install OrangeFS 2.9 with MPICH 3.0.4
-------------------------------------

**Notes   **You must install OrangeFS on your storage nodes and the
OrangeFS system must be online prior to performing these steps. If you
have not completed this step, see the [Installation
Guide](Installation_Guide.htm) for instructions to complete this step
before proceeding.\
 \
 For instructions on how to use MPICH, see *[MPICH User's
Guide](http://www.mpich.org/static/downloads/3.0.4/mpich-3.0.4-userguide.pdf).*

### Secure Shell (SSH)—without Passphrase

You must configure all clients to support secure shell connections via
SSH without passing a passphrase. For more information, see [Generating
SSH Keys for Passwordless
Login](http://hortonworks.com/kb/generating-ssh-keys-for-passwordless-login/),
an article from the Hortonworks Knowledgebase.

Prior to configuring MPICH, ensure that you have built shared libraries
for OrangeFS:

1.  Run the same ./configure command you used when you installed
    OrangeFS, but add the following additional option:

--enable-shared

To configure OrangeFS to work with MPICH 3.0.4, complete the following
steps:

1.  Run the following commands to remove references to methods included
    in MPICH that will cause errors during the ./configure stage.

**Note     **You will not be able to use the MPI/IO functions
IReadContig and IWriteContig.

sed -i s/ADIOI\_PVFS2\_IReadContig/NULL/  \
 src/mpi/romio/adio/ad\_pvfs2/ad\_pvfs2.c\
 sed -i s/ADIOI\_PVFS2\_IWriteContig/NULL/\
 src/mpi/romio/adio/ad\_pvfs2/ad\_pvfs2.c

2.  Compile MPICH with --enable-shared option:

./configure --prefix=/opt/mpich-3.0.4 --enable-romio --enable-shared
--with-pvfs2=/opt/orangefs --with-file-system=pvfs2

where...

*/opt/orangefs* = the location of your OrangeFS installation

*/opt/mpich-3.0.4* = the location of your MPICH installation

**Note     **You can remove the *--prefix* command to install
to*/usr/local*

3.  Make and install freshly compiled MPICH 3.0.4 with OrangeFS Support

·sudo make all install

4.  Set LD\_LIBRARY\_PATH to point to the MPICH libs

export LD\_LIBRARY\_PATH=/*opt/mpich-3.0.4*/lib:\$LD\_LIBRARY\_PATH

where...

*opt/mpich-3.0.4* = the location of your MPICH installation

Install OrangeFS 2.9 with Open MPI 1.6.5
----------------------------------------

**Notes   **You must install OrangeFS on your storage nodes and the
OrangeFS system must be online prior to performing these steps. If you
have not completed this step, see the [Installation
Guide](Installation_Guide.htm) for instructions to complete this step
before proceeding.\
 \
 OrangeFS 2.9.0 has been successfully tested with OpenMPI 1.8.3 on
CentOS 7. No patches are necessary to run OrangeFS with OpenMPI 1.8.3.
If you are using this version, skip steps 1 and 2 below, and change the
OpenMPI version number in step 3.\
 \
 For instructions on how to use Open MPI, see *[Open MPI: Open Source
High Performance Computing](http://www.open-mpi.org/).*

### Secure Shell (SSH)—without Passphrase

You must configure all clients to support secure shell connections via
SSH without passing a passphrase. For more information, see [Generating
SSH Keys for Passwordless
Login](http://hortonworks.com/kb/generating-ssh-keys-for-passwordless-login/),
an article from the Hortonworks Knowledgebase.

Prior to configuring MPICH, ensure that you have built shared libraries
for OrangeFS:

1.  Run the same ./configure command you used when you installed
    OrangeFS, but add the following additional option:

--enable-shared

To configure OrangeFS to work with Open MPI 1.6.5, complete the
following steps.

1.  Patch the Open MPI 1.6.5 source to support OrangeFS: \
     Patch the Open MPI installation using the openmpi-1.6.5-romio.patch
    file. This patch is available on the OrangeFS

patch -p0 \< openmpi-1.6.5-romio.patch

2.  Run the following commands to remove references to methods included
    in Open MPI that will cause errors during the ./configure stage.

**Note     **You will not be able to use the MPI-IO functions
IReadContig and IWriteContig.

sed -e 's/ADIOI\_PVFS2\_IReadContig/NULL/' \\\
 -i ompi/mca/io/romio/romio/adio/ad\_pvfs2/ad\_pvfs2.c\
 sed -e 's/ADIOI\_PVFS2\_IWriteContig/NULL/' \\\
 -i ompi/mca/io/romio/romio/adio/ad\_pvfs2/ad\_pvfs2.c

3.  Compile Open MPI with the --enable-shared and --with-pic options:

./configure --prefix=/opt/openmpi-1.6.5 --enable-shared --with-pic
--with-io-romio-flags="--with-pvfs2=/opt/orangefs
--with-file-system=pvfs2"

where...

*/opt/orangefs* = the location of your OrangeFS installation

*/opt/openmpi-1.6.5* = the location of your Open MPI installation

**Note     **You can remove the *--prefix* command to install
to*/usr/local*

4.  Make and install freshly compiled Open MPI 1.6.5 with OrangeFS
    Support

·sudo make all install

5.  Set LD\_LIBRARY\_PATH to point to the Open MPI libs

export LD\_LIBRARY\_PATH=/opt/openmpi-1.6.5/lib:\$LD\_LIBRARY\_PATH

where...

*/opt/orangefs* = the location of your OrangeFS installation

*/opt/openmpi-1.6.5* = the location of your Open MPI installation

 

 

 

 
