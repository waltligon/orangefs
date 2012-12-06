/* 
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */

package org.apache.hadoop.fs.ofs;

import java.io.IOException;
import org.orangefs.usrint.*;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Seekable;
import org.apache.hadoop.fs.PositionedReadable;
import java.io.Closeable;

public class OrangeFileSystemFSInputStream 
        extends OrangeFileSystemInputStream 
        implements Closeable, Seekable, PositionedReadable {

    private FileSystem.Statistics statistics;

    /* Constructor Passes Params to Parent Class and initializes stats */
    public OrangeFileSystemFSInputStream(
            String path,
            int bufferSize,
            FileSystem.Statistics statistics) throws IOException {
        /* Initialize Parent Class Using Params */
        super(path, bufferSize);
        displayMethodInfo(true, false);
        this.statistics = statistics;
    }

    /* Override parent class implementation to include FileSystem.Statistics */
    public synchronized int read() throws IOException {
        displayMethodInfo(true, false);
        int ret = super.read();
        if(ret != -1 && statistics != null) {
            System.out.println("<<<<< OrangeFileSystemFSInputStream: int ret = " + ret + " >>>>>");
            statistics.incrementBytesRead(1);
        }
        if(statistics == null) {
            System.out.println("couldn't increment statistics: statistics is null!");
        }
        return ret;
    }

    /* Override parent class implementation to include FileSystem.Statistics */
    public synchronized int read(byte[] b) throws IOException {
        displayMethodInfo(true, false);
        int ret = super.read(b);
        if(ret > 0 && statistics != null) {
            statistics.incrementBytesRead(ret);
            System.out.println("<<<<< OrangeFileSystemFSInputStream: byte[] ret = " + ret + " >>>>>");
        }
        if(statistics == null) {
            System.out.println("couldn't increment statistics: statistics is null!");
        }

        return ret;
    }

    /* Override parent class implementation to include FileSystem.Statistics */
    public synchronized int read(byte[] b, int off, int len) throws IOException {
        displayMethodInfo(true, false);
        int ret = super.read(b, off, len);
        if(ret > 0 && statistics != null) {
            System.out.println("<<<<< OrangeFileSystemFSInputStream: off ret = " + ret + " >>>>>");
            statistics.incrementBytesRead(ret);
        }
        if(statistics == null) {
            System.out.println("couldn't increment statistics: statistics is null!");
        }
        return ret;
    }

    /* *** This method declared abstract in FSInputStream *** */
    public long getPos() throws IOException {
        displayMethodInfo(true, false);
        if(filePtr == 0) {
            throw new IOException("Invalid filePtr");
        }
        long rc = orange.stdio.ftell(filePtr);
        if(rc < 0) {
            throw new IOException("getPos failed on file: " + path);
        }
        return rc;
    }

    /* *** This method declared abstract in FSInputStream *** */
    public synchronized void seek(long pos) throws IOException {
        displayMethodInfo(true, false);
        if(filePtr == 0) {
            throw new IOException("Invalid filePtr");
        }
        if(orange.stdio.fseek(filePtr, pos, sf.SEEK_SET) != 0) {
            throw new IOException("seek failed on file: " + path);
        }
    }

    /* *** This method declared abstract in FSInputStream *** */
    public synchronized boolean seekToNewSource(long targetPos) throws IOException {
        displayMethodInfo(true, false);
        return false;
    }

    /* This method has an implementation in abstract class FSInputStream */
    public int read(long position,
                byte[] buffer,
                int offset,
                int length)
            throws IOException {
        displayMethodInfo(true, false);
        /* TODO */
        seek(position);
        return read(buffer, offset, length);
    }

    /* This method has an implementation in abstract class FSInputStreama */
    public void readFully(long position,
                      byte[] buffer)
               throws IOException {
        displayMethodInfo(true, false);
        /* TODO */
        seek(position);
        read(buffer);
    }

    /* This method has an implementation in abstract class FSInputStream */
    public void readFully(long position,
                      byte[] buffer,
                      int offset,
                      int length)
               throws IOException {
        displayMethodInfo(true, false);
        /* TODO */
        seek(position);
        read(buffer, offset, length);
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
