/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

package org.orangefs.usrint;

/* This is class that allows the JNI layer of the OrangeFS Direct Client 
 * interface to be instantiated. Follows the singleton pattern. */
public class Orange {
    
    private static Orange orange = null;
    
    public PVFS2POSIXJNI posix;
    public PVFS2STDIOJNI stdio;
    
    protected Orange() {
        System.out.println("hello from Orange constructor.");
        posix = new PVFS2POSIXJNI();
        stdio = new PVFS2STDIOJNI();
    }
    
    public static Orange getInstance() {
        if(orange == null)
            return (orange = new Orange());
        
        return orange;
    }
}
