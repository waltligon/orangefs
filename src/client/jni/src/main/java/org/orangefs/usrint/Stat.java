/*
 * (C) 2011 Clemson University
 * 
 * See COPYING in top-level directory.
 */
package org.orangefs.usrint;

import java.lang.reflect.Field;

/* Class representing C struct: 'struct stat' */
public class Stat {
    public long st_dev;
    public long st_ino;
    public int st_mode;
    public int st_nlink;
    public long st_uid;
    public long st_gid;
    public long st_rdev;
    public long st_size;
    public int st_blksize;
    public long st_blocks;
    public long st_atime;
    public long st_mtime;
    public long st_ctime;

    /*
     * This probably won't get called much since we're depending upon a native
     * method to initialize this object.
     */
    Stat() {}

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
