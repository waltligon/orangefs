/*
 * (C) 2011 Clemson University
 * 
 * See COPYING in top-level directory.
 */
package org.orangefs.usrint;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

/* An OFS compatible File Output Stream */
public class OrangeFileSystemOutputStream extends OutputStream {
    /* Interface Related Fields */
    private Orange orange;
    private PVFS2POSIXJNIFlags pf;
    /* File Related Fields */
    private OrangeFileSystemOutputChannel outChannel;
    private String path;
    private static final Log OFSLOG = LogFactory
            .getLog(OrangeFileSystemOutputStream.class);

    /* TODO: comments */
    public OrangeFileSystemOutputStream(String path, int bufferSize,
            short replication, long blockSize, boolean append,
            OrangeFileSystemLayout layout)
            throws IOException {
        int ret = -1;
        /* Initialize Interface and Flags */
        this.orange = Orange.getInstance();
        pf = orange.posix.f;
        this.path = path;
        /*
         * TODO: replication
         */
        /* Perform open */
        ret =
                orange.posix.openWithHints(path, (append ? pf.O_APPEND
                        : pf.O_CREAT) | pf.O_WRONLY, pf.S_IRWXU | pf.S_IRWXG
                        | pf.S_IRWXO, replication, blockSize,
                        layout.getLayout());
        if (ret < 0) {
            throw new IOException(path + " couldn't be opened. (open)");
        }
        outChannel = new OrangeFileSystemOutputChannel(ret, bufferSize);
        if (outChannel == null) {
            throw new IOException("outChannel is null");
        }
        OFSLOG.debug(path + " opened successfully. fd = " + ret
                + " , bufferSize = " + bufferSize + " , blockSize = "
                + blockSize + ", layout = " + layout.toString());
    }

    /*
     * Closes this output stream and releases any system resources associated
     * with this stream. The general contract of close is that it closes the
     * output stream. A closed stream cannot perform output operations and
     * cannot be reopened. The close method of OutputStream does nothing.
     */
    @Override
    public synchronized void close()
            throws IOException {
        if (outChannel == null) {
            return;
        }
        /* Note: flush occurs when the outChannel is closed. */
        outChannel.close();
        outChannel = null;
    }

    /*
     * Flushes this output stream and forces any buffered output bytes to be
     * written out. The general contract of flush is that calling it is an
     * indication that, if any bytes previously written have been buffered by
     * the implementation of the output stream, such bytes should immediately be
     * written to their intended destination. If the intended destination of
     * this stream is an abstraction provided by the underlying operating
     * system, for example a file, then flushing the stream guarantees only that
     * bytes previously written to the stream are passed to the operating system
     * for writing; it does not guarantee that they are actually written to a
     * physical device such as a disk drive. The flush method of OutputStream
     * does nothing.
     */
    @Override
    public void flush()
            throws IOException {
        if (outChannel == null) {
            throw new IOException("outChannel is null");
        }
        outChannel.flush();
    }

    public String getPath()
            throws IOException {
        if (outChannel == null) {
            throw new IOException("outChannel is null");
        }
        return path;
    }

    /* Returns current position within the file */
    public long tell()
            throws IOException {
        if (outChannel == null) {
            throw new IOException("outChannel is null.");
        }
        return outChannel.tell();
    }

    /*
     * Writes len bytes from the specified byte array starting at offset off to
     * this output stream. The general contract for write(b, off, len) is that
     * some of the bytes in the array b are written to the output stream in
     * order; element b[off] is the first byte written and b[off+len-1] is the
     * last byte written by this operation. If b is null, a NullPointerException
     * is thrown. If off is negative, or len is negative, or off+len is greater
     * than the length of the array b, then an IndexOutOfBoundsException is
     * thrown.
     */
    @Override
    public void write(byte b[], int off, int len)
            throws IOException {
        if (outChannel == null) {
            throw new IOException("outChannel is null");
        }
        if (b == null) {
            throw new NullPointerException("b is null");
        }
        if (off < 0 || len < 0 || (off + len) > b.length) {
            throw new IndexOutOfBoundsException(
                    "off or length is < 0; or  off + len is > b.length: off = "
                            + off + ", len = " + len);
        }
        if (off < 0) {
            return;
        }
        outChannel.write(ByteBuffer.wrap(b, off, len));
    }

    /*
     * Writes b.length bytes from the specified byte array to this output
     * stream. The general contract for write(b) is that it should have exactly
     * the same effect as the call write(b, 0, b.length).
     */
    @Override
    public void write(byte[] b)
            throws IOException {
        if (outChannel == null) {
            throw new IOException("outChannel is null");
        }
        write(b, 0, b.length);
    }

    /*
     * Writes the specified byte to this output stream. The general contract for
     * write is that one byte is written to the output stream. The byte to be
     * written is the eight low-order bits of the argument b. The 24 high-order
     * bits of b are ignored. Subclasses of OutputStream must provide an
     * implementation for this method.
     */
    @Override
    public void write(int b)
            throws IOException {
        if (outChannel == null) {
            throw new IOException("outChannel is null");
        }
        byte[] byteArray = {(byte) (b & 0x000000ff)};
        write(byteArray, 0, 1);
    }
}
