CC=clang
#CFLAGS="-m32"

cache: client-cache.c client-cache.h
	${CC} ${CFLAGS} -g -Wall -Wpadded -o cache client-cache.c -lpthread

clean:
	rm -f cache
