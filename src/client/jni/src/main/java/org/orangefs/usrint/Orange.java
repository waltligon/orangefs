/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */
package org.orangefs.usrint;

/* This is class that allows the JNI layer of the OrangeFS Direct Client 
 * interface to be instantiated. Follows the singleton pattern. */
public class Orange {
    private static class OrangeHolder {
        public static final Orange INSTANCE = new Orange();
    }

    @SuppressWarnings("unused")
    private static Orange orange = null;

    public static Orange getInstance() {
        return OrangeHolder.INSTANCE;
    }

    public PVFS2POSIXJNI posix;

    public PVFS2STDIOJNI stdio;

    private Orange() {
        posix = new PVFS2POSIXJNI();
        stdio = new PVFS2STDIOJNI();
    }
}
