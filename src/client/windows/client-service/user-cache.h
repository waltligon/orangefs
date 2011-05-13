/* Copyright (C) 2011 Omnibond, LLC
   User cache declarations */

#include "pvfs2.h"
#include "quickhash.h"

struct user_entry
{
    struct qhash_head hash_link;
    char user_name[256];
    PVFS_credentials credentials;
    time_t expires;
};

int user_compare(void *key, 
                 struct qhash_head *link);

int add_user(char *user_name, 
             PVFS_credentials *credentials,
             time_t expires);

int get_cached_user(char *user_name, 
                    PVFS_credentials *credentials);

int remove_user(char *user_name);

unsigned int user_cache_thread(void *options);

