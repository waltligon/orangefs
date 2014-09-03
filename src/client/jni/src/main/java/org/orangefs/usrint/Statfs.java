/*
 * (C) 2011 Clemson University
 * 
 * See COPYING in top-level directory.
 */

package org.orangefs.usrint;

import java.lang.reflect.Field;

/* Class representing C struct: 'struct statfs' */
public class Statfs {

    long f_type;
    long f_bsize;
    long f_blocks;
    long f_bfree;
    long f_bavail;
    long f_files;
    long f_ffree;
    int f_fsid;
    long f_namelen;
    /* SHOULD always be zero since OrangeFS doesn't handle this field yet. */
    long f_frsize;

    /*
     * This probably won't get called much since we're depending upon a native
     * method to initialize this object.
     */
    Statfs() {

    }

    public long getCapacity() {
        return f_bsize;
    }

    public long getUsed() {
        return f_bfree;
    }

    public long getRemaining() {
        return f_bavail;
    }

    /* Generic Object Dump to String */
    @Override
    public String toString() {
        StringBuilder result = new StringBuilder();
        String newLine = System.getProperty("line.separator");

        result.append(this.getClass().getName());
        result.append(" Object {");
        result.append(newLine);

        Field[] fields = this.getClass().getDeclaredFields();

        for (Field field : fields) {
            result.append("  ");
            try {
                result.append(field.getName());
                result.append(": ");
                result.append(field.get(this));
            } catch (IllegalAccessException ex) {
                System.out.println(ex);
            }
            result.append(newLine);
        }
        result.append("}");
        return result.toString();
    }
}
