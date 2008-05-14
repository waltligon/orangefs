#include "quickhash.h"
#include "pint-security.h"

typedef struct {
	struct qlist_head hash_link;
    PVFS_handle range;
    EVP_PKEY public_key;
} hash_struct;

void SEC_hash_init();
void SEC_add_key(PVFS_handle, EVP_PKEY);
EVP_PKEY *SEC_lookup_key(PVFS_handle, EVP_PKEY);
void SEC_hash_finalize();
bool SEC_compare(void *, struct qhash_head *);
