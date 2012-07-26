
# Script for building various codes with PVFS stupport.
# Set the path to the source of the package in the
# appropriate variable below and run the script.
# Packages without a source path set are not built.
# If PVFSINSTALLDIR is left blank script expects
# PVFS is installed in /bin /lib etc. and will install
# packages there too.  Edit INSTALLDIR variables to
# put packages in different places.
#

PVFSINSTALLDIR=

CORESOURCEDIR=
COREINSTALLDIR=${PVFSINSTALLDIR}
TARSOURCEDIR=
TARINSTALLDIR=${PVFSINSTALLDIR}
BASHSOURCEDIR=
BASHINSTALLDIR=${PVFSINSTALLDIR}
TCSHSOURCEDIR=
TCSHINSTALLDIR=${PVFSINSTALLDIR}
XATTRSOURCEDIR=
XATTRINSTALLDIR=${PVFSINSTALLDIR}
ACLSOURCEDIR=
ACLINSTALLDIR=${PVFSINSTALLDIR}


LIBDIR=${PVFSINSTALLDIR}/lib

THISDIR=`pwd`

# Script to build GNU coreutils with PVFS support

if [ ${CORESOURCEDIR}foo != foo ] ;
    cd ${CORESOURCEDIR};
    ./configure --prefix=${COREINSTALLDIR} LDFLAGS=-L${LIBDIR} LIBS='-Wl,--no-as-needed -lofs -lpvfs2 -rdynamic -lssl -lcrypto -lpthread -lselinux'
    # Add code to check for success before proceeding
    make
    make install
    cd ${THISDIR};
fi

# Script to build GNU tar with PVFS support

if [ ${TARSOURCEDIR}foo != foo ] ;
   cd ${TARSOURCEDIR};
   ./configure --prefix=${TARINSTALLDIR} LDFLAGS=-L${LIBDIR} LIBS='-lofs -lpvfs2 -rdynamic -lssl -lcrypto -lpthread'
   # Add code to check for success before proceeding
   make
   make install
   cd ${THISDIR};
fi

# Script to build GNU bash with PVFS support

if [ ${BASHSOURCEDIR}foo != foo ] ;
   cd ${BASHSOURCEDIR};
   ./configure --prefix=${BASHINSTALLDIR} LDFLAGS=-L${LIBDIR} LIBS='-lofs -lpvfs2 -rdynamic -lssl -lcrypto -lpthread'
   # Add code to check for success before proceeding
   make
   make install
   cd ${THISDIR};
fi

# Script to build GNU tcsh with PVFS support

if [ ${TCSHSOURCEDIR}foo != foo ] ;
   cd ${TCSHSOURCEDIR};
   ./configure --prefix=${TCSHINSTALLDIR} LDFLAGS=-L${LIBDIR} LIBS='-lofs -lpvfs2 -rdynamic -lssl -lcrypto -lpthread'
   # Add code to check for success before proceeding
   make
   make install
   cd ${THISDIR};
fi

# Script to build GNU xattr with PVFS support
# Need to edit Makefile - doesn't use LIBS

if [ ${XATTRSOURCEDIR}foo != foo ] ;
   cd ${XATTRSOURCEDIR};
   ./configure --prefix=${XATTRINSTALLDIR} LDFLAGS=-L${LIBDIR} LIBS='-lofs -lpvfs2 -rdynamic -lssl -lcrypto -lpthread'
   # Add code to check for success before proceeding
   make
   make install
   cd ${THISDIR};
fi

# Script to build GNU acl with PVFS support
# Need to edit Makefile - doesn't use LIBS

if [ ${ACLSOURCEDIR}foo != foo ] ;
   cd ${ACLSOURCEDIR};
   ./configure --prefix=${ACLINSTALLDIR} LDFLAGS=-L${LIBDIR} LIBS='-lofs -lpvfs2 -rdynamic -lssl -lcrypto -lpthread'
   # Add code to check for success before proceeding
   make
   make install
   cd ${THISDIR};
fi

