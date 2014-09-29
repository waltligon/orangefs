/*
 * (C) 2012 Clemson University
 * 
 * See COPYING in top-level directory.
 */
package org.orangefs.usrint;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;

public class OrangeFileSystemInputStream extends InputStream implements
        Closeable {
    /* Interface Related Fields */
    private Orange orange = Orange.getInstance();
    private PVFS2POSIXJNIFlags pf = orange.posix.f;
    private static final Log OFSLOG = LogFactory
            .getLog(OrangeFileSystemInputStream.class);
    /* File Related Fields */
    private OrangeFileSystemInputChannel inChannel;
    private String path;
    private long fileSize;

    public OrangeFileSystemInputStream(String path, int bufferSize)
            throws IOException {
        int fd = -1;
        this.path = path;
        /* Perform open */
        fd = orange.posix.open(path, pf.O_RDONLY, 0);
        if (fd < 0) {
            throw new IOException(path + " couldn't be opened.");
        }
        /* Obtain the fileSize */
        fileSize = orange.posix.lseek(fd, 0, pf.SEEK_END);
        if (fileSize < 0 || orange.posix.lseek(fd, 0, pf.SEEK_SET) < 0) {
            throw new IOException("Error determining fileSize: lseek: "
                    + fileSize);
        }
        /* Instantiate the input channel object */
        inChannel = new OrangeFileSystemInputChannel(fd, bufferSize);
    }

    @Override
    public synchronized int available()
            throws IOException {
        if (inChannel == null) {
            throw new IOException("InputChannel is null.");
        }
        return (int) (fileSize - inChannel.tell());
    }

    @Override
    public synchronized void close()
            throws IOException {
        if (inChannel == null) {
            return;
        }
        inChannel.close();
        inChannel = null;
    }

    public String getPath()
            throws IOException {
        return path;
    }

    @Override
    public void mark(int readLimit) {}

    @Override
    public boolean markSupported() {
        return false;
    }

    @Override
    public synchronized int read()
            throws IOException {
        byte[] b = new byte[1];
        int rc = read(b, 0, 1);
        if (rc == 1) {
            int retVal = 0xff & b[0];
            /* Return byte as int */
            return retVal;
        }
        /* Return EOF */
        return -1;
    }

    @Override
    public synchronized int read(byte[] b)
            throws IOException {
        return read(b, 0, b.length);
    }

    @Override
    public synchronized int read(byte[] b, int off, int len)
            throws IOException {
        if (inChannel == null) {
            throw new IOException("InputChannel is null.");
        }
        if (len == 0) {
            return 0;
        }
        int ret = inChannel.read(ByteBuffer.wrap(b, off, len));
        OFSLOG.debug("inChannel.read ret = " + ret);
        if (ret <= 0) {
            OFSLOG.debug("Nothing read -> " + ret + " / " + len);
            return -1;
        }
        return ret;
    }

    @Override
    public void reset()
            throws IOException {
        throw new IOException("No support for marking.");
    }

    public synchronized void seek(long pos)
            throws IOException {
        if (inChannel == null) {
            throw new IOException("InputChannel is null.");
        }
        if (pos >= fileSize) {
            throw new IOException("Attempted to seek past EOF: fileSize = "
                    + fileSize + ", pos = " + pos);
        }
        inChannel.seek(pos);
    }

    public synchronized boolean seekToNewSource(long targetPos)
            throws IOException {
        return false;
    }

    @Override
    public synchronized long skip(long n)
            throws IOException {
        if (n < 0) {
            return 0;
        }
        if (inChannel == null) {
            throw new IOException("InputChannel is null.");
        }
        long fileBytesAvailable = available();
        if (n > fileBytesAvailable) {
            n = fileBytesAvailable;
        }
        inChannel.seek(n);
        return n;
    }

    /* Returns current position within the file */
    public long tell()
            throws IOException {
        if (inChannel == null) {
            throw new IOException("InputChannel is null.");
        }
        return inChannel.tell();
    }
}
