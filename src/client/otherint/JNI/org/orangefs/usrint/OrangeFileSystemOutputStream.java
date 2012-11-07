/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

package org.orangefs.usrint;

import java.io.OutputStream;
import java.io.IOException;

//import java.nio.ByteBuffer;
//import java.io.FileNotFoundException;

/* An OFS compatible File Output Stream */
public class OrangeFileSystemOutputStream extends OutputStream {

    /* Interface Related Fields*/
    public Orange orange;
    public PVFS2POSIXJNIFlags pf;
    public PVFS2STDIOJNIFlags sf;

    /* File Related Fields */
    public String path;
    public long filePtr;
    public int fd;
    public short replication;
    public boolean append;

    public long bufferPtr;
    public long bufferSize;

    public OrangeFileSystemOutputStream (
        String path, 
        short replication, 
        boolean append) throws IOException 
    {
        displayMethodInfo(true, false);

        this.orange = Orange.getInstance();
        pf = orange.posix.f;
        sf = orange.stdio.f;

        this.path = path;
        this.replication = replication;
        this.append = append;

        /* TODO: replication */

        String fopenMode = append ? "a" : "w";

        /* Open */
        filePtr = orange.stdio.fopen(this.path, fopenMode);
        if(filePtr == 0) {
            throw new IOException(this.path + 
                " couldn't be opened. (fopen)");
        }
        
        fd = orange.stdio.fileno(filePtr);
        System.out.println("fileno fd = " + fd);
        if(fd == -1) {
            //throw new FileNotFoundException(this.path + " couldn't be opened. (Fileno)");
            throw new IOException(this.path + 
                " couldn't be opened. (fileno)");
        }

        /* TODO: Buffering? */
        //setvbuf
    }

    public synchronized void close() throws IOException {
        displayMethodInfo(true, false);

        if((filePtr == 0) && (fd == -1)) {
            return; 
        }

        /*TODO: check if this flush is redundant */
        //flush();

        int ret = orange.stdio.fclose(filePtr);
        if(ret != 0) {
            throw new IOException("Couldn't close stream: " + path);
        }
        filePtr = 0;
        fd = -1;
    }

    public void flush() throws IOException {
        displayMethodInfo(true, false);

        int ret = orange.stdio.fflush(filePtr);
        if(ret < 0) {
            throw new IOException("Couldn't flush stream: " + path);
        }
    }

    public void write(int v) throws IOException {
        displayMethodInfo(true, false);

        byte [] b = { (byte) v };
        long ret = orange.stdio.fwrite(b, (long) 1, 1, filePtr);
        if(ret != (long) 1) {
            throw new IOException("Bytes not fully written to stream: "
                + path);
        }
    }

    public void write(byte b[], int off, int len) throws IOException {
        displayMethodInfo(true, false);

        if(len <= 0)
            return;

        byte c[] = new byte[len];
        System.arraycopy(b, off, c, 0, len);
        long ret = orange.stdio.fwrite(c, (long) len, 1, filePtr);
        if(ret != (long) 1) {
            throw new IOException("Bytes not fully written to stream: " 
                + path + ". Requested write: " + len + ". Wrote: " 
                + (ret * len) + ".");
        }
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
