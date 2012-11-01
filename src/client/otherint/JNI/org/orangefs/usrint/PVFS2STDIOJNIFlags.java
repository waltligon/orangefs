/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

package org.orangefs.usrint;

import java.lang.reflect.Field;

public class PVFS2STDIOJNIFlags {

    /* Constructor is irrelevant. Call native method fillPVFS2STDIOJNIFlags */
    public PVFS2STDIOJNIFlags() {}

    /* Fields set by JNI function fill_PVFS2STDIOJNIFlags.
    * See libPVFS2STDIOJNI.c 
    */
    long SEEK_SET;
    long SEEK_CUR;
    long SEEK_END;

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