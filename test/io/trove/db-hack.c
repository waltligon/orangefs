#include <db.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "pvfs2-test-config.h"

int main(int argc, char *argv[])
{
	DB *db;
	DBC *db_c;
	DBT key, data;
	int ret;
	db_recno_t offset = 0;
	char *fname = NULL;

	if (argc != 2 && argc != 3)
	{
		fprintf(stderr, "Usage: %s <db file> {offset}\n", argv[0]);
		exit(1);
	}
	fname = strdup(argv[1]);
	if (argc == 3)
	{
		offset = atoi(argv[2]);
	}
	if ((ret = db_create(&db, NULL, 0)) != 0)
	{
		fprintf(stderr, "db_create: %s\n", db_strerror(ret));
		exit(1);
	}
	if ((ret = db->set_flags(db, DB_RECNUM)) != 0)
	{
		fprintf(stderr, "db->set_flags: %s\n", db_strerror(ret));
		exit(1);
	}
	db->set_errfile(db, stderr);
	db->set_errpfx(db, "pvfs2");
	/* open the database */
	if ((ret = db->open(db, 
			    NULL, fname, NULL, DB_UNKNOWN,  
#ifdef HAVE_DB_DIRTY_READ
			    DB_DIRTY_READ | 
#endif
			    DB_THREAD, 0)) != 0)
	{
		fprintf(stderr, "db->open: %s\n", db_strerror(ret));
		exit(1);
	}
	if ((ret = db->cursor(db, NULL, &db_c, 0)) != 0)
	{
		fprintf(stderr, "db->cursor: %s\n", db_strerror(ret));
		exit(1);
	}
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	if (offset)
	{
		key.data = (char *) malloc(16);
		key.size = key.ulen = 16;
		key.flags |= DB_DBT_USERMEM;
		*(int32_t*) key.data = offset;

		data.flags |= DB_DBT_MALLOC;
		if ((ret = db_c->c_get(db_c, &key, &data, DB_SET_RECNO)) != 0)
		{
			fprintf(stderr, "db_c->c_get: could not position cursor: %s\n", db_strerror(ret));
			exit(1);
		}
		free(data.data);
		free(key.data);
		memset(&key, 0, sizeof(key));
		memset(&data, 0, sizeof(data));
	}
	for (;;) 
	{
		key.flags |= DB_DBT_MALLOC;
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
		fprintf(stderr, "key: %s\n", (char *) key.data);
		if (key.data)
			free(key.data);
		if (data.data) 
			free(data.data);
	}
	db->close(db, 0);
	return 0;
}
