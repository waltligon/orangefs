# PVFS2 internal I/O API terminology

PVFS2 contains several low level interfaces for performing various types
of I/O. None of these are meant to be accessed by end users. However,
they are pervasive enough in the design that it is helpful to describe
some of their common characteristics in a single piece of documentation.

## Internal I/O interfaces

The following is a list of the lowest level APIs that share
characteristics that we will discuss here.

-   BMI (Buffered Message Interface): message based network
    communications

-   Trove: local file and database access

-   Flow: high level I/O API that ties together lower level components
    (such as BMI and Trove) in a single transfer; handles buffering and
    datatype processing

-   Dev: user level interaction with kernel device driver

-   NCAC (Network Centric Adaptive Cache): user level buffer cache that
    works on top of Trove (*currently unused*)

-   Request scheduler: handles concurrency and scheduling at the file
    system request level

## Job interface

The Job interface is a single API that binds together all of the above
components. This provides a single point for testing for completion of
any nonblocking operations that have been submitted to a lower level
API. It also handles most of the thread management where applicable.

## Posting and testing

All of the APIs listed in this document are nonblocking. The model used
in all cases is to first `post` a desired operation, then `test` until
the operation has completed, and finally check the resulting error code
to determine if the operation was successful. Every `post` results in
the creation of a unique ID that is used as an input to the `test` call.
This is the mechanism by which particular posts are matched with the
correct test.

It is also possible for certain operations to complete immediately at
post time, therefore eliminating the need to test later if it is not
required. This condition is indicated by the return code of the post
call. A return code of 0 indicates that the post was successful, but
that the caller should test for completion. A return code of 1 indicates
that the call was immediately successful, and that no test is needed.
Errors are indicated by either a negative return code, or else indicated
by an output argument that is specific to that API.

## Test variations

In a parallel file system, it is not uncommon for a client or server to
be carrying out many operations at once. We can improve efficiency in
this case by providing mechanisms for testing for completion of more
than one operation in a single function call. Each API will support the
following variants of the test function (where PREFIX depends on the
API):

-   PREFIX\_test(): This is the most simple version of the test
    function. It checks for completion of an individual operation based
    on the ID given by the caller.

-   PREFIX\_testsome(): This is an expansion of the above call. The
    difference is that it takes an array of IDs and a count as input,
    and provides an array of status values and a count as output. It
    checks for completion of any non-zero ID in the array. The output
    count indicates how many of the operations in question completed,
    which may range from 0 to the input count.

-   PREFIX\_testcontext(): This function is similar to testsome().
    However, it does not take an array of IDs as input. Instead, it
    tests for completion of *any* operations that have previously been
    posted, regardless of the ID. A count argument limits how many
    results may be returned to the caller. A context (discussed in the
    following subsection) can be used to limit the scope of IDs that may
    be accessed through this function.

## Contexts

Before posting any operations to a particular interface, the caller must
first open a `context` for that interface. This is a mechanism by which
an interface can differentiate between two different callers (ie, if
operations are being posted by more than one thread or more than one
higher level component). This context is then used as an input argument
to every subsequent post and test call. In particular, it is very useful
for the testcontext() functions, to insure that it does not return
information about operations that were posted by a different caller.

## User pointers

`User pointers` are void\* values that are passed into an interface at
post time and returned to the caller at completion time through one of
the test functions. These pointers are never stored or transmitted over
the network; they are intended for local use by the interface caller.
They may be used for any purpose. For example, it may be set to point at
a data structure that tracks the state of the system. When the pointer
is returned at completion time, the caller can then map back to this
data structure immediately without searching because it has a direct
pointer.

## Time outs and max idle time

The job interface allows the caller to specify a time out with all test
functions. This determines how long the test function is allowed to
block before returning if no operations of interest have completed.

The lower level APIs follow different semantics. Rather than a time out,
they allow the caller to specify a `max idle time`. The max idle time
governs how long the API is allowed to sleep if it is idle when the test
call is made. It is under no obligation to actually consume the full
idle time. It is more like a hint to control whether the function is a
busy poll, or if it should sleep when there is no work to do.
