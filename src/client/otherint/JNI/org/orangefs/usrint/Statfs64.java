/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

package org.orangefs.usrint;

import java.lang.reflect.Field;

/* Class representing C struct: 'struct statfs64' */
public class Statfs64{
    long f_type;
    long f_bsize;
    long f_blocks;
    long f_bfree;
    long f_bavail;
    long f_files;
    long f_ffree;
    long f_flags;
    long f_namelen;
    long f_frsize;
    long [] f_spare = new long[5];

    /* This probably won't get called much, since we're depending upon a 
     * native method to initialize this object.
     */
    Statfs64() {

    }

    /* Generic Object Dump to String */
    public String toString() {
        StringBuilder result = new StringBuilder();
        String newLine = System.getProperty("line.separator");

        result.append(this.getClass().getName());
        result.append(" Object {");
        result.append(newLine);

        Field[] fields = this.getClass().getDeclaredFields();

        for(Field field : fields ) {
            result.append("  ");
            try {
                result.append(field.getName());
                result.append(": ");
                result.append(field.get(this));
            } catch(IllegalAccessException ex) {
                System.out.println(ex);
            }
            result.append(newLine);
        }
        result.append("}");
        return result.toString();
    }
}