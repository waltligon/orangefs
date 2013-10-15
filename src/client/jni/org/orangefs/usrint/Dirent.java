/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

package org.orangefs.usrint;

import java.lang.reflect.Field;

/* Class representing C struct: 'struct dirent' */
public class Dirent {

    long d_ino;
    long d_off;
    int d_reclen;
    String d_type;
    String d_name;

    /*
     * This probably won't get called much since we're depending upon a native
     * method to initialize this object.
     */
    Dirent() {

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