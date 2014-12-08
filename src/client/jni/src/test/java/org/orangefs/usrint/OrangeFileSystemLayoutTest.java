/*
 * (C) 2014 Clemson University
 * 
 * See COPYING in top-level directory.
 */

package org.orangefs.usrint;

import org.junit.*;
import static org.junit.Assert.*;

public class OrangeFileSystemLayoutTest {

    @Test
    public void testGetLayout() {
        assertEquals("(PVFS_SYS_LAYOUT_NONE == 1)", 1,
                OrangeFileSystemLayout.PVFS_SYS_LAYOUT_NONE.getLayout());
        assertEquals("(PVFS_SYS_LAYOUT_ROUND_ROBIN == 2)", 2,
                OrangeFileSystemLayout.PVFS_SYS_LAYOUT_ROUND_ROBIN.getLayout());
        assertEquals("(PVFS_SYS_LAYOUT_RANDOM == 3)", 3,
                OrangeFileSystemLayout.PVFS_SYS_LAYOUT_RANDOM.getLayout());
        assertEquals("(PVFS_SYS_LAYOUT_LIST == 4)", 4,
                OrangeFileSystemLayout.PVFS_SYS_LAYOUT_LIST.getLayout());
        assertEquals("(PVFS_SYS_LAYOUT_LOCAL == 5)", 5,
                OrangeFileSystemLayout.PVFS_SYS_LAYOUT_LOCAL.getLayout());
    }

    @Test
    public void testLayoutUsingValueOf() {
        assertEquals("PVFS_SYS_LAYOUT_NONE",
                OrangeFileSystemLayout.PVFS_SYS_LAYOUT_NONE,
                OrangeFileSystemLayout.valueOf("PVFS_SYS_LAYOUT_NONE"));
        assertEquals("PVFS_SYS_LAYOUT_ROUND_ROBIN",
                OrangeFileSystemLayout.PVFS_SYS_LAYOUT_ROUND_ROBIN,
                OrangeFileSystemLayout.valueOf("PVFS_SYS_LAYOUT_ROUND_ROBIN"));
        assertEquals("PVFS_SYS_LAYOUT_RANDOM",
                OrangeFileSystemLayout.PVFS_SYS_LAYOUT_RANDOM,
                OrangeFileSystemLayout.valueOf("PVFS_SYS_LAYOUT_RANDOM"));
        assertEquals("PVFS_SYS_LAYOUT_LIST",
                OrangeFileSystemLayout.PVFS_SYS_LAYOUT_LIST,
                OrangeFileSystemLayout.valueOf("PVFS_SYS_LAYOUT_LIST"));
        assertEquals("PVFS_SYS_LAYOUT_LOCAL",
                OrangeFileSystemLayout.PVFS_SYS_LAYOUT_LOCAL,
                OrangeFileSystemLayout.valueOf("PVFS_SYS_LAYOUT_LOCAL"));
    }


}
