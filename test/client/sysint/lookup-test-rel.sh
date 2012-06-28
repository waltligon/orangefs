#!/bin/bash
#
# A PVFS2 test script using only sysint test programs
#
# This script focuses on making sure that basic mkdir,
# and lookup programs are working properly.
#
# During operation, this test stresses the lookup operation
# with relative (..) elements, as well as . elements and 
# extra slashes.
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

# check if we can find the lookup program
if ! test -x $SYSINT_TEST_DIR/lookup; then
    echo "Cannot find sysint test program: $SYSINT_TEST_DIR/lookup"
    exit 1
fi

# note: set TEST_ROOT in environment -- may be empty

# create the test directory tree
$SYSINT_TEST_DIR/mkdir $TEST_ROOT/foo
$SYSINT_TEST_DIR/mkdir $TEST_ROOT/foo/bar
$SYSINT_TEST_DIR/mkdir $TEST_ROOT/foo/blah
$SYSINT_TEST_DIR/mkdir $TEST_ROOT/foo/bar/baz
$SYSINT_TEST_DIR/mkdir $TEST_ROOT/foo/bar/baz/dir1
$SYSINT_TEST_DIR/mkdir $TEST_ROOT/foo/bar/baz/dir1/dir2
$SYSINT_TEST_DIR/mkdir $TEST_ROOT/foo/bar/baz/dir1/dir2/dir3
$SYSINT_TEST_DIR/mkdir $TEST_ROOT/foo/bar/baz/dir1/dir2/dir3/dir4

# expect success - normal lookup
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/blah
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz/dir1
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz/dir1/dir2
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz/dir1/dir2/dir3
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz/dir1/dir2/dir3/dir4

#expect success - trailing slash
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz/dir1/dir2/

# expect success - relative lookup
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/..
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/../foo
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz/../../blah
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/../bar/baz
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz/../../../foo/bar/..
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz/dir1/dir2/dir3/../../..
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz/../../blah/../bar

# expect success - . elements
$SYSINT_TEST_DIR/lookup $TEST_ROOT/.
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/.
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/./bar
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz/./../../blah/.
$SYSINT_TEST_DIR/lookup $TEST_ROOT/./foo/bar/../bar/baz
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/./././bar/baz/../../../foo/bar/..
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz/dir1/dir2/dir3/../../../.
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/./bar/baz/.././.././blah/../bar

# expect success - extra slashes
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo//bar
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/..//blah
$SYSINT_TEST_DIR/lookup $TEST_ROOT/./foo///bar/../bar/baz

# expect failure - missing dirs
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bad
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/../bad
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/./blah/bad
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bad/baz
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz/./../../bad/.
$SYSINT_TEST_DIR/lookup $TEST_ROOT/./foo/bad/../bar/dir1
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/./././bar/baz/../../../bad/bar/..
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz/dir1/dir2/dir3/../../bad
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/./bad/baz/..

# expect failure - too many ..'s - works best if TEST_ROOT is empty
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/../..
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/../foo/../..
$SYSINT_TEST_DIR/lookup $TEST_ROOT/foo/bar/baz/../../blah/../../../..
$SYSINT_TEST_DIR/lookup $TEST_ROOT/./foo/bar/../bar/dir1/../../blah/../../..
$SYSINT_TEST_DIR/lookup $TEST_ROOT/./..
$SYSINT_TEST_DIR/lookup $TEST_ROOT/./././..

# cleanup all objects created
#
if test "x$2" = "x"; then
    $SYSINT_TEST_DIR/remove ${TEST_ROOT}/foo/bar/baz/dir1/dir2/dir3/dir4
    $SYSINT_TEST_DIR/remove ${TEST_ROOT}/foo/bar/baz/dir1/dir2/dir3
    $SYSINT_TEST_DIR/remove ${TEST_ROOT}/foo/bar/baz/dir1/dir2
    $SYSINT_TEST_DIR/remove ${TEST_ROOT}/foo/bar/baz/dir1
    $SYSINT_TEST_DIR/remove ${TEST_ROOT}/foo/bar/baz
	$SYSINT_TEST_DIR/remove ${TEST_ROOT}/foo/blah
    $SYSINT_TEST_DIR/remove ${TEST_ROOT}/foo/bar
    $SYSINT_TEST_DIR/remove ${TEST_ROOT}/foo
fi