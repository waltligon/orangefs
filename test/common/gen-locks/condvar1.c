
#include <stdlib.h>
#include <assert.h>

#include "gen-locks.h"

static gen_cond_t cv = NULL;

int main()
{
  assert(cv == NULL);

  assert(gen_cond_init(&cv) == 0);

  assert(cv != NULL);

  assert(gen_cond_destroy(&cv) == 0);

  assert(cv == NULL);
 
  return 0;
}