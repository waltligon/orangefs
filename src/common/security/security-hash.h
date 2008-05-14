#include "quickhash.h"
#include "pvfs2-types.h"
#include <stdlib.h>
#include <openssl/evp.h>

struct hash_struct {
	struct qlist_head hash_link;
    PVFS_handle handle;
    EVP_PKEY public_key;
}; 

void SEC_hash_init(void);
void SEC_add_key(PVFS_handle, EVP_PKEY);
EVP_PKEY *SEC_lookup_key(PVFS_handle, EVP_PKEY);
void SEC_hash_finalize(void);
int SEC_compare(void *, struct qhash_head *);
