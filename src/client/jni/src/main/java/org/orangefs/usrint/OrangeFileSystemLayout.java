/*
 *
 */

package org.orangefs.usrint;

public enum OrangeFileSystemLayout {
    /* order the datafiles according to the server list */
    PVFS_SYS_LAYOUT_NONE(1),

    /* choose the first datafile randomly, and then round-robin in-order */
    PVFS_SYS_LAYOUT_ROUND_ROBIN(2),

    /* choose each datafile randomly */
    PVFS_SYS_LAYOUT_RANDOM(3),

    /* order the datafiles based on the list specified */
    PVFS_SYS_LAYOUT_LIST(4),

    /* TODO */
    PVFS_SYS_LAYOUT_LOCAL(5)

    /* etc */
    ;

    private int layout;

    OrangeFileSystemLayout(int layout) {
        this.layout = layout;
    }

    public int getLayout() {
        return layout;
    }
}
