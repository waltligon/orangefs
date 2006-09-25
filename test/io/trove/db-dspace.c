#include <db.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "pvfs2-test-config.h"
#include "pvfs2-types.h"
#include "pvfs2-internal.h"

struct PVFS_ds_storedattr_s
{
    PVFS_ds_type type;
    PVFS_fs_id fs_id;
    PVFS_handle handle;
    PVFS_uid uid;
    PVFS_gid gid;
    PVFS_permissions mode;
    int32_t __pad1;

    PVFS_time ctime;
    PVFS_time mtime;
    PVFS_time atime;
    uint32_t dfile_count;
    uint32_t dist_size;
};

static void do_print_attr(struct PVFS_ds_storedattr_s *attr, PVFS_handle h)
{
	printf("Handle %lld\n", lld(h));
	printf("Type %s\n", attr->type == PVFS_TYPE_METAFILE ? "Metafile" :
							  attr->type == PVFS_TYPE_DATAFILE ? "Datafile" :
							  attr->type == PVFS_TYPE_DIRECTORY ? "Directory" :
							  attr->type == PVFS_TYPE_SYMLINK ? "Symlink" :
							  attr->type == PVFS_TYPE_DIRDATA ? "Dirdata" : "Unknown");
	printf("uid %d, gid %d\n", attr->uid, attr->gid);
	return;
}

static int PINT_trove_dbpf_ds_attr_compare(
    DB * dbp, const DBT * a, const DBT * b)
{
    const PVFS_handle * handle_a;
    const PVFS_handle * handle_b;

	 if (a->size > b->size)
		 return 1;
	 if (a->size < b->size)
		 return -1;
    handle_a = (const PVFS_handle *) a->data;
    handle_b = (const PVFS_handle *) b->data;

    if(*handle_a == *handle_b)
    {
        return 0;
    }

    return (*handle_a < *handle_b) ? -1 : 1;
}

int main(int argc, char *argv[])
{
	DB *db;
	DBT key, data;
	DBC *db_c;
	int ret;
	PVFS_handle handle = 0;
	char *fname = NULL;
	struct PVFS_ds_storedattr_s ds;

	if (argc != 2 && argc != 3)
	{
		fprintf(stderr, "Usage: %s <db file> {handle}\n", argv[0]);
		exit(1);
	}
	fname = strdup(argv[1]);
	if (argc == 3)
	{
		char *ptr;
		handle = strtoll(argv[2], &ptr, 10);
		if (*ptr != '\0') {
			fprintf(stderr, "Usage: %s <db file> {handle}\n", argv[0]);
			exit(1);
		}
	}
	if ((ret = db_create(&db, NULL, 0)) != 0)
	{
		fprintf(stderr, "db_create: %s\n", db_strerror(ret));
		exit(1);
	}
	db->set_bt_compare(db, &PINT_trove_dbpf_ds_attr_compare);
	if ((ret = db->set_flags(db, DB_RECNUM)) != 0)
	{
		fprintf(stderr, "db->set_flags: %s\n", db_strerror(ret));
		exit(1);
	}
	db->set_errfile(db, stderr);
	db->set_errpfx(db, "pvfs2");
#if 0
	/* open the database */
	if ((ret = db->open(db, 
			    NULL, fname, NULL, DB_UNKNOWN,  
			    DB_DIRTY_READ | 
			    DB_THREAD, 0)) != 0)
	{
		fprintf(stderr, "db->open: %s\n", db_strerror(ret));
		exit(1);
	}
#endif
	if ((ret = db->cursor(db, NULL, &db_c, 0)) != 0)
	{
		fprintf(stderr, "db->cursor: %s\n", db_strerror(ret));
		exit(1);
	}
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	if (handle)
	{
		memset(&key, 0, sizeof(key));
		key.data = &handle;
		key.size = key.ulen = sizeof(PVFS_handle);

		memset(&data, 0, sizeof(data));
		memset(&ds, 0, sizeof(ds));
		data.data = &ds;
		data.size = data.ulen = sizeof(ds);
		data.flags |= DB_DBT_USERMEM;

		if ((ret = db->get(db, NULL, &key, &data, 0)) != 0)
		{
			fprintf(stderr, "db->get: get failed: %s\n", db_strerror(ret));
			exit(1);
		}
		do_print_attr(&ds, handle);
	}
	else {
		for (;;) 
		{
			struct PVFS_ds_storedattr_s *pds;
			memset(&key, 0, sizeof(key));
			key.data = &handle;
			key.size = key.ulen = sizeof(PVFS_handle);
			key.flags |= DB_DBT_USERMEM;

			memset(&data, 0, sizeof(data));
			data.flags |= DB_DBT_MALLOC;

			ret = db_c->c_get(db_c, &key, &data, DB_NEXT);
			if (ret == DB_NOTFOUND)
			{
				break;
			}
			else if (ret != 0)
			{
				fprintf(stderr, "db_c->c_get: %s\n", db_strerror(ret));
				break;
			}
			pds = data.data;
			do_print_attr(pds, handle);
			if (data.data)
				free(data.data);
		}
	}
	db->close(db, 0);
	return 0;
}
