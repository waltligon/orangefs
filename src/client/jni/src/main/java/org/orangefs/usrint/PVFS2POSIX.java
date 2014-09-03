package org.orangefs.usrint;


import java.io.IOException;

/* TODO use this interface when complete... */
public interface PVFS2POSIX {
    PVFS2POSIXJNIFlags f = null;

    int close(int fd);

    int creat(String path, long mode);

    long lseek(int fd, long offset, long whence);

    int open(String path, long flags, long mode);

    int openWrapper(String path, long flags, long mode)
            throws IOException;

    PVFS2POSIXJNIFlags fillPVFS2POSIXJNIFlags();
}
