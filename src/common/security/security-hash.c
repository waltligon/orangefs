#include "quickhash.h"
#include "pint-security.h"
#include "security-hash.h"

#define TABLE_SIZE 71

qhash_table *hash_table = NULL;

void SEC_hash_init() {
	hash_table = qhash_init(SEC_compare, quickhash_64bit_hash, TABLE_SIZE);
}

void SEC_hash_finalize() {
	qhash_finalize(hash_table);
}

void SEC_add_key(PVFS_handle range, EVP_PKEY key) {
	struct hash_struct *temp;
	temp = (struct hash_struct *)malloc(sizeof(struct hash_struct), 1);
	temp->handle = range;
	temp->public_key = key;
	qhash_add(hash_table, &temp->handle, &temp->hash_link);
}

EVP_PKEY *SEC_lookup_key(PVFS_handle range, EVP_PKEY key) {
	struct hash_struct *temp;
	temp = (struct hash_struct *) qhash_search(hash_table, &key);
	assert(temp);
	if (temp->public_key != NULL)
		return &temp->public_key;
	else return NULL;
}

bool SEC_compare(void *key, struct qhash_head *link) {
	PVFS_handle range = *((PVFS_hangle *)key);
	hash_struct *temp = NULL;
	temp = qlist_entry(link, hash_struct, range);
	assert(temp);
	return (handle == range);
}
