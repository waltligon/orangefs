#ifndef __NCAC_LRU_H_
#define __NCAC_LRU_H_

void LRU_add_cache_item(struct cache_stack *cache,struct extent
*extent);
void LRU_remove_cache_item(struct cache_stack *cache, struct extent
*extent);
int LRU_shrink_cache(struct cache_stack *cache, unsigned int expected,
                unsigned int *shrinked);
#endif
