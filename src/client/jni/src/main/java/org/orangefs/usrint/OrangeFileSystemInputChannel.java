/*
 * (C) 2013 Clemson University
 * 
 * See COPYING in top-level directory.
 */
package org.orangefs.usrint;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.ReadableByteChannel;

/* Seekable OrangeFS channel for reading bytes. */
public class OrangeFileSystemInputChannel implements ReadableByteChannel {
    /* Interface Related Fields */
    private Orange orange;
    private PVFS2POSIXJNIFlags pf;
    /* Channel Related Fields */
    private int fd;
    private int bufferSize;
    private ByteBuffer channelBuffer;
    /* OFSLOG for logging */
    public static final Log OFSLOG = LogFactory
            .getLog(OrangeFileSystemInputChannel.class);

    public OrangeFileSystemInputChannel(int fd, int bufferSize) {
        this.orange = Orange.getInstance();
        pf = orange.posix.f;
        this.fd = fd;
        this.bufferSize = bufferSize;
        channelBuffer = ByteBuffer.allocateDirect(bufferSize);
        channelBuffer.flip();
    }

    @Override
    public synchronized void close()
            throws IOException {
        if (fd < 0) {
            return;
        }
        int ret = orange.posix.close(fd);
        if (ret < 0) {
            throw new IOException("close error: ret = " + ret + ", fd = " + fd);
        }
        fd = -1;
        pf = null;
        channelBuffer.clear();
        channelBuffer = null;
    }

    /*
     * Help cleanup the unreleased file descriptors. Should find a better to do
     * this
     */
    protected void finalize()
            throws Throwable {
        try {
            if (orange != null && fd != -1) {
                orange.posix.close(fd);
                orange = null;
                fd = -1;
                pf = null;
                channelBuffer = null;
            }
        } finally {
            super.finalize();
        }
    }

    @Override
    public synchronized boolean isOpen() {
        return fd >= 0;
    }

    @Override
    public synchronized int read(ByteBuffer dst)
            throws IOException {
        if (fd < 0) {
            throw new IOException("file descriptor isn't open.");
        }
        int initialDstRemaining = dst.remaining();
        int dstRemaining = 0;
        int channelBufferRemaining = 0;
        int channelBufferLimit = 0;
        int channelBufferPosition = 0;
        int finalDstRemaining = 0;
        int bytesRead = 0;
        /* Put bytes from channelBuffer into dst */
        while (dst.hasRemaining()) {
            dstRemaining = dst.remaining();
            /* Read from OrangeFS if channelBuffer is empty */
            if (!channelBuffer.hasRemaining()) {
                /* clear buffer then readOFS */
                channelBuffer.clear();
                readOFS();
                if (channelBuffer.position() == 0) {
                    channelBuffer.limit(0);
                    break;
                }
                channelBuffer.flip();
            }
            channelBufferRemaining = channelBuffer.remaining();
            channelBufferLimit = channelBuffer.limit();
            channelBufferPosition = channelBuffer.position();
            /*
             * Potentially limit put from channelBuffer into the dst buffer, put
             * the data, then restore limit if necessary.
             */
            if (channelBufferRemaining > dstRemaining) {
                channelBuffer.limit(channelBufferPosition + dstRemaining);
                dst.put(channelBuffer);
                channelBuffer.limit(channelBufferLimit);
            } else {
                dst.put(channelBuffer);
            }
        }
        /*
         * Return the number of bytes read. Return -1 for EOF
         */
        finalDstRemaining = dst.remaining();
        bytesRead = initialDstRemaining - finalDstRemaining;
        if (initialDstRemaining == 0 || bytesRead > 0) {
            OFSLOG.debug("read: " + bytesRead);
            return bytesRead;
        } else {
            OFSLOG.debug("no bytes read...EOF");
            return -1;
        }
    }


    /*
     * This is reserved method to provide a Zero-Copy between byte array and
     * ByteBuffer
     */
    public synchronized int read(byte[] dst, int off, int len)
            throws IOException {
        if (fd < 0) {
            throw new IOException("file descriptor isn't open.");
        }
        long ret = 0;
        /*
         * Attempt to directly read bufferSize bytes from OrangeFS into Byte
         * Buffer.
         */
        if (!channelBuffer.hasRemaining() || channelBuffer.limit() == 0) {
            /* clear buffer then readOFS */
            channelBuffer.clear();
            // channelBuffer.flip();
            ret = orange.posix.read(fd, channelBuffer, len);
            channelBuffer.position((int) ret);
            channelBuffer.flip();

        }

        // OFSLOG.warn("dst[] size is: " + dst.length);
        // OFSLOG.warn("off is: " + off);
        // OFSLOG.warn("length is: " + len);
        // OFSLOG.warn("bytebuffer capacity: " + channelBuffer.capacity());
        // OFSLOG.warn("bytebuffer remaining: " + channelBuffer.remaining());
        // OFSLOG.warn("bytebuffer limit: " + channelBuffer.limit());
        // OFSLOG.warn("1 bytebuffer position: " + channelBuffer.position());

        if (ret < 0) {
            throw new IOException("orange.posix.read failed.");
        } else if (ret == 0) {
            return -1;
        } else {
            channelBuffer.get(dst, off, len);
            return (int) ret;
        }
    }

    /*
     * When this method is called, the position should equal 0, and the limit
     * should equal the capacity, via clear().
     */
    private synchronized void readOFS()
            throws IOException {
        if (fd < 0) {
            throw new IOException("file descriptor isn't open.");
        }
        /* Attempt read of bufferSize bytes from OrangeFS into channelBuffer. */
        long ret = orange.posix.read(fd, channelBuffer, bufferSize);
        if (ret < 0) {
            throw new IOException("orange.posix.read failed.");
        }
        /* Set the position to the number of bytes read. */
        channelBuffer.position((int) ret);
    }

    public synchronized void seek(long pos)
            throws IOException {
        if (fd < 0) {
            throw new IOException("file descriptor isn't open.");
        }
        /* Reset the channelBuffer since we are seeking */
        channelBuffer.position(0).limit(0);
        long ret = orange.posix.lseek(fd, pos, pf.SEEK_SET);
        if (ret < 0 || ret != pos) {
            throw new IOException("seek error:" + " pos = " + pos + ", ret = "
                    + ret);
        }
    }

    /* Returns current position within the file */
    public synchronized long tell()
            throws IOException {
        if (fd < 0) {
            throw new IOException("file descriptor isn't open.");
        }
        /*
         * Note: no seeking is performed here, so no need to reset ChannelBuffer
         */
        long ret = orange.posix.lseek(fd, 0, pf.SEEK_CUR);
        if (ret < 0) {
            throw new IOException("lseek error: ret = " + ret);
        }
        /*
         * We must offset the return of lseek by the bytes remaining in the
         * buffer.
         */
        return ret - channelBuffer.remaining();
    }
}
