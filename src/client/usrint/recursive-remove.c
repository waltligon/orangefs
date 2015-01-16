/*
 * (C) 2014 Clemson University
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pvfs2-types.h>
#include <usrint.h>
#include <posix-pvfs.h>
#include <gossip.h>
#include <recursive-remove.h>
#include <str-utils.h>

/* Recursively delete the absolute path "dir".
 * Returns 0 on success, -1 on failure.
 */
int recursive_delete_dir(char *dir)
{
    int ret = -1;
    DIR * dirp = NULL;
    struct dirent * direntp = NULL;

    RR_PFI();
    RR_PRINT("opening dir=%s\n", dir);
    /* Open the directory specified by dir */
    dirp = opendir(dir);
    if (!dirp)
    {
        RR_PERROR("opendir failed: ");
        return -1;
    }

    /* Remove all files in the current directory */
    if (remove_files_in_dir(dir, dirp) != 0)
    {
        RR_ERROR("remove_files_in_dir failed on directory: %s\n", dir);
        return -1;
    }

    /* Rewind directory stream and call recursive delete on the directories
     * in this directory.
     */
    RR_PRINT("rewinding dirp = %p\n", dirp);
    rewinddir(dirp);
    while(1)
    {
        char abs_path[PVFS_PATH_MAX + 1];
        struct stat buf;
        
        RR_PRINT("calling readdir on dirp=%p\n", (void *) dirp);
        errno = 0;
        if((direntp = readdir(dirp)) == NULL)
        {
            if(errno != 0)
            {
                RR_PERROR("readdir failed: ");
                return -1;
            }
            break;
        }
        /* Skip the . and .. dir entries */
        if(PINT_is_dot_dir(direntp->d_name))
        {
            continue;
        }
        
        ret = PINT_merge_paths(dir, direntp->d_name, abs_path);
        if(ret < 0)
        {
            RR_ERROR("pvfs_merge_paths failed");
            return ret;
        }
        /* Determine if this entry is a file or directory. */
        RR_PRINT("calling pvfs_lstat_mask on file: %s\n", abs_path);
        ret = pvfs_lstat_mask(abs_path, &buf, PVFS_ATTR_SYS_TYPE);
        if(ret < 0)
        {
            RR_PERROR("pvfs_lstat_mask failed: ");
            return ret;
        }
        if(S_ISDIR(buf.st_mode))
        {
            ret = recursive_delete_dir(abs_path);
            if(ret < 0)
            {
                RR_ERROR("recursive_delete_dir failed on path:%s\n", abs_path);
                return -1;
            }
        }
    }

    /* Note: Behavior of recursive rm on root OrangeFS dir:
     * All files and directories under "/" will be removed,
     * but an error will be thrown on rmdir of "/". The message is:
     *
     * errno= 116  Error detected on line ___ in function recursive_delete_dir
     * rmdir failed: Illegal seek
     * recursive_delete_dir failed on path: /mnt/orangefs/
     */ 

    /* Close current directory before we attempt removal */
    RR_PRINT("closing dir: %s\n", dir);
    if (closedir(dirp) != 0)
    {
        RR_PERROR("closedir failed: ");
        return -1;
    }
    RR_PRINT("removing dir: %s\n", dir);
    if (rmdir(dir) != 0)
    {
        RR_PERROR("rmdir failed: ");
        return -1;
    }
    return 0;
}

/* Remove all files (and links) discovered in the open directory named "dir"
 * pointed to by "dirp".
 */
int remove_files_in_dir(char *dir, DIR* dirp)
{
    int ret = -1;
    struct dirent* direntp = NULL;
    
    RR_PFI();
    /* Rewind this directory stream back to the beginning. */
    RR_PRINT("rewinding dirp = %p\n", dirp);
    rewinddir(dirp);

    /* Loop over directory entries */
    while(1)
    {
        char abs_path[PVFS_PATH_MAX + 1];
        struct stat buf;

        errno = 0;
        if((direntp = readdir(dirp)) == NULL)
        {
            if(errno != 0)
            {
                RR_PERROR("readdir failed: ");
                return -1;
            }
            break;
        }
        RR_PRINT("direntp->d_name = %s\n", direntp->d_name);
        ret = PINT_merge_paths(dir, direntp->d_name, abs_path);
        if(ret < 0)
        {
            RR_ERROR("pvfs_merge_paths failed");
            return ret;
        }
        /* Determine if this entry is a file or directory. */
        ret = pvfs_lstat_mask(abs_path, &buf, PVFS_ATTR_SYS_TYPE);
        if(ret < 0)
        {
            RR_PERROR("pvfs_lstat_mask failed: ");
            RR_ERROR("abs_path = %s\n", abs_path);
            return ret;
        }
        /* Skip directories. */
        if(S_ISDIR(buf.st_mode))
        {
            continue;
        }
        /* Unlink file. */
        RR_PRINT("Unlinking file=%s\n", abs_path);
        ret = unlink(abs_path);
        if (ret == -1)
        {
            RR_PERROR("unlink failed: ");
            return ret;
        }
    }
    return 0;
}
