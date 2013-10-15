/* 
 * (C) 2013 Clemson University
 *
 * See COPYING in top-level directory.
 */
package org.orangefs.usrint;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.ReadableByteChannel;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

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
    public synchronized void close() throws IOException {
        if (fd < 0) {
            return;
        }
        int ret = orange.posix.close(fd);
        if (ret < 0) {
            throw new IOException("close error: ret = " + ret + ", fd = " + fd);
        }
        fd = -1;
    }

    @Override
    public synchronized boolean isOpen() {
        return fd >= 0;
    }

    @Override
    public synchronized int read(ByteBuffer dst) throws IOException {
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
            }
            else {
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
        }
        else {
            OFSLOG.debug("no bytes read...EOF");
            return -1;
        }
    }

    /*
     * When this method is called, the position should equal 0, and the limit
     * should equal the capacity, via clear().
     */
    private synchronized void readOFS() throws IOException {
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

    public synchronized void seek(long pos) throws IOException {
        if (fd < 0) {
            throw new IOException("file descriptor isn't open.");
        }
        /* Reset the channelBuffer since we are seeking */
        channelBuffer.position(0).limit(0);
        long ret = orange.posix.lseek(fd, pos, pf.SEEK_SET);
        if (ret < 0 || ret != pos) {
            throw new IOException("seek error: pos = " + pos + ", ret = " + ret);
        }
    }

    /* Returns current position within the file */
    public synchronized long tell() throws IOException {
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
