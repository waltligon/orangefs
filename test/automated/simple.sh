#!/bin/sh

export PVFS2TAB_FILE=$pvfs2tabfile

echo $pvfs2bindir/pvfs2-statfs -m /tmp/pvfs2-pav-mount
$pvfs2bindir/pvfs2-statfs -m /tmp/pvfs2-pav-mount
if [ $? != 0 ] ; then
    echo "statfs failed. Aborting."
    exit 1
fi

echo $pvfs2bindir/pvfs2-import $pvfs2bindir/pvfs2-import /tmp/pvfs2-pav-mount/simple-test
$pvfs2bindir/pvfs2-import $pvfs2bindir/pvfs2-import /tmp/pvfs2-pav-mount/simple-test
if [ $? != 0 ] ; then
    echo "import failed. Aborting."
    exit 1
fi

echo $pvfs2bindir/pvfs2-export /tmp/pvfs2-pav-mount/simple-test /tmp/simple-test
$pvfs2bindir/pvfs2-export /tmp/pvfs2-pav-mount/simple-test /tmp/simple-test
if [ $? != 0 ] ; then
    echo "export failed. Aborting."
    exit 1
fi

echo diff $pvfs2bindir/pvfs2-import /tmp/simple-test
diff $pvfs2bindir/pvfs2-import /tmp/simple-test
if [ $? != 0 ] ; then
    echo "diff failed. Aborting."
    exit 1
fi

exit 0


