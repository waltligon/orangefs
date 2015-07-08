#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "bit-bool.h"
#include "pvfs2-internal.h"

void bit_bool_dump_h2l(bit_bool_t *bb, FILE *out)
{
    if(bb != NULL)
    {
        fprintf(out,
                "bb = {llu_array = %p, llu_count = %llu, "
                "bool_count = %llu}\n",
                bb->llu_array,
                llu(bb->llu_count),
                llu(bb->bool_count));

        fprintf(out, "bb bitmask h2l: {\n");
        uint64_t i = 0LL;
        for(; i < bb->llu_count; i++)
        {
            int j = 0;
            int j_keep = 0;
            if((i == bb->llu_count - 1LL) &&
               DIV64(bb->bool_count) == (bb->llu_count - 1LL))
            {
                j = MOD64(bb->bool_count) - 1;
            }
            else
            {
                j = 63;
            }
            j_keep = j;
            for(; j >= 0; j--)
            {
                if(i != 0 && j == j_keep)
                {
                    fprintf(out, "\n");
                }
                else if(j != 63 && ((j + 1) % 8 == 0))
                {
                    fprintf(out, " ");
                }
                fprintf(out,
                        "%c",
                        bit_bool_get(bb, (i * 64LL) + j) ? '1' : '0');
            }
        }
        fprintf(out, "\n}\n");
    }
}

void bit_bool_dump_l2h(bit_bool_t *bb, FILE *out)
{
    if(bb != NULL)
    {
        fprintf(out,
                "bb = {llu_array = %p, llu_count = %llu, "
                "bool_count = %llu}\n",
                bb->llu_array,
                llu(bb->llu_count),
                llu(bb->bool_count));

        fprintf(out, "bb bitmask l2h: {\n");
        uint64_t i = 0LL;
        for(; i < bb->bool_count; i++)
        {
            if(i > 0 && (i % 64 == 0))
            {
                fprintf(out, "\n");
            }
            else if(i > 0 && (i % 8 == 0))
            {
                fprintf(out, " ");
            }
            fprintf(out, "%c", bit_bool_get(bb, i) ? '1' : '0');
        }
        fprintf(out, "\n}\n");
    }
}

void bit_bool_fini(bit_bool_t *bb)
{
    if(bb != NULL)
    {
        free(bb->llu_array);
    }
}

int bit_bool_get(bit_bool_t *bb, uint64_t bool_index)
{
    assert(bb);
    assert(bool_index < bb->bool_count);
    uint64_t llu_index = llu(DIV64(bool_index));
    uint8_t bit = MOD64(bool_index);
    if(bb->llu_array[llu_index] & (1LL << bit))
    {
        return 1;
    }
    return 0;
}

int bit_bool_init(bit_bool_t *bb, uint64_t bool_count, int value)
{
    int remainder = MOD64(bool_count);

    if(bb == NULL)
    {
        return -1;
    }

    bb->llu_count = remainder ? DIV64(bool_count) + 1 : DIV64(bool_count);
    bb->bool_count = bool_count;

    if(bb->bool_count == 0 || bb->llu_count == 0)
    {
        return -1;
    }

    bb->llu_array = (uint64_t *) malloc(bb->llu_count * sizeof(uint64_t));
    if(bb->llu_array == NULL)
    {
        return -1;
    }

    if(value)
    {
        memset((void *) bb->llu_array, 0xFF, bb->llu_count * sizeof(uint64_t));
    }
    else
    {
        memset((void *) bb->llu_array, 0x00, bb->llu_count * sizeof(uint64_t));
    }

    return 0;
}

void bit_bool_set(bit_bool_t *bb, uint64_t bool_index, int value)
{
    assert(bb);
    assert(bool_index < bb->bool_count);
    uint64_t llu_index = llu(DIV64(bool_index));
    uint8_t bit = MOD64(bool_index);

    if(value)
    {
        bb->llu_array[llu_index] |= (1LL << bit);
    }
    else
    {
        bb->llu_array[llu_index] &= ~(1LL << bit);
    }
}

