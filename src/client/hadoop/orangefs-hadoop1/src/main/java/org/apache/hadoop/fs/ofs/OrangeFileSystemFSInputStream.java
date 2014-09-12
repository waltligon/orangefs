/*
 * (C) 2012 Clemson University
 * 
 * See COPYING in top-level directory.
 */
package org.apache.hadoop.fs.ofs;

import java.io.Closeable;
import java.io.IOException;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.PositionedReadable;
import org.apache.hadoop.fs.Seekable;
import org.orangefs.usrint.OrangeFileSystemInputStream;

public class OrangeFileSystemFSInputStream extends OrangeFileSystemInputStream
        implements Closeable, Seekable, PositionedReadable {
    private FileSystem.Statistics statistics;
    public static final Log OFSLOG = LogFactory
            .getLog(OrangeFileSystemFSInputStream.class);

    /*
     * Constructor passes parameters to parent class and initializes statistics
     */
    public OrangeFileSystemFSInputStream(String path, int bufferSize,
            FileSystem.Statistics statistics)
            throws IOException {
        super(path, bufferSize);
        this.statistics = statistics;
        statistics.incrementReadOps(1);
    }

    /* *** This method declared abstract in FSInputStream *** */
    @Override
    public long getPos()
            throws IOException {
        statistics.incrementReadOps(1);
        return super.tell();
    }

    /* Override parent class implementation to include FileSystem.Statistics */
    @Override
    public synchronized int read()
            throws IOException {
        statistics.incrementReadOps(1);
        int ret = super.read();
        if (ret != -1 && statistics != null) {
            OFSLOG.debug("<<<<< OrangeFileSystemFSInputStream: int ret = "
                    + ret + " >>>>>");
            statistics.incrementBytesRead(1);
        }
        if (statistics == null) {
            OFSLOG.warn("couldn't increment statistics: statistics is null!");
        }
        return ret;
    }

    /* Override parent class implementation to include FileSystem.Statistics */
    @Override
    public synchronized int read(byte[] b)
            throws IOException {
        statistics.incrementReadOps(1);
        int ret = super.read(b);
        if (ret > 0 && statistics != null) {
            statistics.incrementBytesRead(ret);
            OFSLOG.debug("<<<<< OrangeFileSystemFSInputStream: byte[] ret = "
                    + ret + " >>>>>");
        }
        if (statistics == null) {
            OFSLOG.warn("couldn't increment statistics: statistics is null!");
        }
        return ret;
    }

    /* Override parent class implementation to include FileSystem.Statistics */
    @Override
    public synchronized int read(byte[] b, int off, int len)
            throws IOException {
        int ret = super.read(b, off, len);
        statistics.incrementReadOps(1);
        if (ret > 0 && statistics != null) {
            OFSLOG.debug("<<<<< OrangeFileSystemFSInputStream: off ret = "
                    + ret + " >>>>>");
            statistics.incrementBytesRead(ret);
        }
        if (statistics == null) {
            OFSLOG.debug("couldn't increment statistics: statistics is null!");
        }
        return ret;
    }

    /* This method has an implementation in abstract class FSInputStream */
    @Override
    public int read(long position, byte[] buffer, int offset, int length)
            throws IOException {
        long oldPos = getPos();
        seek(position);
        int ret = read(buffer, offset, length);
        seek(oldPos);
        return ret;
    }

    /* This method has an implementation in abstract class FSInputStream */
    @Override
    public void readFully(long position, byte[] buffer)
            throws IOException {
        long oldPos = getPos();
        seek(position);
        int ret = read(buffer);
        if (ret < buffer.length) {
            throw new IOException("readFully read < buffer.length bytes.");
        }
        seek(oldPos);
    }

    /* This method has an implementation in abstract class FSInputStream */
    @Override
    public void readFully(long position, byte[] buffer, int offset, int length)
            throws IOException {
        long oldPos = getPos();
        seek(position);
        int ret = read(buffer, offset, length);
        if (ret < length) {
            throw new IOException("readFully read < buffer.length bytes.");
        }
        seek(oldPos);
    }

    /* *** This method declared abstract in FSInputStream *** */
    @Override
    public synchronized void seek(long pos)
            throws IOException {
        statistics.incrementReadOps(1);
        super.seek(pos);
    }

    /* *** This method declared abstract in FSInputStream *** */
    @Override
    public synchronized boolean seekToNewSource(long targetPos)
            throws IOException {
        return false;
    }
}
