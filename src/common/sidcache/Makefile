
CFLAGS= -g -O0 -I/opt/db4/include -I../../.. -I../../../include -I../gossip \
               -I../quicklist -I../../io/bmi \
               -L/opt/db4/lib

OBJS= sidcache.o pvfs-sid.o policyeval.o example2.o ../gossip/gossip.o ../gen-locks/gen-locks.o

testsidca: testsidca.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -pthread -luuid -ldb


