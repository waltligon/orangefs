#ifndef __CACHE_LOCK_H_
#define __CACHE_LOCK_H_

#include "gen-locks.h"

#define spin_lock_init(x)       gen_posix_mutex_init(x)
#define cache_lock(x)           gen_posix_mutex_lock(x)
#define cache_unlock(x)         gen_posix_mutex_unlock(x)

#define inode_lock(x)           gen_posix_mutex_lock(x)
#define inode_unlock(x)         gen_posix_mutex_unlock(x)

#define list_lock(x)            gen_posix_mutex_lock(x)
#define list_unlock(x)          gen_posix_mutex_unlock(x)

#endif

