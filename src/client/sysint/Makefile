CC=clang
#CFLAGS="-m32"

# -Wpadded
cache: client-cache.c client-cache.h
	${CC} ${CFLAGS} -g -Wall -o cache client-cache.c -lpthread

clean:
	rm -f cache
