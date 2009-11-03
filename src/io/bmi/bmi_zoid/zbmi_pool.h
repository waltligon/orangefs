#ifndef ZBMI_POOL_H
#define ZBMI_POOL_H

void zbmi_pool_init(void* start, size_t len);
void zbmi_pool_fini(void);

void* zbmi_pool_malloc(size_t bytes);
void zbmi_pool_free(void* mem);

#endif
