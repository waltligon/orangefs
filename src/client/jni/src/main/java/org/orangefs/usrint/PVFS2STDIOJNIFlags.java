/*
 * (C) 2011 Clemson University
 * 
 * See COPYING in top-level directory.
 */
package org.orangefs.usrint;

import java.lang.reflect.Field;

public class PVFS2STDIOJNIFlags {
    /*
     * Fields set by JNI function fill_PVFS2STDIOJNIFlags. See
     * libPVFS2STDIOJNI.c
     */

    /* seek whence */
    public long SEEK_SET;
    public long SEEK_CUR;
    public long SEEK_END;

    /* readdir d_type */
    public long DT_BLK;
    public long DT_CHR;
    public long DT_DIR;
    public long DT_FIFO;
    public long DT_LNK;
    public long DT_REG;
    public long DT_SOCK;
    public long DT_UNKNOWN;

    /* setvbuf modes */
    public long _IONBF;
    public long _IOLBF;
    public long _IOFBF;

    /* Constructor is irrelevant. Call native method fillPVFS2STDIOJNIFlags */
    public PVFS2STDIOJNIFlags() {}

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
