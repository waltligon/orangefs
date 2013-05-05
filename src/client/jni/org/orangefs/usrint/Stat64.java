/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

package org.orangefs.usrint;

import java.lang.reflect.Field;

/* Class representing a C struct: 'struct stat64' */
public class Stat64 {

    long st_dev;
    long st_ino;
    int st_mode;
    int st_nlink;
    long st_uid;
    long st_gid;
    long st_rdev;
    long st_size;
    int st_blksize;
    long st_blocks;
    long st_atime;
    long st_mtime;
    long st_ctime;

    /*
     * This probably won't get called much since we're depending upon a native
     * method to initialize this object.
     */
    Stat64() {

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
            }
            catch (IllegalAccessException ex) {
                System.out.println(ex);
            }
            result.append(newLine);
        }
        result.append("}");
        return result.toString();
    }
}