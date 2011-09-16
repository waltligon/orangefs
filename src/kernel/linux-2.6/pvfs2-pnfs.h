/*
 * (C) 2001 Clemson University and The University of Chicago
 * (C) 2011 Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_PNFS_H
#define __PVFS2_PNFS_H

#include <linux/nfs4.h>
#include <linux/nfs_page.h>

#ifdef PVFS2_PNFS_SUPPORT
#define PNFS_GET_DATA_LAYOUT_MAXSIZE 1024
#define PNFS_IO_THRESHOLD 65536
#define PNFS_LAYOUT_PVFS2 4

struct pvfs2layout_mount_type {
    struct super_block* fl_sb;
    int pnfs_fs_id;
};

struct pvfs2layout_layout_type {
    int junk;
};

struct pnfs_layout_segment {
        struct list_head pls_list;
    struct pnfs_layout_range pls_range;
    atomic_t pls_refcount;
    unsigned long pls_flags;
    struct pnfs_layout_hdr *pls_layout;
    struct rpc_cred *pls_lc_cred; /* LAYOUTCOMMIT credential */
    loff_t pls_end_pos; /* LAYOUTCOMMIT write end */
};

enum pnfs_try_status {
    PNFS_ATTEMPTED     = 0,
    PNFS_NOT_ATTEMPTED = 1,
};

#define LAYOUT_NFSV4_1_MODULE_PREFIX "nfs-layouttype4"

enum {
    NFS_LAYOUT_RO_FAILED = 0,       /* get ro layout failed stop trying */
    NFS_LAYOUT_RW_FAILED,           /* get rw layout failed stop trying */
    NFS_LAYOUT_BULK_RECALL,         /* bulk recall affecting layout */
    NFS_LAYOUT_ROC,                 /* some lseg had roc bit set */
    NFS_LAYOUT_DESTROYED,           /* no new use of layout allowed */
};

/* Per-layout driver specific registration structure */
struct pnfs_layoutdriver_type {
    struct list_head pnfs_tblid;
    const u32 id;
    const char *name;
    struct module *owner;
    struct pnfs_layout_segment * (*alloc_lseg) (struct pnfs_layout_hdr *layoutid, struct nfs4_layoutget_res *lgr, gfp_t gfp_flags);
    void (*free_lseg) (struct pnfs_layout_segment *lseg);

    /* test for nfs page cache coalescing */
    int (*pg_test)(struct nfs_pageio_descriptor *, struct nfs_page *, struct nfs_page *);

    /* Returns true if layoutdriver wants to divert this request to
     * driver's commit routine.
     */
    bool (*mark_pnfs_commit)(struct pnfs_layout_segment *lseg);
    struct list_head * (*choose_commit_list) (struct nfs_page *req);
    int (*commit_pagelist)(struct inode *inode, struct list_head *mds_pages, int how);

    /*
     * Return PNFS_ATTEMPTED to indicate the layout code has attempted
     * I/O, else return PNFS_NOT_ATTEMPTED to fall back to normal NFS
     */
    enum pnfs_try_status (*read_pagelist) (struct nfs_read_data *nfs_data);
    enum pnfs_try_status (*write_pagelist) (struct nfs_write_data *nfs_data, int how);
};

extern int pnfs_register_layoutdriver(struct pnfs_layoutdriver_type *);
extern void pnfs_unregister_layoutdriver(struct pnfs_layoutdriver_type *);



#endif /* PVFS2_PNFS_SUPPORT */

#endif
