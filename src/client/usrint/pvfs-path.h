/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines
 */
#ifndef PATH_H
#define PATH_H 1

#include <stdio.h>
#include <pvfs2.h>

#define PVFS_PATH_MAGIC (0xfafbfcfdfefff000)
#define PVFS_PATH_QUALIFIED (0x001)
#define PVFS_PATH_EXPANDED (0x002)
#define PVFS_PATH_RESOLVED (0x004)
#define PVFS_PATH_MNTPOINT (0x008)
#define PVFS_PATH_LOOKEDUP (0x010)
#define PVFS_PATH_FOLLOWSYM (0x020)
#define PVFS_PATH_ERROR (0x800)

#define VALID_PATH_MAGIC(p) \
            (((p)->magic & 0xffffffffffffff00) == PVFS_PATH_MAGIC)

#define PATH_QUALIFIED(p) ((p)->magic & PVFS_PATH_QUALIFIED)
#define SET_QUALIFIED(p) do{(p)->magic |= PVFS_PATH_QUALIFIED;}while(0)
#define CLEAR_QUALIFIED(p) do{(p)->magic &= ~PVFS_PATH_QUALIFIED;}while(0)

#define PATH_EXPANDED(p) ((p)->magic & PVFS_PATH_EXPANDED)
#define SET_EXPANDED(p) do{(p)->magic |= PVFS_PATH_EXPANDED;}while(0)
#define CLEAR_EXPANDED(p) do{(p)->magic &= ~PVFS_PATH_EXPANDED;}while(0)

#define PATH_RESOLVED(p) ((p)->magic & PVFS_PATH_RESOLVED)
#define SET_RESOLVED(p) do{(p)->magic |= PVFS_PATH_RESOLVED;}while(0)
#define CLEAR_RESOLVED(p) do{(p)->magic &= ~PVFS_PATH_RESOLVED;}while(0)

#define PATH_MNTPOINT(p) ((p)->magic & PVFS_PATH_MNTPOINT)
#define SET_MNTPOINT(p) do{(p)->magic |= PVFS_PATH_MNTPOINT;}while(0)
#define CLEAR_MNTPOINT(p) do{(p)->magic &= ~PVFS_PATH_MNTPOINT;}while(0)

#define PATH_LOOKEDUP(p) ((p)->magic & PVFS_PATH_LOOKEDUP)
#define SET_LOOKEDUP(p) do{(p)->magic |= PVFS_PATH_LOOKEDUP;}while(0)
#define CLEAR_LOOKEDUP(p) do{(p)->magic &= ~PVFS_PATH_LOOKEDUP;}while(0)

#define PATH_FOLLOWSYM(p) ((p)->magic & PVFS_PATH_FOLLOWSYM)
#define SET_FOLLOWSYM(p) do{(p)->magic |= PVFS_PATH_FOLLOWSYM;}while(0)
#define CLEAR_FOLLOWSYM(p) do{(p)->magic &= ~PVFS_PATH_FOLLOWSYM;}while(0)

#define PATH_ERROR(p) ((p)->magic & PVFS_PATH_ERROR)
#define SET_ERROR(p) do{(p)->magic |= PVFS_PATH_ERROR;}while(0)
#define CLEAR_ERROR(p) do{(p)->magic &= ~PVFS_PATH_ERROR;}while(0)

/**
 * A PVFS_path keeps up with an path we are trying to access and the
 * the results of processing that path.  If the path is qualified and/or
 * expanded then the path starts at the root and has had all . and /
 * elements removed (except a single / between segments).  Qualified
 * paths have not had any segment verified or resolved.  Later a resolved
 * path has the pvfs_path pointer pointing to the portion of the path
 * just past the mount point and has the fs_id filled in.  In most cases
 * we co do a lookup from there.  If the path fails to resolve or lookup
 * correctly we will fully expand the path resolving symbolic links.
 * During this process each segment is resolved and then looked up to see
 * if it is a symlink, and if so the link is read and the path futher
 * expanded.  If a segment fails to look up the process stops.  In this
 * case the last successful lookup in pvfs space will have its object
 * handle recorded and the filename poitner will point to the remaining
 * segments.  In the case where we are creating an object this might be a
 * signle segment.  If it is multiple segments it may be an error.
 *
 * the structure uspassed around using the address if the expanded path
 * buffer so that it appears to be a simple char path.  Functions compute
 * the address of the structure and check for the magic number to verify
 * they have a PVFS_path and not single char array.  This is used because
 * some of the interfaces can either be called by a higher layer
 * interface that has already done some processing, or directly by a
 * program which has not.
 */

typedef struct PVFS_path_s
{
    int rc;            /* return code of internal functions */
    char *orig_path;   /* this is the original unmodified path */
    char *pvfs_path;   /* if we resove, this points to the pvfs portion */
    PVFS_fs_id fs_id;  /* if we resolve this is the fs_id */
    PVFS_handle handle;/* if we look up a pvfs object, this is the last one */
    char *filename;    /* this is the path left after the object */
    uint64_t magic;    /* contains a magic to id the struct a flags */
    char expanded_path[PVFS_PATH_MAX + 1]; /* modified path is here */
} PVFS_path_t;

static __inline__ PVFS_path_t *PVFS_new_path(const char *orig_path)
{
    PVFS_path_t *newp;
    newp = (PVFS_path_t *)malloc(sizeof(PVFS_path_t));
    memset(newp, 0, sizeof(PVFS_path_t));
    newp->magic = PVFS_PATH_MAGIC;
    newp->orig_path = (char *)orig_path;
    newp->pvfs_path = NULL;
    newp->fs_id = 0;
    newp->handle = 0;
    newp->filename = NULL;
    return newp;
}

static __inline__ PVFS_path_t *PVFS_path_from_expanded(const char *expanded_path)
{
    return ((PVFS_path_t *)((char *)expanded_path -
             (unsigned long)((char *)(&((PVFS_path_t *)0)->expanded_path))));
}

static __inline__ void PVFS_free_path(PVFS_path_t *path)
{
    memset(path, 0, sizeof(PVFS_path_t));
    free(path);
}

static __inline__ void PVFS_free_expanded(const char *path)
{
    if (path)
    {
        PVFS_path_t *Ppath = PVFS_path_from_expanded((char *)path);
        if (VALID_PATH_MAGIC(Ppath))
        {
            PVFS_free_path(Ppath);
        }
    }
}

char *PVFS_qualify_path(const char *path);

char *PVFS_expand_path(const char *path, int skip_last_lookup);

int is_pvfs_path(const char **path, int skip_last_lookup);

int split_pathname( const char *path,
                    char **directory,
                    char **filename);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
