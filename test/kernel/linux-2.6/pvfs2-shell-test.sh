#!/bin/bash
#
# A PVFS2 test script only useful for running
# on a mounted GNU/Linux pvfs2 volume
#
# This script focuses on making sure that basic shell
# programs work correctly on top of pvfs2.
#

#####################################
# misc functions here
#####################################

timestamp()
{
    WORDS=$1
    CMD=$2
    OUTPUT=$3

    DATE=`date`
    echo "$DATE: $WORDS"

    if ! test "x$OUTPUT" = "x"; then
        echo "Piping command output to /dev/null"
        $CMD > /dev/null
    else
        $CMD
    fi

    DATE=`date`
    echo "$DATE: Finished"
}

error_exit()
{
    echo "Aborting test due to detected error"
    exit 1
}

setup_testdir()
{
    DIR=$1

    echo "Removing directory $DIR (if it exists)"
    rm -rf $DIR

    echo "Creating directory $DIR"
    mkdir  $DIR
}

remove_testdir()
{
    DIR=$1

    echo "Removing directory $DIR"
    rm -rf $DIR
}

generate_hello_world_code()
{
    OUTFILE=$1

    rm -f $OUTFILE

    echo "#include <stdio.h>" >> $OUTFILE
    echo "int main(int argc, char **argv) {" >> $OUTFILE
    echo "printf(\"Hello, World!\n\");" >> $OUTFILE
    echo "return 0; }" >> $OUTFILE
}


#####################################
# simple directory test functions
#####################################

# create a dir
# create 100 files in that dir
# rm -rf the dir
directory_test1()
{
    echo ""
    echo "******************************************"
    echo "* RUNNING DIRECTORY TEST 1"
    echo "******************************************"

    setup_testdir $PVFS2_TESTDIR

    DATE=`date`
    echo "$DATE: Creating 100 new files"
    for f in `seq 1 100`; do
        CUR_FILE=$PVFS2_TESTDIR/testfile0$f

        RET=`touch $CUR_FILE`

        if ! test "x$RET" = "x"; then
            echo "FAILURE IN DIRECTORY TEST "
            echo "Failed to create file $CUR_FILE"
            return 1
        fi
    done
    DATE=`date`
    echo "$DATE: Finished"

    remove_testdir $PVFS2_TESTDIR

    return 0
}

# create a dir
# create 256 files in that dir
# create 256 directories in that dir
# do a find on that dir > /dev/null
# do a recursive ls -al on that dir > /dev/null
# rm -rf the dir
directory_test2()
{
    echo ""
    echo "******************************************"
    echo "* RUNNING DIRECTORY TEST 2"
    echo "******************************************"

    setup_testdir $PVFS2_TESTDIR

    DATE=`date`
    echo "$DATE: Creating 256 new files"
    for f in `seq 1 100`; do
        CUR_FILE=$PVFS2_TESTDIR/testfile1$f

        RET=`touch $CUR_FILE`

        if ! test "x$RET" = "x"; then
            echo "FAILURE IN DIRECTORY TEST 2"
            echo "Failed to create file $CUR_FILE"
            return 1
        fi
    done
    DATE=`date`
    echo "$DATE: Finished"

    DATE=`date`
    echo "$DATE: Creating 256 new directories"
    for f in `seq 1 100`; do
        CUR_DIR=$PVFS2_TESTDIR/testdir1$f

        RET=`mkdir $CUR_DIR`

        if ! test "x$RET" = "x"; then
            echo "FAILURE IN DIRECTORY TEST 2"
            echo "Failed to create directory $CUR_DIR"
            return 1
        fi
    done
    DATE=`date`
    echo "$DATE: Finished"

    CMD="find $PVFS2_TESTDIR"
    timestamp "Running Find $PVFS2_TESTDIR" "$CMD" /dev/null

    CMD="ls -alR $PVFS2_TESTDIR"
    timestamp "Running ls -alR $PVFS2_TESTDIR" "$CMD" /dev/null

    remove_testdir $PVFS2_TESTDIR

    return 0
}

# create a dir
# create 256 files in that dir
# create 256 directories in that dir
# create 256 symlinks in the 256 directories to those files in that dir
# do a find on that dir > /dev/null
# do a recursive ls -al on that dir > /dev/null
# rm -rf the dir
directory_test3()
{
    echo ""
    echo "******************************************"
    echo "* RUNNING DIRECTORY TEST 3"
    echo "******************************************"

    setup_testdir $PVFS2_TESTDIR

    DATE=`date`
    echo "$DATE: Creating 256 new files"
    for f in `seq 1 100`; do
        CUR_FILE=$PVFS2_TESTDIR/testfile1$f

        RET=`touch $CUR_FILE`

        if ! test "x$RET" = "x"; then
            echo "FAILURE IN DIRECTORY TEST 3"
            echo "Failed to create file $CUR_FILE"
            return 1
        fi
    done
    DATE=`date`
    echo "$DATE: Finished"

    DATE=`date`
    echo "$DATE: Creating 256 new directories"
    for f in `seq 1 100`; do
        CUR_DIR=$PVFS2_TESTDIR/testdir1$f

        RET=`mkdir $CUR_DIR`

        if ! test "x$RET" = "x"; then
            echo "FAILURE IN DIRECTORY TEST 3"
            echo "Failed to create directory $CUR_DIR"
            return 1
        fi
    done
    DATE=`date`
    echo "$DATE: Finished"

    DATE=`date`
    echo "$DATE: Creating 256 new symlinks"
    for f in `seq 1 100`; do
        CUR_FILE=$PVFS2_TESTDIR/testfile1$f
        CUR_DIR=$PVFS2_TESTDIR/testdir1$f
        CUR_LINK=$CUR_DIR/testsymlink1$f

        RET=`ln -s $CUR_FILE $CUR_LINK`

        if ! test "x$RET" = "x"; then
            echo "FAILURE IN DIRECTORY TEST 3"
            echo "Failed to create symlink $CUR_LINK"
            return 1
        fi
    done
    DATE=`date`
    echo "$DATE: Finished"

    CMD="find $PVFS2_TESTDIR"
    timestamp "Running Find $PVFS2_TESTDIR" "$CMD" /dev/null

    CMD="ls -alR $PVFS2_TESTDIR"
    timestamp "Running ls -alR $PVFS2_TESTDIR" "$CMD" /dev/null

    remove_testdir $PVFS2_TESTDIR

    return 0
}

#####################################
# simple i/o test functions
#####################################

# generate a 32MB file full of zeroed data (using dd)
# cp this file
# remove the files
io_test1()
{
    echo ""
    echo "******************************************"
    echo "* RUNNING I/O TEST 1"
    echo "******************************************"

    if ! test -c /dev/zero; then
        echo "Skipping test because /dev/zero does not "
        echo " exist or is not a character file!"
        return 1
    fi

    setup_testdir $PVFS2_TESTDIR

    OUTFILE1="$PVFS2_TESTDIR/zero_data_file1"
    OUTFILE2="$PVFS2_TESTDIR/zero_data_file2"

    echo "Removing temporary datafiles (if they exist)"
    rm -rf $OUTFILE1 $OUTFILE2

    CMD="dd if=/dev/zero of=$OUTFILE1 bs=4194304 count=8"
    timestamp "Generating zeroed 32MB file in 8 4MB blocks" "$CMD"

    CMD="cp $OUTFILE1 $OUTFILE2"
    timestamp "Copying data file" "$CMD"

    CMD="rm -rf $OUTFILE1 $OUTFILE2"
    timestamp "Removing temporary datafiles" "$CMD"

    remove_testdir $PVFS2_TESTDIR

    return 0
}

# generate a 32MB file full of random data (using dd)
# cp this file
# remove the files
io_test2()
{
    echo ""
    echo "******************************************"
    echo "* RUNNING I/O TEST 2"
    echo "******************************************"

    if ! test -c /dev/urandom; then
        echo "Skipping test because /dev/urandom does not "
        echo " exist or is not a character file!"
        return 1
    fi

    setup_testdir $PVFS2_TESTDIR

    OUTFILE1="$PVFS2_TESTDIR/random_data_file1"
    OUTFILE2="$PVFS2_TESTDIR/random_data_file2"

    echo "Removing temporary datafiles (if they exist)"
    rm -rf $OUTFILE1 $OUTFILE2

    CMD="dd if=/dev/urandom of=$OUTFILE1 bs=4194304 count=8"
    timestamp "Generating random 32MB file in 8 4MB blocks" "$CMD"

    CMD="cp $OUTFILE1 $OUTFILE2"
    timestamp "Copying data file" "$CMD"

    CMD="rm -rf $OUTFILE1 $OUTFILE2"
    timestamp "Removing temporary datafiles" "$CMD"

    remove_testdir $PVFS2_TESTDIR

    return 0
}


#####################################
# simple compilation test functions
#####################################

# generate a hello world c program
# run gcc on the file
# make sure the file exists and is executable
# execute the program to make sure the output is good
# remove the source and binary files
compile_test1()
{
    echo ""
    echo "******************************************"
    echo "* RUNNING COMPILE TEST 1"
    echo "******************************************"

    setup_testdir $PVFS2_TESTDIR

    HELLO_WORLD_SOURCE="$PVFS2_TESTDIR/__hello_world.c"

    echo "Removing temporary files (if they exist)"
    rm -rf $HELLO_WORLD_SOURCE $HELLO_WORLD_BINARY

    generate_hello_world_code $HELLO_WORLD_SOURCE


    # testing only
    echo "DUMPING GENERATED FILE"
    echo ""
    cat $HELLO_WORLD_SOURCE

    echo "FIXME: THIS TEST ISN'T FINISHED YET"

    remove_testdir $PVFS2_TESTDIR

    return 0
}

#####################################
# script entry point
#####################################

ORIG_DIR=`pwd`

if test "x$1" = "x"; then
    echo "Usage: pvfs2-shell-test.sh </pvfs/mnt/point>"
    exit 0
else
    PVFS2_MNT=$1
fi

echo "Using PVFS2 Mount Point: $PVFS2_MNT"

PVFS2_TESTDIR=$PVFS2_MNT/__testdir

cd $PVFS2_MNT

if test "x$ORIG_DIR" = "x`pwd`"; then
    echo "Cannot change directory to $PVFS_MNT"
    error_exit;
fi


#####################################
# start running tests here
#####################################

directory_test1

directory_test2

directory_test3

io_test1

io_test2

compile_test1

