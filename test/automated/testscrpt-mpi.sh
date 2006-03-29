#!/bin/sh

# parent script already built and deployed pvfs2 and mpich2.  All we need to do
# now is set up PAV and kick off some scripts
# At this point we have:
#
#   ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}         installation prefix for pvfs2
#   ${PVFS2_DEST}/pvfs2-${CVS_TAG}/test/common/pav location of pav scripts
#   ${PVFS2_DEST}/soft/mpich2                     installation prefix for mpich2
#   $PVFS2_MOUNTPOINT                             mountpoint for pvfs2

if [ -z "$PVFS2_DEST" ] ; then
	echo "do not run this script directly."
	exit 1
fi

MPIIO_SCRIPTS=${PVFS2_DEST}/pvfs2-${CVS_TAG}/test/automated/mpiio-tests.d


# we will only do multi processor tests if there's a pav config file 
# we can use.   several tests can fall back to single processor, and 
# still do something reasonable. 

export PAV_CONFIG=${HOME}/pav-config-testing

# cluster environments need a few things available on a cluster-wide file
# system: pav (which needs some pvfs2 programs), the mpi program, mpich2
# (specifically mpd and tools )

export CLUSTER_DIR=${HOME}/nightly
[ -d ${CLUSTER_DIR} ] ||  mkdir -p ${CLUSTER_DIR}
rm -rf ${CLUSTER_DIR}/pav ${CLUSTER_DIR}/mpich2 ${CLUSTER_DIR}/pvfs2
cp -ar ${PVFS2_DEST}/pvfs2-${CVS_TAG}/test/common/pav ${CLUSTER_DIR}
cp -ar ${PVFS2_DEST}/soft/mpich2 ${CLUSTER_DIR}
cp -ar ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG} ${CLUSTER_DIR}/pvfs2

run_parts ${MPIIO_SCRIPTS}
