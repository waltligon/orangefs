/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

package org.orangefs.usrint;

import java.io.InputStream;
import java.io.IOException;
import org.orangefs.usrint.*;

public class OrangeFileSystemInputStream extends InputStream {

    /* Interface Related Fields*/
    public Orange orange;
    public PVFS2POSIXJNIFlags pf;
    public PVFS2STDIOJNIFlags sf;

    /* File Related Fields*/
    public String path;
    public long filePtr;
    public long bufferPtr;
    public int bufferSize;

    public OrangeFileSystemInputStream(
            String path,
            int bufferSize) throws IOException {

        displayMethodInfo(true, false);
        
        this.orange = Orange.getInstance();
        pf = orange.posix.f;
        sf = orange.stdio.f;

        this.path = path;
        this.bufferSize = bufferSize;
        int rc = 0;
        String fopenMode = "r";

        filePtr = orange.stdio.fopen(this.path, fopenMode);
        if(filePtr == 0) {
            throw new IOException(this.path +
                " couldn't be opened. (fopen)");
        }
    }

    /* This method has an implementation in abstract class InputStream */
    public int available() throws IOException {
        displayMethodInfo(true, false);
        if(filePtr == 0)
            throw new IOException("Invalid filePtr");
        return 0;
    }

    /* This method has an implementation in abstract class InputStream */
    public void close() throws IOException {
        displayMethodInfo(true, false);
        if(filePtr == 0) {
            return;
        }

        int ret = orange.stdio.fclose(filePtr);
        if(ret != 0) {
            throw new IOException("Couldn't close stream: " + path);
        }

        filePtr = 0;
    }

    /* This method has an implementation in abstract class InputStream */
    public void mark(int readLimit) {
        displayMethodInfo(true, false);
    }

    /* This method has an implementation in abstract class InputStream */
    public boolean markSupported() {
        displayMethodInfo(true, false);
        return false;
    }

    /* *** This method declared abstract in InputStream *** */
    public synchronized int read() throws IOException {
        displayMethodInfo(true, false);
        byte [] b = new byte[1];
        long ret = orange.stdio.fread(b, (long) 1, 1, filePtr);

        if(ret != (long) 1) {
            if(orange.stdio.feof(filePtr) != 0) {
                System.out.println("read reached EOF on path: " + path);

                orange.stdio.clearerr(filePtr);
                return -1;
            }

            if(orange.stdio.ferror(filePtr) != 0) {
                orange.stdio.clearerr(filePtr);
                throw new IOException("Error: Byte not written to stream: "
                   + path);
            }
        }
        return (int) b[0];
    }

    public synchronized int read(byte[] b) throws IOException {
        displayMethodInfo(true, false);
        //fill the byte and should return 1 or -1
        //b = orange.posix.fread.(this.ptr);
        return 0;
    }

    /* This method has an implementation in abstract class InputStream */
    public synchronized int read(byte[] b, int off, int len) throws IOException {
        displayMethodInfo(true, false);
        int ret = 0;

        if(len == 0) {
            return 0;
        }

        byte c[] = new byte[len];
        ret = (int)orange.stdio.fread(c, (long) len, 1, filePtr);
        if(ret != 1) {
            if(orange.stdio.feof(filePtr) != 0) {
                orange.stdio.clearerr(filePtr);
                return -1;
            }
            if(orange.stdio.ferror(filePtr) != 0) {
                orange.stdio.clearerr(filePtr);
                throw new IOException("Error: Bytes not read from file ( " +
                    ret + " of " + len + "): " + path);
            }
        }
        System.arraycopy(c, 0, b, off, len);
        return ret * len;
    }

    /* This method has an implementation in abstract class InputStream */
    public void reset() throws IOException {
        displayMethodInfo(true, false);
        throw new IOException("No marking support");
    }

    /* This method has an implementation in abstract class InputStream */
    public long skip(long n) throws IOException {
        displayMethodInfo(true, false);
        return 0;
    }
    public void displayMethodInfo(boolean showName, boolean showStack) {
        if(showName || showStack) {
            String methodName =
                Thread.currentThread().getStackTrace()[2].getMethodName();
            if(showName) {
                System.out.println("method=[" + methodName + "]");
            }
            if(showStack) {
                System.out.print("\t");
                Thread.currentThread().dumpStack();
            }
        }
    }
}
