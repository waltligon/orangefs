#!/bin/bash
#
# A PVFS2 test script only useful for running
# on a mounted GNU/Linux pvfs2 volume
#
# This script focuses on making sure that basic shell
# programs work correctly on top of pvfs2.
#
# run this test like this:
#
# sh pvfs2-shell-test.sh /pvfs/mntpoint
#

#####################################
# runtime options
#####################################

# Fully comment out to disable categories of tests
# DO NOT just set the value to be 0
#
ENABLE_DIRECTORY_TESTS=1
ENABLE_IO_TESTS=1
ENABLE_EXECUTE_TESTS=1
ENABLE_COMPILE_TESTS=1
ENABLE_PERMISSION_TESTS=1
ENABLE_SYMLINK_TESTS=1

#####################################
# test constants here
#####################################

TEST_PERMISSIONS="0777 0767 0666 0644 0600 0500 0400 0000"

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

    if test $? -ne 0 ; then
        return 1
    fi

    DATE=`date`
    echo "$DATE: Finished"
    return 0
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

    DATE=`date`
    echo "$DATE: Generating Hello World source code"

    echo "#include <stdio.h>" >> $OUTFILE
    echo "int main(int argc, char **argv) {" >> $OUTFILE
    echo "printf(\"Hello, World!\n\");" >> $OUTFILE
    echo "return 0; }" >> $OUTFILE

    DATE=`date`
    echo "$DATE: Hello World source code written"
}

generate_mmap_read_code()
{
    OUTFILE=$1

    rm -f $OUTFILE

    DATE=`date`
    echo "$DATE: Generating mmap.c source code (an mmap read test)"

    cat >> $OUTFILE <<EOF
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
int main(int argc, char **argv) {
    int ret = -1, fd = -1; void *start = NULL;
    struct stat statbuf; char *ptr = NULL, *end = NULL;
    memset(&statbuf, 0, sizeof(statbuf));
    if (stat(argv[1],&statbuf) == 0) {
        if ((fd = open(argv[1], O_RDONLY)) != -1) {
            start = mmap(NULL, statbuf.st_size, PROT_READ,
                         MAP_PRIVATE, fd, 0);
            if (start != MAP_FAILED) {
                fprintf(stderr,"MMAP Read content length is %d "
                        "\n\n", (int)statbuf.st_size);
                end = (char *)(start + statbuf.st_size);
                for(ptr = (char *)start; ptr != end; ptr++)
                    if (ptr)
                        printf("%x ",*ptr);
                printf("\n");
                ret = munmap(start, statbuf.st_size);
            }
            close(fd);
        }
    }
    return ret;
}
EOF

    DATE=`date`
    echo "$DATE: MMAP Read source code written"
}

stat_file()
{
    WORDS=$1
    FILE=$2

    DATE=`date`
    echo "$DATE: $WORDS"

    echo "***********************************"
    stat $FILE
    RET=$?
    echo "***********************************"

    DATE=`date`
    echo "$DATE: Finished"
    return $RET
}

check_entry_permissions()
{
    ENTRY=$1
    PERM=$2

#    echo "  - Verifying that entry permission is now $PERM"
    OUTPUT=`stat $ENTRY | head -n 4 | tail -n 1 | awk '{print \$2}' | grep $PERM`

    if test "x$OUTPUT" = "x"; then
        echo "Permission check: FAILED.  Test Aborting."
        return 1
    else
        echo "Permission check: OK $OUTPUT"
    fi
    return 0
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

        touch $CUR_FILE

        if test $? -ne 0 ; then
            echo ""
            echo "******************************************"
            echo "* FAILED DIRECTORY TEST 1"
            echo "******************************************"
            return 1
        fi
    done
    DATE=`date`
    echo "$DATE: Finished"

    remove_testdir $PVFS2_TESTDIR

    echo ""
    echo "******************************************"
    echo "* PASSED DIRECTORY TEST 1"
    echo "******************************************"
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

        touch $CUR_FILE

        if test $? -ne 0 ; then
            echo ""
            echo "******************************************"
            echo "* FAILED DIRECTORY TEST 2 [stage 1]"
            echo "******************************************"
            return 1
        fi
    done
    DATE=`date`
    echo "$DATE: Finished"

    DATE=`date`
    echo "$DATE: Creating 256 new directories"
    for f in `seq 1 100`; do
        CUR_DIR=$PVFS2_TESTDIR/testdir1$f

        mkdir $CUR_DIR

        if test $? -ne 0 ; then
            echo ""
            echo "******************************************"
            echo "* FAILED DIRECTORY TEST 2 [stage 2]"
            echo "******************************************"
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

    echo ""
    echo "******************************************"
    echo "* PASSED DIRECTORY TEST 2"
    echo "******************************************"
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

        touch $CUR_FILE

        if test $? -ne 0 ; then
            echo ""
            echo "******************************************"
            echo "* FAILED DIRECTORY TEST 3 [stage 1]"
            echo "******************************************"
            return 1
        fi
    done
    DATE=`date`
    echo "$DATE: Finished"

    DATE=`date`
    echo "$DATE: Creating 256 new directories"
    for f in `seq 1 100`; do
        CUR_DIR=$PVFS2_TESTDIR/testdir1$f

        mkdir $CUR_DIR

        if test $? -ne 0 ; then
            echo ""
            echo "******************************************"
            echo "* FAILED DIRECTORY TEST 3 [stage 2]"
            echo "******************************************"
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

        ln -s $CUR_FILE $CUR_LINK

        if test $? -ne 0 ; then
            echo ""
            echo "******************************************"
            echo "* FAILED DIRECTORY TEST 3 [stage 3]"
            echo "******************************************"
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

    echo ""
    echo "******************************************"
    echo "* PASSED DIRECTORY TEST 3"
    echo "******************************************"
    return 0
}

# create 100 nested subdirectories
# ls -alR the top-level dir
# rm -rf the dir
directory_test4()
{
    echo ""
    echo "******************************************"
    echo "* RUNNING DIRECTORY TEST 4"
    echo "******************************************"

    setup_testdir $PVFS2_TESTDIR

    old_dir=`pwd`
    cd $PVFS2_TESTDIR
    mkdir $PVFS2_TESTDIR/100

    DATE=`date`
    echo "$DATE: Creating 100 nested subdirectories"

    # no error checking inside the loop as we're playing
    # a shell trick in there to get the nesting right
    echo "100" ;
    for f in `seq 1 100`; do
        mkdir $_/$f
    done

    # now make sure all dirs exist
    NUMDIRS=`find . | grep -c .`
    if test $NUMDIRS -ne 102 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED DIRECTORY TEST 4 [stage 1]"
        echo "******************************************"
        return 1
    fi

    DATE=`date`
    echo "$DATE: Finished"

    CMD="ls -alR $PVFS2_TESTDIR/100"
    timestamp "Running ls -alR $PVFS2_TESTDIR/100" "$CMD" /dev/null

    cd $old_dir

    remove_testdir $PVFS2_TESTDIR

    echo ""
    echo "******************************************"
    echo "* PASSED DIRECTORY TEST 4"
    echo "******************************************"
    return 0
}

# create 50 nested subdirectories
# touch 1 file in each of the directories
# ls -alR the top-level dir
# rm -rf the dir
directory_test5()
{
    echo ""
    echo "******************************************"
    echo "* RUNNING DIRECTORY TEST 5"
    echo "******************************************"

    setup_testdir $PVFS2_TESTDIR

    old_dir=`pwd`
    cd $PVFS2_TESTDIR
    mkdir $PVFS2_TESTDIR/100

    DATE=`date`
    echo "$DATE: Creating 50 nested subdirectories"

    # no error checking inside the loop as we're playing
    # a shell trick in there to get the nesting right
    echo "100" ;
    for f in `seq 1 50`; do
        mkdir $_/$f
    done

    # now make sure all dirs exist
    NUMDIRS=`find . | grep -c .`
    if test $NUMDIRS -ne 52 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED DIRECTORY TEST 5 [stage 1]"
        echo "******************************************"
        return 1
    fi

    DATE=`date`
    echo "$DATE: Creating files in all nested subdirectories"
    for f in `find .`; do
        touch $f/TESTFILE

        if test $? -ne 0 ; then
            echo ""
            echo "******************************************"
            echo "* FAILED DIRECTORY TEST 5 [stage 2]"
            echo "******************************************"
            return 1
        fi
    done

    DATE=`date`
    echo "$DATE: Finished"

    CMD="ls -alR $PVFS2_TESTDIR/100"
    timestamp "Running ls -alR $PVFS2_TESTDIR/100" "$CMD" /dev/null

    cd $old_dir

    remove_testdir $PVFS2_TESTDIR

    echo ""
    echo "******************************************"
    echo "* PASSED DIRECTORY TEST 5"
    echo "******************************************"
    return 0
}


#####################################
# simple permission test functions
#####################################

# create a dir
# create a file in that dir
# change permissions of that file to each of TEST_PERMISSIONS[] and verify
# rm -rf the dir
permission_test1()
{
    echo ""
    echo "******************************************"
    echo "* RUNNING PERMISSION TEST 1"
    echo "******************************************"

    setup_testdir $PVFS2_TESTDIR

    TESTFILE=$PVFS2_TESTDIR/perm-testfile

    echo "Creating test file $TESTFILE"
    touch $TESTFILE

    if ! test -f $TESTFILE; then
        echo ""
        echo "******************************************"
        echo "* FAILED PERMISSION TEST 1 [stage 1]"
        echo "******************************************"
        return 1
    fi

    DATE=`date`
    echo "$DATE: Modifying permissions of test file"
    for f in $TEST_PERMISSIONS; do
        echo "Changing permission of test file to $f"
        chmod $f $TESTFILE
        check_entry_permissions $TESTFILE $f
        if test $? -ne 0 ; then
            echo ""
            echo "******************************************"
            echo "* FAILED PERMISSION TEST 1 [stage 2]"
            echo "******************************************"
            return 1;
        fi
    done
    DATE=`date`
    echo "$DATE: Finished"

    echo "Removing testfile"
    rm -f $TESTFILE

    remove_testdir $PVFS2_TESTDIR

    echo ""
    echo "******************************************"
    echo "* PASSED PERMISSION TEST 1"
    echo "******************************************"
    return 0
}

# create a dir
# create a dir in that dir
# change permissions of that dir to each of TEST_PERMISSIONS[] and verify
# rm -rf the dir(s)
permission_test2()
{
    echo ""
    echo "******************************************"
    echo "* RUNNING PERMISSION TEST 2"
    echo "******************************************"

    setup_testdir $PVFS2_TESTDIR

    TESTDIR=$PVFS2_TESTDIR/perm-testdir

    echo "Removing test dir $TESTDIR (if exists)"
    rm -rf $TESTDIR

    echo "Creating test dir $TESTDIR"
    mkdir $TESTDIR

    if ! test -d $TESTDIR; then
        echo ""
        echo "******************************************"
        echo "* FAILED PERMISSION TEST 2 [stage 1]"
        echo "******************************************"
        return 1
    fi

    DATE=`date`
    echo "$DATE: Modifying permissions of test dir"
    for f in $TEST_PERMISSIONS; do
        echo "Changing permission of test dir to $f"
        chmod $f $TESTDIR
        check_entry_permissions $TESTDIR $f
        if test $? -ne 0 ; then
            echo ""
            echo "******************************************"
            echo "* FAILED PERMISSION TEST 2 [stage 2]"
            echo "******************************************"
            return 1;
        fi
    done
    DATE=`date`
    echo "$DATE: Finished"

    echo "Removing testdir"
    rm -rf $TESTDIR

    remove_testdir $PVFS2_TESTDIR

    echo ""
    echo "******************************************"
    echo "* PASSED PERMISSION TEST 2"
    echo "******************************************"
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
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED I/O TEST 1 [stage 1]"
        echo "******************************************"
        return 1
    fi

    CMD="cp $OUTFILE1 $OUTFILE2"
    timestamp "Copying data file" "$CMD"
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED I/O TEST 1 [stage 2]"
        echo "******************************************"
        return 1
    fi

    CMD="rm -rf $OUTFILE1 $OUTFILE2"
    timestamp "Removing temporary datafiles" "$CMD"
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED I/O TEST 1 [stage 3]"
        echo "******************************************"
        return 1
    fi

    remove_testdir $PVFS2_TESTDIR

    echo ""
    echo "******************************************"
    echo "* PASSED I/O TEST 1"
    echo "******************************************"
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
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED I/O TEST 2 [stage 1]"
        echo "******************************************"
        return 1
    fi

    CMD="cp $OUTFILE1 $OUTFILE2"
    timestamp "Copying data file" "$CMD"
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED I/O TEST 2 [stage 2]"
        echo "******************************************"
        return 1
    fi

    CMD="rm -rf $OUTFILE1 $OUTFILE2"
    timestamp "Removing temporary datafiles" "$CMD"
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED I/O TEST 2 [stage 3]"
        echo "******************************************"
        return 1
    fi

    remove_testdir $PVFS2_TESTDIR

    echo ""
    echo "******************************************"
    echo "* PASSED I/O TEST 2"
    echo "******************************************"
    return 0
}

#####################################
# simple execute test functions
#####################################

# execute a file on pvfs2 multiple times in parallel
#
#   the idea is that one will finish before the other
#   causing the page cache to be flushed in pvfs2.
#   this forces the second one still running to re-read
#   the missing pages, but is an interesting case to test for.
#   this also could be seen using make -j3 on a pvfs2 volume.
execute_test1()
{
    echo ""
    echo "******************************************"
    echo "* RUNNING EXECUTE TEST 1"
    echo "******************************************"

    setup_testdir $PVFS2_TESTDIR

    PVFS2_LS="$PVFS2_TESTDIR/ls -al $PVFS2_TESTDIR"

    LS=`which ls`
    if ! test -x $LS; then
        echo ""
        echo "******************************************"
        echo "* FAILED EXECUTE TEST 1 [stage 1]"
        echo "******************************************"
        return 1
    fi

    # make sure ls will be running from the pvfs2 volume
    cp $LS $PVFS2_TESTDIR/ls
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED EXECUTE TEST 1 [stage 2]"
        echo "******************************************"
        return 1
    fi

    # populate the working dir to run the LS command on
    touch $PVFS2_TESTDIR/a $PVFS2_TESTDIR/b $PVFS2_TESTDIR/c
    touch $PVFS2_TESTDIR/1 $PVFS2_TESTDIR/2 $PVFS2_TESTDIR/3
    touch $PVFS2_TESTDIR/d $PVFS2_TESTDIR/e $PVFS2_TESTDIR/f
    touch $PVFS2_TESTDIR/4 $PVFS2_TESTDIR/5 $PVFS2_TESTDIR/6

    CMD="eval $PVFS2_LS & $PVFS2_LS & $PVFS2_LS & $PVFS2_LS & $PVFS2_LS"
    timestamp "Running 'ls' multiple times at once" "$CMD" /dev/null
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED EXECUTE TEST 1 [stage 3]"
        echo "******************************************"
        return 1
    fi

    # attempt to be sure the commands have returned before we
    # continue.  if we don't sleep here, it looks like a serious error
    # occured because we are listing a directory that we've removed
    # (below) before the command completes.  the error looks
    # particularly nasty because the binary the kernel is trying to
    # re-fetch has also disappeared in that case

    echo "Waiting for 'ls' commands to complete"
    sleep 30

    remove_testdir $PVFS2_TESTDIR

    echo ""
    echo "******************************************"
    echo "* PASSED EXECUTE TEST 1"
    echo "******************************************"
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
    HELLO_WORLD_BINARY="$PVFS2_TESTDIR/__hello_world"

    echo "Removing temporary files (if they exist)"
    rm -rf $HELLO_WORLD_SOURCE $HELLO_WORLD_BINARY

    generate_hello_world_code $HELLO_WORLD_SOURCE

    stat_file "Doing stat on generated source code" $HELLO_WORLD_SOURCE

    DATE=`date`
    echo "$DATE: Compiling source code"

    OUTPUT=`gcc $HELLO_WORLD_SOURCE -o $HELLO_WORLD_BINARY`
    if ! test "x$OUTPUT" = "x"; then
        echo "Clean Compilation Failed"
        echo $OUTPUT
        echo ""
        echo "******************************************"
        echo "* FAILED COMPILE TEST 1 [stage 1]"
        echo "******************************************"
        return 1
    fi

    if ! test -x "$HELLO_WORLD_BINARY"; then
        echo "Binary file not created!"
        echo $OUTPUT
        return 1
    fi

    DATE=`date`
    echo "$DATE: Compilation finished"

    stat_file "Doing stat on generated binary" $HELLO_WORLD_BINARY
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED COMPILE TEST 1 [stage 2]"
        echo "******************************************"
        return 1
    fi

    CMD="$HELLO_WORLD_BINARY"
    timestamp "Executing Hello World Program" $CMD
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED COMPILE TEST 1 [stage 3]"
        echo "******************************************"
        return 1
    fi

    remove_testdir $PVFS2_TESTDIR

    echo ""
    echo "******************************************"
    echo "* PASSED COMPILE TEST 1"
    echo "******************************************"
    return 0
}

# generate an mmap read c program
# run gcc on the file
# make sure the file exists and is executable
# generate a 4MB test binary file to mmap read
# execute the program on the test file to make sure the output is good
# remove the source, test, and binary files by removing the test dir
compile_test2()
{
    echo ""
    echo "******************************************"
    echo "* RUNNING COMPILE TEST 2"
    echo "******************************************"

    setup_testdir $PVFS2_TESTDIR

    MMAP_READ_SOURCE="$PVFS2_TESTDIR/__mmap_read.c"
    MMAP_READ_BINARY="$PVFS2_TESTDIR/__mmap_read"
    MMAP_READ_BINARY_DATA="$PVFS2_TESTDIR/__mmap_read_data"

    echo "Removing temporary files (if they exist)"
    rm -rf $MMAP_READ_SOURCE $MMAP_READ_BINARY $MMAP_READ_BINARY_DATA

    generate_mmap_read_code $MMAP_READ_SOURCE

    stat_file "Doing stat on generated source code" $MMAP_READ_SOURCE
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED COMPILE TEST 2 [stage 1]"
        echo "******************************************"
        return 1
    fi

    DATE=`date`
    echo "$DATE: Compiling source code"

    OUTPUT=`gcc $MMAP_READ_SOURCE -o $MMAP_READ_BINARY`
    if ! test "x$OUTPUT" = "x"; then
        echo "Clean Compilation Failed"
        echo $OUTPUT
        echo ""
        echo "******************************************"
        echo "* FAILED COMPILE TEST 2 [stage 2]"
        echo "******************************************"
        return 1
    fi

    if ! test -x "$MMAP_READ_BINARY"; then
        echo "Binary file not created!"
        echo $OUTPUT
        echo ""
        echo "******************************************"
        echo "* FAILED COMPILE TEST 2 [stage 3]"
        echo "******************************************"
        return 1
    fi

    DATE=`date`
    echo "$DATE: Compilation finished"

    stat_file "Doing stat on generated binary" $MMAP_READ_BINARY
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED COMPILE TEST 2 [stage 4]"
        echo "******************************************"
        return 1
    fi

    if ! test -c /dev/urandom; then
        echo "Skipping test because /dev/urandom does not "
        echo " exist or is not a character file!"
        return 1
    fi

    CMD="dd if=/dev/urandom of=$MMAP_READ_BINARY_DATA bs=4194304 count=1"
    timestamp "Generating random 4MB file in 1 4MB block" "$CMD"
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED COMPILE TEST 2 [stage 5]"
        echo "******************************************"
        return 1
    fi

    CMD="$MMAP_READ_BINARY $MMAP_READ_BINARY_DATA"
    timestamp "Executing MMAP Read Program on random data file" "$CMD" /dev/null
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED COMPILE TEST 2 [stage 6]"
        echo "******************************************"
        return 1
    fi

    remove_testdir $PVFS2_TESTDIR

    echo ""
    echo "******************************************"
    echo "* PASSED COMPILE TEST 2"
    echo "******************************************"
    return 0
}

#####################################
# simple symlink test functions
#####################################

symlink_test1()
{
    echo ""
    echo "******************************************"
    echo "* RUNNING SYMLINK TEST 1"
    echo "******************************************"

    setup_testdir $PVFS2_TESTDIR

    # create a test directory tree
    mkdir $PVFS2_TESTDIR/a
    mkdir $PVFS2_TESTDIR/a/b
    mkdir $PVFS2_TESTDIR/a/b/c
    if test $? -ne 0 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED SYMLINK TEST 1 [stage 1]"
        echo "******************************************"
        return 1
    fi

    # setup a test link environment
    ln -s $PVFS2_TESTDIR/a/b $PVFS2_TESTDIR/a/b-link1
    ln -s b $PVFS2_TESTDIR/a/b-link2
    ln -s $PVFS2_TESTDIR/a/b/c $PVFS2_TESTDIR/c
    ln -s $PVFS2_TESTDIR/deadlink1 $PVFS2_TESTDIR/deadlink1
    ln -s deadlink2 $PVFS2_TESTDIR/deadlink2

    # run checks to be sure our links resolve properly
    RET=`ls $PVFS2_TESTDIR/a/b $PVFS2_TESTDIR/a/b-link1 $PVFS2_TESTDIR/a/b-link2 | grep -c c`
    if test $? -ne 0 || test $RET -ne 3 ; then
        echo ""
        echo "******************************************"
        echo "* FAILED SYMLINK TEST 1 [stage 2]"
        echo "******************************************"
    fi

    RET=`cat $PVFS2_TESTDIR/deadlink1 2>&1 | grep "Too many levels"`
    if test $? -ne 0 && test "x$RET" != "x" ; then
        echo ""
        echo "******************************************"
        echo "* FAILED SYMLINK TEST 1 [stage 3]"
        echo "******************************************"
    fi

    remove_testdir $PVFS2_TESTDIR

    echo ""
    echo "******************************************"
    echo "* PASSED SYMLINK TEST 1"
    echo "******************************************"
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

if ! test -z "$ENABLE_DIRECTORY_TESTS"; then

    directory_test1

    directory_test2

    directory_test3

    directory_test4

    directory_test5

fi


if ! test -z "$ENABLE_IO_TESTS"; then

    io_test1

    io_test2

fi

if ! test -z "$ENABLE_EXECUTE_TESTS"; then

    execute_test1

fi

if ! test -z "$ENABLE_COMPILE_TESTS"; then

    compile_test1

    compile_test2

fi

if ! test -z "$ENABLE_PERMISSION_TESTS"; then

    permission_test1

    permission_test2

fi

if ! test -z "$ENABLE_SYMLINK_TESTS"; then

    symlink_test1

fi

# if the script hasn't aborted at this point, we've passed everything
echo ""
echo "****************************************************"
echo "* pvfs2-shell-test.sh completed all configured tests"
echo "****************************************************"
