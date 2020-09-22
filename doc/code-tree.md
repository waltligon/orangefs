# The code tree

In this section we describe how the code tree is set up for PVFS2 and
discuss a little about how the build system works.

## The top level

At the top level we see:

  - `doc`

  - `examples`

  - `include`

  - `lib`

  - `maint`

  - `src`

  - `test`

The `doc` directory rather obviously holds documentation, mostly written
in LaTeX. There are a few subdirectories under `doc`. The `coding`
subdirectory has a document describing guidelines for writing code for
the project. The `design` subdirectory has a number of documents
describing various components of the system and APIs and more
importantly currently houses the Quick Start.

Much of the documentation is out of date.

`examples` currently holds two example server configuration files and
that is it.

`include` holds include files that are both used all over the system and
are eventually installed on the system during a `make install`. Any
prototypes or defines that are needed by clients using the API should be
in one of the include files in this directory.

`lib` is empty. It holds `libpvfs2.a` when it is built, prior to
installation, if you are building in-tree. More on out-of-tree builds
later.

`maint` holds a collection of scripts. Some of these are used in the
build process, while others are used to check for the presence of
inappropriately named symbols in the resulting library or reformat code
that doesn’t conform to the coding standard.

`src` holds the source code to the majority of PVFS2, including the
server, client library, Linux 2.6.x kernel module, and management tools.
We’ll talk more about this one in a subsequent subsection.

`test` holds the source code to many many tests that have been built
over time to validate the PVFS2 implementation. We will discuss this
more in a subsequent subsection as well.

## `src`

The `src` directory contains the majority of the PVFS2 distribution.

Unlike PVFS1, where the PVFS kernel code was in a separate package from
the “core,” in PVFS2 both the servers, client API, and kernel-specific
code are packaged together.

`src/common` holds a number of components shared between clients and
servers. This includes:

  - dotconf – a configuration file parser

  - gen-locks – an implementation of local locks used to provide atomic
    access to shared structures in the presence of threads

  - id-generator – a simple system for generating unique references
    (ids) to data structures

  - llist – a linked-list implementation

  - gossip – our logging component

  - quicklist – another linked-list implementation

  - quickhash – a hash table implementation

  - statecomp – the parser for our state machine description language
    (discussed subsequently)

  - misc – leftovers, including some state machine code, config file
    manipulation code, some string manipulation utilities, etc.

`src/apps` holds applications associated with PVFS2. The
`src/apps/admin` subdirectory holds a collection of tools for setting
up, monitoring, and manipulating files on a PVFS2 file system.
`pvfs2-genconfig` is used to create configuration files. `pvfs2-cp` may
be used to move files on and off PVFS2 file systems. `pvfs2-ping` and
`pvfs2-statfs` may be used to check on the status of a PVFS2 file
system. `pvfs2-ls` is an `ls` implementation for PVFS2 file systems that
does not require that the file system be mounted.

The `src/apps/karma` subdirectory contains a gtk-based gui for
monitoring a PVFS2 file system in real time. The
`src/apps/kernel/linux-2.6` subdirectory contains the user space
component of the PVFS2 kernel driver implementation, which matches the
kernel driver code found in `src/kernel/linux-2.6`. The `src/apps/vis`
contains experimental code for performance visualization.

`src/client` holds code for the “system interface” library, the lowest
level library used on the client side for access. This is in the
`src/client/sysint` subdirectory. The `unix-io` subdirectory is no
longer used. Note that there is other code used on the client side: the
ROMIO components (included in MPICH and MPICH2) and the kernel support
code (located in `src/kernel`, discussed subsequently).

Note that the ROMIO support for PVFS2 is included in MPICH1, MPICH2, and
ROMIO distributions and is not present anywhere in this tree.

`src/server` holds code for the pvfs2-server. The request scheduler code
is split into its own subdirectory for no particular reason.

`src/proto` holds code for encoding and decoding our over-the-wire
protocol. Currently the “encoding scheme” used is the *contig* scheme,
stored in its own subdirectory. This encoding scheme really just puts
the bytes into a contiguous region, so it is only good for homogeneous
systems or systems with the same byte orders where we have correctly
padded all the structures (which we probably haven’t).

`src/kernel` holds implementations of kernel support. Currently there is
only one, `src/kernel/linux-2.6`.

`src/io` holds enough code that we’ll just talk about it in its own
subsection.

## `src/io`

This directory holds all the code used to move data across the wire, to
store and retrieve data from local resources, to buffer data on servers,
and to describe I/O accesses and physical data distribution.

`bmi` holds the Buffered Messaging Interface (BMI) implementations. The
top-level directory holds code for mapping to the various underlying
implementations and defining common data structures. Subdirectories hold
implementations for GM (`bmi_gm`), TCP/IP (`bmi_tcp`), and InfiniBand
using either the VAPI or OpenIB API (`bmi_ib`).

`buffer` holds the implementation of our internal buffering and caching
component. At the time of writing this is not complete and is not
enabled.

`dev` holds code that understands how to move data through a device file
that is used by our Linux 2.6 kernel module. This is stored in this
directory because it is hooked under the *job* component.

`job` holds the job component. This component is responsible for
providing us with a common mechanism for queueing and testing for
completion of operations on a variety of different resources, including
all BMI, Trove, and the device listed above.

`description` holds code used to describe I/O accesses and physical data
distributions.

`trove` holds implementations of the Trove storage interface. The
top-level directory holds code for mapping to the various underlying
implementations and defining common data structures. Currently there is
only a single implementation of Trove, called DBPF (for DB Plus Files).
This implementation builds on Berkeley DB and local UNIX files for
storing local data.

`flow` holds the Flow component implementations. These components are
responsible for ferrying data between different types of *endpoints*.
Valid endpoints include BMI, Trove, memory, and the buffer cache.

## `test`

This directory holds a great deal of test code, most of which is useless
to the average user.

`test/client/sysint` has a collection of tests we have used when
implementing (or reimplementing) various system interface functions.

`test/correctness/pts` holds the PVFS Test Suite (PTS), a suite designed
for testing the correctness of PVFS under various different conditions.
There are actually quite a few tests in here, and the vision is that we
will run these in an automated fashion relatively often (but we aren’t
there quite yet). This is probably the second most useful code (after
pvfs2-client) in the `test` directory.

## State machines and `statecomp`

The PVFS2 source is heavily dependent on a state machine implementation
that is included in the tree. We’ve already noted that the parser,
statecomp, is located in the `src/common/statecomp` subdirectory.
Additional code for processing state machines is in `src/common/misc`.

State machine source is denoted with a `.sm` suffix. These are converted
to `.c` files by statecomp. If you are building out of tree, the `.c`
files will end up in the build tree; otherwise you’ll be in the
confusing situation of having both versions in the same subdirectory. If
modifying these, be careful to only modify the `.sm` files – the
corresponding `.c` file can be overwritten on rebuilds.

## Build system

The build system relies on the “single makefile” concept that was
promoted by someone or another other than us (we should have a
reference). Overall we’re relatively happy with it.

We also adopted the Linux 2.6 kernel style of obfuscating the actual
compile lines. This can be irritating if you’re trying to debug the
build system. It can be turned off with a “make V=1”, which makes the
build verbose again. This is controlled via a variable called
`QUIET_COMPILE` in the makefile, if you are looking for how this is
done.

## Out-of-tree builds

Some of the developers are really fond of out-of-tree builds, while
others aren’t. Basically the idea is to perform the build in a separate
directory so that the output from the build process doesn’t clutter up
the source tree.

This can be done by executing `configure` from a separate directory. For
example:

    # tar xzf pvfs2-0.0.2.tgz
    # mkdir BUILD-pvfs2
    # cd BUILD-pvfs2
    # ../pvfs2/configure
