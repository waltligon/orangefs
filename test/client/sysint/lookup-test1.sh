#!/bin/bash
#
# A PVFS2 test script using only sysint test programs
#
# This script focuses on making sure that basic mkdir,
# create, lookup, and symlink programs are working properly.
#
# During operation, this test stresses the lookup operation
# and symlink resolution.
#

# check if a sysint test path was specified
if test "x$1" = "x"; then
    SYSINT_TEST_DIR=.
else
    SYSINT_TEST_DIR=$1
fi

# check if we can find the mkdir program
if ! test -x $SYSINT_TEST_DIR/mkdir; then
    echo "Cannot find sysint test program: $SYSINT_TEST_DIR/mkdir"
    exit 1
fi

# check if we can find the create program
if ! test -x $SYSINT_TEST_DIR/create; then
    echo "Cannot find sysint test program: $SYSINT_TEST_DIR/create"
    exit 1
fi

# check if we can find the symlink program
if ! test -x $SYSINT_TEST_DIR/symlink; then
    echo "Cannot find sysint test program: $SYSINT_TEST_DIR/symlink"
    exit 1
fi

# check if we can find the lookup program
if ! test -x $SYSINT_TEST_DIR/lookup; then
    echo "Cannot find sysint test program: $SYSINT_TEST_DIR/lookup"
    exit 1
fi


# create the test directory tree
$SYSINT_TEST_DIR/mkdir /foo
$SYSINT_TEST_DIR/mkdir /foo/bar
$SYSINT_TEST_DIR/mkdir /foo/bar/baz
$SYSINT_TEST_DIR/mkdir /foo/bar/baz/dir1
$SYSINT_TEST_DIR/mkdir /foo/bar/baz/dir1/dir2
$SYSINT_TEST_DIR/mkdir /foo/bar/baz/dir1/dir2/dir3
$SYSINT_TEST_DIR/mkdir /foo/bar/baz/dir1/dir2/dir3/dir4
$SYSINT_TEST_DIR/mkdir /foo/bar/baz/dir1/dir2/dir3/dir4/dir5
$SYSINT_TEST_DIR/mkdir /foo/bar/baz/dir1/dir2/dir3/dir4/dir5/dir6


# symlink /foo/barlink1 -> /foo/bar
$SYSINT_TEST_DIR/symlink /foo/barlink1 /foo/bar

# symlink /foo/barlink2 -> bar
$SYSINT_TEST_DIR/symlink /foo/barlink2 bar

# symlink /baz -> /foo/bar/baz
$SYSINT_TEST_DIR/symlink /baz /foo/bar/baz

# symlink /foo/bar/baz/dir1/dir2link1 -> /foo/barlink1/baz/dir1/dir2
$SYSINT_TEST_DIR/symlink /foo/bar/baz/dir1/dir2link1 /foo/barlink1/baz/dir1/dir2

# symlink /foo/bar/baz/dir1/dir2link2 -> dir2
$SYSINT_TEST_DIR/symlink /foo/bar/baz/dir1/dir2link2 dir2

# symlink /dir2 -> /foo/barlink2/baz/dir1/dir2link2
$SYSINT_TEST_DIR/symlink /dir2 /foo/barlink2/baz/dir1/dir2link2

# symlink /deadlink1 -> /deadlink1
$SYSINT_TEST_DIR/symlink /deadlink1 /deadlink1

# symlink /deadlink2 -> deadlink1
$SYSINT_TEST_DIR/symlink /deadlink2 deadlink1


# lookup both dead links to make sure they can't resolve if followed
$SYSINT_TEST_DIR/lookup /deadlink2 1
$SYSINT_TEST_DIR/lookup /deadlink1 1


# create some files, using some symlink references in baz directory
$SYSINT_TEST_DIR/create /foo/bar/baz/testfile01
$SYSINT_TEST_DIR/create /foo/barlink1/baz/testfile02
$SYSINT_TEST_DIR/create /foo/barlink2/baz/testfile03
$SYSINT_TEST_DIR/create /baz/testfile04

# create some files, using some symlink references in dir2 directory
$SYSINT_TEST_DIR/create /foo/bar/baz/dir1/dir2/testfile11
$SYSINT_TEST_DIR/create /foo/bar/baz/dir1/dir2link1/testfile12
$SYSINT_TEST_DIR/create /foo/bar/baz/dir1/dir2link2/testfile13
$SYSINT_TEST_DIR/create /dir2/testfile14


# cleanup all objects created
#
if test "x$2" = "x"; then
    $SYSINT_TEST_DIR/remove /foo/bar/baz/dir1/dir2/dir3/dir4/dir5/dir6
    $SYSINT_TEST_DIR/remove /foo/bar/baz/dir1/dir2/dir3/dir4/dir5
    $SYSINT_TEST_DIR/remove /foo/bar/baz/dir1/dir2/dir3/dir4
    $SYSINT_TEST_DIR/remove /foo/bar/baz/dir1/dir2/dir3

    $SYSINT_TEST_DIR/remove /foo/barlink2/baz/dir1/dir2link1/testfile11
    $SYSINT_TEST_DIR/remove /foo/barlink1/baz/dir1/dir2link1/testfile12
    $SYSINT_TEST_DIR/remove /foo/bar/baz/dir1/dir2link1/testfile13
    $SYSINT_TEST_DIR/remove /baz/dir1/dir2link1/testfile14

    $SYSINT_TEST_DIR/remove /foo/barlink2/baz/dir1/dir2link1
    $SYSINT_TEST_DIR/remove /foo/barlink1/baz/dir1/dir2link2

    $SYSINT_TEST_DIR/remove /foo/bar/baz/dir1/dir2
    $SYSINT_TEST_DIR/remove /foo/bar/baz/dir1

    $SYSINT_TEST_DIR/remove /foo/barlink2/baz/testfile03
    $SYSINT_TEST_DIR/remove /foo/barlink1/baz/testfile02
    $SYSINT_TEST_DIR/remove /baz/testfile01
    $SYSINT_TEST_DIR/remove /foo/barlink2/baz/testfile04

    $SYSINT_TEST_DIR/remove /foo/bar/baz

    $SYSINT_TEST_DIR/remove /foo/barlink1
    $SYSINT_TEST_DIR/remove /foo/barlink2

    $SYSINT_TEST_DIR/remove /foo/bar
    $SYSINT_TEST_DIR/remove /foo

    $SYSINT_TEST_DIR/remove /baz
    $SYSINT_TEST_DIR/remove /dir2

    $SYSINT_TEST_DIR/remove /deadlink1
    $SYSINT_TEST_DIR/remove /deadlink2
fi