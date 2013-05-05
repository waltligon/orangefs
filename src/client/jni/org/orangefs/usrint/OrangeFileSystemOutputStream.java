/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

package org.orangefs.usrint;

import java.io.IOException;
import java.io.OutputStream;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

/* An OFS compatible File Output Stream */
public class OrangeFileSystemOutputStream extends OutputStream {

    /* Interface Related Fields */
    public Orange orange;
    public PVFS2POSIXJNIFlags pf;
    public PVFS2STDIOJNIFlags sf;

    /* File Related Fields */
    public String path;
    public short replication;
    public long bufferSize;
    public long filePtr;
    public long bufferPtr;
    public boolean append;

    public static final Log OFSLOG = LogFactory
            .getLog(OrangeFileSystemOutputStream.class);

    public OrangeFileSystemOutputStream(String path, int bufferSize,
            short replication, boolean append) throws IOException {
        displayMethodInfo(true, false);

        /* Initialize Interface and Flags */
        this.orange = Orange.getInstance();
        pf = orange.posix.f;
        sf = orange.stdio.f;

        this.path = path;
        this.filePtr = 0;
        this.bufferPtr = 0;
        this.bufferSize = bufferSize;
        /* TODO: replication, also think about appends rep. */
        this.replication = (short) 0; // replication;
        this.append = append;
        String fopenMode = append ? "a" : "w";

        /* Perform fopen */
        filePtr = orange.stdio.fopen(path, fopenMode);
        if (filePtr == 0) {
            throw new IOException(path + " couldn't be opened. (fopen)");
        }
        /* Allocate Space for Buffer based on bufferSize */
        bufferPtr = orange.stdio.calloc(1, bufferSize);
        if (bufferPtr == 0) {
            throw new IOException(path
                    + "couldn't be opened. (calloc for setvbuf)");
        }
        /* Set buffering as desired */
        if (orange.stdio.setvbuf(filePtr, bufferPtr, sf._IOFBF, bufferSize) != 0) {
            throw new IOException(path + "couldn't be opened. (setvbuf)");
        }
    }

    @Override
    public synchronized void close() throws IOException {
        displayMethodInfo(true, false);

        if (filePtr == 0) {
            return;
        }
        if (orange.stdio.fclose(filePtr) != 0) {
            throw new IOException("Couldn't close stream: " + path);
        }
        filePtr = 0;
        /* Free buffer */
        if (bufferPtr != 0) {
            orange.stdio.free(bufferPtr);
        }
        bufferPtr = 0;
    }

    @Override
    public void flush() throws IOException {
        displayMethodInfo(true, false);

        int ret = orange.stdio.fflush(filePtr);
        if (ret != 0) {
            throw new IOException("Couldn't flush stream: " + path);
        }
    }

    @Override
    public void write(int b) throws IOException {
        displayMethodInfo(true, false);

        byte[] bytes = { (byte) b };
        write(bytes, 0, 1);
    }

    @Override
    public void write(byte[] b) throws IOException {
        displayMethodInfo(true, false);

        write(b, 0, b.length);
    }

    @Override
    public void write(byte b[], int off, int len) throws IOException {
        displayMethodInfo(true, false);

        if (len <= 0)
            return;

        byte c[] = new byte[len];
        System.arraycopy(b, off, c, 0, len);
        long ret = orange.stdio.fwrite(c, 1, len, filePtr);
        if (ret < len) {
            /* Check for stream error indicator */
            if (orange.stdio.ferror(filePtr) != 0) {
                orange.stdio.clearerr(filePtr);
                throw new IOException("Error: Bytes not written to file ( "
                        + ret + " of " + len + "): " + path);
            }
        }
    }

    public void displayMethodInfo(boolean showName, boolean showStack) {
        if (showName || showStack) {
            String methodName = Thread.currentThread().getStackTrace()[2]
                    .getMethodName();
            if (showName) {
                OFSLOG.debug("method=[" + methodName + "]");
            }
            if (showStack) {
                // System.out.print("\t");
                // Thread.currentThread().dumpStack();
            }
        }
    }
}
