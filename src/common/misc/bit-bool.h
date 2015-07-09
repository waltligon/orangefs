#ifndef BIT_BOOL_H
#define BIT_BOOL_H 1

#include <stdint.h>

/* #define OPT_CODE */
#ifndef OPT_CODE
#define DIV64(a)    ((a) / 64)
#define MOD64(a)    ((a) % 64)
#else
#define DIV64(a)    ((a) >> 6)
#define MOD64(a)    ((a) - 64 * ((a) >> 6))
#endif

typedef struct bit_bool_s
{
    uint64_t *llu_array;
    uint64_t llu_count;
    uint64_t bool_count;
} bit_bool_t;

void bit_bool_dump_h2l(bit_bool_t *bb, FILE *out);

void bit_bool_dump_l2h(bit_bool_t *bb, FILE *out);

void bit_bool_fini(bit_bool_t *bb);

int bit_bool_get(bit_bool_t *bb, uint64_t bool_index);

int bit_bool_init(bit_bool_t *bb, uint64_t bool_count, int value);

void bit_bool_set(bit_bool_t *bb, uint64_t bool_index, int value);

#endif
