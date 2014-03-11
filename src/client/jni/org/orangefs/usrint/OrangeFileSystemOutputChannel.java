package org.orangefs.usrint;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.WritableByteChannel;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

public class OrangeFileSystemOutputChannel implements WritableByteChannel {
    /* Interface Related Fields */
    private Orange orange;
    private PVFS2POSIXJNIFlags pf;
    /* Channel Related Fields */
    private int fd;
    private ByteBuffer channelBuffer;
    /* OFSLOG for logging */
    public static final Log OFSLOG = LogFactory
            .getLog(OrangeFileSystemInputChannel.class);

    public OrangeFileSystemOutputChannel(int fd, int bufferSize) {
        this.orange = Orange.getInstance();
        pf = orange.posix.f;
        this.fd = fd;
        channelBuffer = ByteBuffer.allocateDirect(bufferSize);
    }

    /* Flush the outChannel and close the file */
    @Override
    public synchronized void close() throws IOException {
        if (fd < 0) {
            return;
        }
        flush();
        int ret = orange.posix.close(fd);
        if (ret < 0) {
            throw new IOException("close failed");
        }
    }

    /* Flush what's left in the channelBuffer to the file system */
    public synchronized void flush() throws IOException {
        if (fd < 0) {
            throw new IOException("file descriptor isn't open");
        }
        channelBuffer.flip();
        if (channelBuffer.hasRemaining()) {
            long ret = orange.posix.write(fd, channelBuffer, channelBuffer
                    .remaining());
            if (ret < 0) {
                throw new IOException("write error");
            }
            channelBuffer.clear();
        }
    }

    @Override
    public synchronized boolean isOpen() {
        return fd >= 0;
    }

    public synchronized void seek(long pos) throws IOException {
        if (fd < 0) {
            throw new IOException("file descriptor isn't open.");
        }
        /* Flush the channelBuffer since we are seeking */
        flush();
        long ret = orange.posix.lseek(fd, pos, pf.SEEK_SET);
        if (ret < 0 || ret != pos) {
            throw new IOException("seek error: pos = " + pos + ", ret = " + ret);
        }
    }

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
        return ret + channelBuffer.position();
    }

    @Override
    public synchronized int write(ByteBuffer src) throws IOException {
        if (fd < 0) {
            throw new IOException("file descriptor isn't open.");
        }
        int initialSrcRemaining = src.remaining();
        int srcRemaining = 0;
        int srcPosition = 0;
        int srcLimit = 0;
        int channelBufferRemaining = 0;
        int finalSrcRemaining = 0;
        int bytesWritten = 0;
        /* Put bytes from src into channelBuffer. */
        while (src.hasRemaining()) {
            srcRemaining = src.remaining();
            srcPosition = src.position();
            // Write to OrangeFS if channelBuffer is full
            if (!channelBuffer.hasRemaining()) {
                flush();
            }
            channelBufferRemaining = channelBuffer.remaining();
            srcLimit = src.limit();
            /*
             * Limit src if necessary, put src into channel buffer, and restore
             * the limit if necessary.
             */
            if (srcRemaining > channelBufferRemaining) {
                src.limit(srcPosition + channelBufferRemaining);
                channelBuffer.put(src);
                src.limit(srcLimit);
            }
            else {
                channelBuffer.put(src);
            }
        }
        /* Return the number of bytes written. */
        finalSrcRemaining = src.remaining();
        bytesWritten = initialSrcRemaining - finalSrcRemaining;
        return bytesWritten;
    }
}
