#ifndef __CACHE_H
#define __CACHE_H

#define DISCARD_CLUSTER_MAX 32
#define REFILL_CLUSTER_MAX  32

#define DISCARD_CLUSTER_MIN 4
#define DELT_DISCARD_NUM    5

struct extent * lookup_cache_item(struct inode *mapping, unsigned long offset);

struct extent * get_free_extent_list_item(struct list_head *list);

int add_cache_item(struct extent *page, struct inode *mapping, unsigned long offset);

void list_set_clean_page(struct extent *page);
int  try_to_discard_extents( struct cache_stack *cache_stack, unsigned int num);




#endif
