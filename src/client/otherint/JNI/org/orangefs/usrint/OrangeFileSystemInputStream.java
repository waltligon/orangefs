/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

package org.orangefs.usrint;

import java.io.InputStream;
import java.io.IOException;
import java.io.Closeable;

public class OrangeFileSystemInputStream 
        extends InputStream 
        implements Closeable {

    /* Interface Related Fields*/
    public Orange orange;
    public PVFS2POSIXJNIFlags pf;
    public PVFS2STDIOJNIFlags sf;

    /* File Related Fields*/
    public String path;
    public int bufferSize;
    public long filePtr;
    public long bufferPtr;
    public long fileSize;

    /* FileSystem Statistics */
    //public FileSystem.Statistics statistics;

    public OrangeFileSystemInputStream(
            String path,
            int bufferSize) throws IOException {

        displayMethodInfo(true, false);
        
        int rc = 0;        

        this.orange = Orange.getInstance();
        pf = orange.posix.f;
        sf = orange.stdio.f;


        this.path = path;
        this.filePtr = 0;
        this.bufferPtr = 0;
        this.bufferSize = bufferSize;
        String fopenMode = "r";

        /* Get current file size by calling stat */
        Stat fileStat = orange.posix.stat(this.path);
        if(fileStat == null) {
            fileSize = 0;
        }
        else {
            fileSize = fileStat.st_size;
        }
        /* Perform fopen */
        filePtr = orange.stdio.fopen(path, fopenMode);
        if(filePtr == 0) {
            throw new IOException(path +
                " couldn't be opened. (fopen)");
        }
        /* Allocate Space for Buffer based on bufferSize */
        bufferPtr = orange.stdio.calloc(1, bufferSize);
        if(bufferPtr == 0) {
            throw new IOException(path + 
                "couldn't be opened. (calloc for setvbuf)");
        }
        /* Set buffering as desired */
        if(orange.stdio.setvbuf(filePtr, 
            bufferPtr, sf._IOFBF, bufferSize) != 0)
        {
            throw new IOException(path + "couldn't be opened. (setvbuf)");
        }
    }

    /* This method has an implementation in abstract class InputStream */
    /* */
    public synchronized int available() throws IOException {
        displayMethodInfo(true, false);
        if(filePtr == 0) {
            throw new IOException("Invalid filePtr");
        }
        return (int) (fileSize - orange.stdio.ftell(filePtr));
    }

    /* This method has an implementation in abstract class InputStream */
    public void close() throws IOException {
        displayMethodInfo(true, false);
        if(filePtr == 0) {
            return;
        }
        if(orange.stdio.fclose(filePtr) != 0) {
            throw new IOException("Couldn't close stream: " + path);
        }
        filePtr = 0;
        /* Free buffer */        
        if(bufferPtr != 0) {
            orange.stdio.free(bufferPtr);
        }
        bufferPtr = 0;
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
        if(filePtr == 0) {
            throw new IOException("Invalid filePtr");
        }
        byte [] b = new byte[1];
        int rc = read(b, 0, 1);
        if(rc == 1) {
            int retVal = (int) (0xff & b[0]);
            /* Return byte as int */
            return retVal;
        }
        /* Return EOF */
        return -1;
    }

    /* This method has an implementation in abstract class InputStream */
    public synchronized int read(byte[] b) throws IOException {
        displayMethodInfo(true, false);
        if(filePtr == 0) {
            throw new IOException("Invalid filePtr");
        }
        return read(b, 0, b.length);
    }

    /* This method has an implementation in abstract class InputStream */
    public synchronized int read(byte[] b, int off, int len) throws IOException {
        displayMethodInfo(true, false);
        if(filePtr == 0) {
            throw new IOException("Couldn't read, invalid filePtr.");
        }
        if(len == 0) {
            return 0;
        }
        byte c[] = new byte[len];
        int ret = (int) orange.stdio.fread(c, 1, (long) len, filePtr);
        if(ret < len) {
            /* Check for EOF */
            if(orange.stdio.feof(filePtr) != 0) {
                orange.stdio.clearerr(filePtr);
                return -1;
            }
            /* Check for stream error indicator */
            if(orange.stdio.ferror(filePtr) != 0) {
                orange.stdio.clearerr(filePtr);
                throw new IOException("Error: Bytes not read from file ( " +
                    ret + " of " + len + "): " + path);
            }
        }
        /*
        if(statistics != null) {
            statistics.incrementBytesRead(ret);
        }
        */
        System.arraycopy(c, 0, b, off, ret);
        return ret;
    }

    /* This method has an implementation in abstract class InputStream */
    public void reset() throws IOException {
        displayMethodInfo(true, false);
        throw new IOException("No support for marking.");
    }

    /* This method has an implementation in abstract class InputStream */
    public long skip(long n) throws IOException {
        displayMethodInfo(true, false);
        if(filePtr == 0) {
            throw new IOException("Invalid filePtr");
        }
        int rc = orange.stdio.fseek(filePtr, n, sf.SEEK_CUR);
        if(rc != 0) {
            throw new IOException("Fseek failed.");
        }
        return n;
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
