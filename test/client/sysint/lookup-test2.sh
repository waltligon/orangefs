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

# tests the following functionality:
#
#  /a/b/c/d/e/blink1 -> /a/b
#  /a/b/c/d/e/blink2 -> ../../../../b
#
#  /a/blink3 -> /a/b
#  /a/blink4 -> ./b
#  /a/blink5 -> b
#
#  /a/b/c/d/e/blink6 -> /a/blink4
#  /a/b/c/d/e/blink7 -> ../../../../blink3
#
#  /a/blink8 -> /a/b/c/d/e/blink7
#  /a/blink9 -> /a/b/c/d/e/blink6
#
#  /a/blink-invalid -> ../../../../invalid
#  /a/blink-dead -> ./blink-dead
#
#  lookups are then done on all of those symlinks, in addition to the
#  following paths:
#
#  /a/blink3/c/d/e/blink1
#  /a/blink4/c/d/e/blink1
#  /a/blink5/c/d/e/blink1
#  [ ADD SOME MORE ]
#
#  all of those path lookups should resolve to the handle of the 'b'
#  directory when following symlinks (except the invalid and dead
#  links, of course).  it's a decent test because it tests 1) absolute
#  links, 2) relative links, 3) relative links with consecutive '..'
#  segments, 4) recursive symlinks, 5) invalid symlinks (i.e. that
#  point to a non-existent path), and 6) dead links (i.e. symlinks that
#  point to themselves recursively)

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
$SYSINT_TEST_DIR/mkdir /a
$SYSINT_TEST_DIR/mkdir /a/b
$SYSINT_TEST_DIR/mkdir /a/b/c
$SYSINT_TEST_DIR/mkdir /a/b/c/d
$SYSINT_TEST_DIR/mkdir /a/b/c/d/e


# symlink /a/b/c/d/e/blink1 -> /a/b
$SYSINT_TEST_DIR/symlink /a/b/c/d/e/blink1 /a/b

# symlink /a/b/c/d/e/blink2 -> ../../../../b
$SYSINT_TEST_DIR/symlink /a/b/c/d/e/blink2 ../../../../b

# symlink /a/blink3 -> /a/b
$SYSINT_TEST_DIR/symlink /a/blink3 /a/b

# symlink /a/blink4 -> ./b
$SYSINT_TEST_DIR/symlink /a/blink4 ./b

# symlink /a/blink5 -> b
$SYSINT_TEST_DIR/symlink /a/blink5 b

# symlink /a/b/c/d/e/blink6 -> /a/blink4
$SYSINT_TEST_DIR/symlink /a/b/c/d/e/blink6 /a/blink4

# symlink /a/b/c/d/e/blink7 -> ../../../../blink3
$SYSINT_TEST_DIR/symlink /a/b/c/d/e/blink7 ../../../../blink3

# symlink /a/blink8 -> /a/b/c/d/e/blink7
$SYSINT_TEST_DIR/symlink /a/blink8 /a/b/c/d/e/blink7

# symlink /a/blink9 -> /a/b/c/d/e/blink6
$SYSINT_TEST_DIR/symlink /a/blink9 /a/b/c/d/e/blink6

# symlink /a/blink-invalid -> ../../../../invalid
$SYSINT_TEST_DIR/symlink /a/blink-invalid ../../../../invalid

# symlink /a/blink-dead -> ./blink-dead
$SYSINT_TEST_DIR/symlink /a/blink-dead ./blink-dead


# lookup both invalid and dead links to make sure
# they can't resolve if followed
$SYSINT_TEST_DIR/lookup /a/blink-invalid 1
$SYSINT_TEST_DIR/lookup /a/blink-dead 1

# lookup other pathnames that should now resolve properly
$SYSINT_TEST_DIR/lookup /a/blink3/c/d/e/blink1 1
$SYSINT_TEST_DIR/lookup /a/blink4/c/d/e/blink1 1
$SYSINT_TEST_DIR/lookup /a/blink5/c/d/e/blink1 1

$SYSINT_TEST_DIR/lookup /a/blink3/c/d/e/blink2 1
$SYSINT_TEST_DIR/lookup /a/blink4/c/d/e/blink2 1
$SYSINT_TEST_DIR/lookup /a/blink5/c/d/e/blink2 1

$SYSINT_TEST_DIR/lookup /a/blink3/c/d/e/blink6 1
$SYSINT_TEST_DIR/lookup /a/blink4/c/d/e/blink6 1
$SYSINT_TEST_DIR/lookup /a/blink5/c/d/e/blink6 1

$SYSINT_TEST_DIR/lookup /a/blink3/c/d/e/blink7 1
$SYSINT_TEST_DIR/lookup /a/blink4/c/d/e/blink7 1
$SYSINT_TEST_DIR/lookup /a/blink5/c/d/e/blink7 1

# cleanup all objects created
#
if test "x$2" = "x"; then
    $SYSINT_TEST_DIR/remove /a/b/c/d/e/blink1
    $SYSINT_TEST_DIR/remove /a/b/c/d/e/blink2
    $SYSINT_TEST_DIR/remove /a/b/c/d/e/blink6
    $SYSINT_TEST_DIR/remove /a/b/c/d/e/blink7

    $SYSINT_TEST_DIR/remove /a/blink3
    $SYSINT_TEST_DIR/remove /a/blink4
    $SYSINT_TEST_DIR/remove /a/blink5
    $SYSINT_TEST_DIR/remove /a/blink8
    $SYSINT_TEST_DIR/remove /a/blink9

    $SYSINT_TEST_DIR/remove /a/blink-invalid
    $SYSINT_TEST_DIR/remove /a/blink-dead

    $SYSINT_TEST_DIR/remove /a/b/c/d/e
    $SYSINT_TEST_DIR/remove /a/b/c/d
    $SYSINT_TEST_DIR/remove /a/b/c
    $SYSINT_TEST_DIR/remove /a/b
    $SYSINT_TEST_DIR/remove /a
fi