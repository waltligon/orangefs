#ifndef __CACHE_H
#define __CACHE_H

#define DISCARD_CLUSTER_MAX 32
#define REFILL_CLUSTER_MAX  32

#define DISCARD_CLUSTER_MIN 4
#define DELT_DISCARD_NUM    5

#define LRU_POLICY      1
#define ARC_POLICY      2
#define TWOQ_POLICY       3

struct extent *lookup_cache_item(struct inode *mapping, unsigned long offset);
struct extent *get_free_extent_list_item(struct list_head *list);
int add_cache_item(struct extent *page, struct inode *mapping, 
                    unsigned long index, int policy);
void remove_cache_item(struct extent *page, int policy);
int shrink_cache(struct cache_stack *cache_stack, unsigned int expected, 
                    int policy, unsigned int *shrinked);
int is_extent_discardable(struct extent *victim);
void hit_cache_item(struct extent *page, int policy);

#endif
